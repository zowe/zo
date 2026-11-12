/**
 * This program and the accompanying materials are made available under the terms of the
 * Eclipse Public License v2.0 which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v20.html
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Copyright Contributors to the Zowe Project.
 *
 */

package org.zowe.zo;

import org.aspectj.lang.ProceedingJoinPoint;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.config.ThreadSecurityAspect;
import org.zowe.zo.controllers.zo.ZoCertificateController;
import org.zowe.zo.controllers.zo.ZoExceptionAdvice;
import org.zowe.zo.controllers.zosmf.ZosmfUssController;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcBindings;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.CertificateService;
import org.zowe.zo.service.DataSetService;
import org.zowe.zo.service.UssService;
import org.zowe.zo.zos.security.service.PlatformSecurityService;
import org.zowe.zo.zos.security.thread.PlatformThreadLevelSecurity;

import java.util.Map;
import java.util.concurrent.Callable;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.put;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

/** Regressions for the findings of the branch review (pr_review.md). */
@ExtendWith(MockitoExtension.class)
class ReviewRegressionTest {

    @Mock
    ZoRpcClient rpc;

    @Mock
    ZoContentIo contentIo;

    private MockMvc uss() {
        return MockMvcBuilders.standaloneSetup(new ZosmfUssController(new UssService(rpc, contentIo)))
                .setControllerAdvice(new GlobalExceptionHandler()).build();
    }

    @Test
    void zosmfCopyWithoutOverwriteLeavesExistingTargetAlone() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(null);

        uss().perform(put("/zosmf/restfiles/fs/u/b").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"request\":\"copy\",\"from\":\"/u/a\",\"overwrite\":false}"))
                .andExpect(status().isConflict());

        verify(rpc, never()).call(eq("copyUss"), any());
    }

    @Test
    void zosmfCopyWithoutOverwriteProceedsWhenTargetIsMissing() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenThrow(
                new ZoRpcException("listFiles", -1, "Path '/u/b' does not exist", null));

        uss().perform(put("/zosmf/restfiles/fs/u/b").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"request\":\"copy\",\"from\":\"/u/a\"}"))
                .andExpect(status().isCreated());

        verify(rpc).call(eq("copyUss"), any());
    }

    @Test
    void nativePathDoesNotExistMapsTo404() throws Exception {
        when(rpc.call(eq("deleteFile"), any())).thenThrow(
                new ZoRpcException("deleteFile", -1, "Path '/u/x' does not exist", null));

        uss().perform(org.springframework.test.web.servlet.request.MockMvcRequestBuilders
                        .delete("/zosmf/restfiles/fs/u/x").header("X-CSRF-ZOSMF-HEADER", "true"))
                .andExpect(status().isNotFound());
    }

    @Test
    void volumeQualifiedDeleteIsRejectedBeforeDispatch() {
        var service = new DataSetService(rpc, contentIo);
        var e = assertThrows(ZoRpcException.class,
                () -> service.delete(DataSetService.Target.of("USER.DATA", null, "VOL002")));
        assertEquals(ZoRpcException.METHOD_NOT_FOUND, e.getCode());
        verifyNoInteractions(rpc);
    }

    @Test
    void getExportNeverWritesAServerSideDestination() throws Exception {
        MockMvc mvc = MockMvcBuilders.standaloneSetup(new ZoCertificateController(new CertificateService(rpc)))
                .setControllerAdvice(new GlobalExceptionHandler(), new ZoExceptionAdvice()).build();

        mvc.perform(get("/zo/api/v1/keyrings/USER01/RING1/certificates/CERT1/export").param("file", "/u/user01/x.pem"))
                .andExpect(status().isBadRequest());
        mvc.perform(get("/zo/api/v1/keyrings/USER01/RING1/certificates/CERT1/export").param("dsn", "USER01.CERT"))
                .andExpect(status().isBadRequest());

        verifyNoInteractions(rpc);
    }

    @Test
    void rpcRejectsCharactersOutsideLatin1BeforeNativeCall() {
        var bindings = new ZoRpcBindings();
        assertThrows(IllegalArgumentException.class,
                () -> bindings.call("deleteFile", Map.of("fspath", "/tmp/caf€")));
    }

    @Test
    void securityAspectRethrowsDeclaredExceptionsUnwrapped() throws Throwable {
        PlatformThreadLevelSecurity threadSecurity = org.mockito.Mockito.mock(PlatformThreadLevelSecurity.class);
        when(threadSecurity.wrapCallableInEnvironmentForAuthenticatedUser(any()))
                .thenAnswer(invocation -> invocation.<Callable<?>>getArgument(0));
        PlatformSecurityService securityService = org.mockito.Mockito.mock(PlatformSecurityService.class);
        ProceedingJoinPoint joinPoint = org.mockito.Mockito.mock(ProceedingJoinPoint.class);
        var failure = new ZoRpcException("getJobStatus", ZoRpcException.INTERNAL_ERROR, "could not locate job", null);
        when(joinPoint.proceed()).thenThrow(failure);

        var aspect = new ThreadSecurityAspect(threadSecurity, securityService);
        Throwable thrown = assertThrows(Throwable.class, () -> aspect.wrapInSecurityContext(joinPoint));
        assertSame(failure, thrown);
    }
}
