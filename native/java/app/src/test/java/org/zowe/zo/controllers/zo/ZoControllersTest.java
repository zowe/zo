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

package org.zowe.zo.controllers.zo;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.service.JobService;
import org.zowe.zo.service.UssService;

import java.util.Base64;
import java.util.Map;

import static org.hamcrest.Matchers.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
import static org.zowe.zo.testutil.TestJson.CORRELATOR;
import static org.zowe.zo.testutil.TestJson.job;
import static org.zowe.zo.testutil.TestJson.json;

@ExtendWith(MockitoExtension.class)
class ZoControllersTest {

    @Mock
    ZoRpcClient rpc;

    @Mock
    ZoContentIo contentIo;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZoJobController(new JobService(rpc)), new ZoUssController(new UssService(rpc, contentIo)))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    void jobListKeepsZoFieldNames() throws Exception {
        when(rpc.call(eq("listJobs"), any())).thenReturn(json("{\"items\":[" + job("JOB00001", "MYJOB") + "]}"));

        mockMvc.perform(get("/zo/api/v1/jobs").param("owner", "*").param("maxItems", "10"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$[0].id").value("JOB00001"))
                .andExpect(jsonPath("$[0].name").value("MYJOB"))
                .andExpect(jsonPath("$[0].phaseName").exists())
                .andExpect(jsonPath("$[0].execStarted").value("2026-01-01T10:00:01.000Z"));

        verify(rpc).call("listJobs", Map.of("owner", "*", "prefix", "*", "maxItems", 10));
    }

    @Test
    void jobByCorrelatorAndSpoolAndJcl() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", CORRELATOR))).thenReturn(job("JOB00001", "MYJOB"));
        when(rpc.call("listSpools", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"items\":[{\"id\":1,\"ddname\":\"JESMSGLG\",\"stepname\":\"JES2\",\"dsname\":\"D\",\"procstep\":\"\"}]}"));
        when(rpc.call("getJcl", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"data\":\"//J JOB\"}"));

        mockMvc.perform(get("/zo/api/v1/jobs/" + CORRELATOR)).andExpect(jsonPath("$.name").value("MYJOB"));
        mockMvc.perform(get("/zo/api/v1/jobs/JOB00001/spool")).andExpect(jsonPath("$[0].dsname").value("D"));
        mockMvc.perform(get("/zo/api/v1/jobs/JOB00001/jcl")).andExpect(content().string("//J JOB"));
    }

    @Test
    void spoolReadBinaryAndText() throws Exception {
        when(rpc.call(eq("readSpool"), any())).thenReturn(json("{\"data\":\"" + Base64.getEncoder().encodeToString("x".getBytes()) + "\"}"));

        mockMvc.perform(get("/zo/api/v1/jobs/JOB00001/spool/2")).andExpect(content().string("x"));
        mockMvc.perform(get("/zo/api/v1/jobs/JOB00001/spool/2").param("encoding", "binary"))
                .andExpect(content().contentType(MediaType.APPLICATION_OCTET_STREAM));
    }

    @Test
    void jobActionsAndDelete() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(job("JOB00001", "MYJOB"));

        mockMvc.perform(post("/zo/api/v1/jobs/JOB00001/hold")).andExpect(status().isOk());
        mockMvc.perform(post("/zo/api/v1/jobs/JOB00001/release")).andExpect(status().isOk());
        mockMvc.perform(post("/zo/api/v1/jobs/JOB00001/cancel")).andExpect(status().isOk());
        mockMvc.perform(delete("/zo/api/v1/jobs/JOB00001")).andExpect(status().isNoContent());
        mockMvc.perform(post("/zo/api/v1/jobs/BAD/hold")).andExpect(status().isBadRequest());

        verify(rpc).call("holdJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("releaseJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("cancelJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("deleteJob", Map.of("jobId", "JOB00001"));
    }

    @Test
    void submitJclAndDataset() throws Exception {
        when(rpc.call(eq("submitJcl"), any())).thenReturn(json("{\"jobId\":\"JOB00001\",\"jobName\":\"MYJOB\"}"));
        when(rpc.call("submitJob", Map.of("dsname", "A.B(C)"))).thenReturn(json("{\"jobId\":\"JOB00001\",\"jobName\":\"MYJOB\"}"));
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(job("JOB00001", "MYJOB"));

        mockMvc.perform(post("/zo/api/v1/jobs").contentType(MediaType.TEXT_PLAIN).content("//MYJOB JOB")).andExpect(status().isCreated()).andExpect(jsonPath("$.id").value("JOB00001"));
        mockMvc.perform(post("/zo/api/v1/jobs/dataset").contentType(MediaType.APPLICATION_JSON).content("{\"dsname\":\"A.B(C)\"}")).andExpect(status().isCreated());
        mockMvc.perform(post("/zo/api/v1/jobs/dataset").contentType(MediaType.APPLICATION_JSON).content("{}")).andExpect(status().isBadRequest());
    }

    private static final String LISTING = "{\"items\":[{\"name\":\"a.txt\",\"mode\":\"-rw-r--r--\",\"links\":1,\"user\":\"IBMUSER\",\"group\":\"SYS1\",\"size\":10,\"filetag\":\"IBM-1047\",\"mtime\":\"2026-01-01T10:00:00\"}]}";

    @Test
    void ussListIncludesFiletagAndLinks() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        mockMvc.perform(get("/zo/api/v1/fs/list").param("path", "/u/ibmuser"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].filetag").value("IBM-1047"))
                .andExpect(jsonPath("$.items[0].links").value(1))
                .andExpect(jsonPath("$.totalRows").value(1));
    }

    @Test
    void ussTextListing() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        mockMvc.perform(get("/zo/api/v1/fs/listing").param("path", "/u/ibmuser"))
                .andExpect(status().isOk())
                .andExpect(content().string("-rw-r--r-- 1 IBMUSER SYS1 10 IBM-1047 2026-01-01T10:00:00 a.txt\n"));
        mockMvc.perform(get("/zo/api/v1/fs/listing").param("path", "nope")).andExpect(status().isBadRequest());
    }

    @Test
    void ussRecursiveChtagAndCopyWithExtras() throws Exception {
        mockMvc.perform(put("/zo/api/v1/fs/chtag").contentType(MediaType.APPLICATION_JSON).content("{\"path\":\"/u/d\",\"tag\":\"819\",\"recursive\":true}"))
                .andExpect(status().isOk());
        mockMvc.perform(post("/zo/api/v1/fs/copy").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"from\":\"/u/a\",\"to\":\"/u/b\",\"recursive\":true,\"followSymlinks\":true,\"preserveAttributes\":true}"))
                .andExpect(status().isCreated());
        mockMvc.perform(put("/zo/api/v1/fs/chtag").contentType(MediaType.APPLICATION_JSON).content("{\"path\":\"/u/d\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(put("/zo/api/v1/fs/chtag").contentType(MediaType.APPLICATION_JSON).content("{\"path\":\"/u/d\",\"tag\":\"x y\"}")).andExpect(status().isBadRequest());

        verify(rpc).call("chtagFile", Map.of("fspath", "/u/d", "tag", "819", "recursive", true));
        verify(rpc).call("copyUss", Map.of("srcFsPath", "/u/a", "dstFsPath", "/u/b", "recursive", true, "followSymlinks", true, "preserveAttributes", true));
        verifyNoMoreInteractions(rpc);
    }
}
