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

package org.zowe.zo.service;

import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

/** Spool reads use the binary content shim, not the base64 JSON-RPC path. */
class JobServiceSpoolTest {

    private final ZoRpcClient rpc = mock(ZoRpcClient.class);
    private final ZoContentIo contentIo = mock(ZoContentIo.class);
    private final JobService service = new JobService(rpc, contentIo);

    @Test
    void textSpoolReadUsesContentShimWithEncodings() throws Exception {
        when(contentIo.readSpool(eq("JOB00001"), eq(3), any())).thenReturn(new ZoContentIo.Content("hello".getBytes(), null));

        assertArrayEquals("hello".getBytes(), service.readSpool("JOB00001", 3, "IBM-037"));

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(contentIo).readSpool(eq("JOB00001"), eq(3), options.capture());
        assertEquals("IBM-037", options.getValue().encoding());
        assertEquals("UTF-8", options.getValue().localEncoding());
        verifyNoInteractions(rpc);
    }

    @Test
    void binarySpoolReadHasNoLocalEncoding() throws Exception {
        when(contentIo.readSpool(any(), anyInt(), any())).thenReturn(new ZoContentIo.Content(new byte[]{1, 2}, null));

        service.readSpool("JOB00001", 1, "binary");

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(contentIo).readSpool(eq("JOB00001"), eq(1), options.capture());
        assertEquals("binary", options.getValue().encoding());
        assertNull(options.getValue().localEncoding());
    }

    @Test
    void missingSpoolFileMapsToNotFound() throws Exception {
        when(contentIo.readSpool(any(), anyInt(), any()))
                .thenThrow(new ZoRpcException("readSpool", ZoRpcException.NOT_FOUND, "could not read spool file", null));
        var e = assertThrows(ZoRpcException.class, () -> service.readSpool("JOB00001", 9, null));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }
}
