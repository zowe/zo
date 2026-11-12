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

package org.zowe.zo.controllers.zosmf;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.filter.ForwardedHeaderFilter;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.JobService;

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
class ZosmfJobControllerTest {

    private static final String BASE = "/zosmf/restjobs/jobs";
    private static final String JOB = BASE + "/MYJOB/JOB00001";

    @Mock
    ZoRpcClient rpc;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZosmfJobController(new JobService(rpc)))
                .setControllerAdvice(new GlobalExceptionHandler())
                .addFilters(new ForwardedHeaderFilter())
                .build();
    }

    private void stubJob() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(job("JOB00001", "MYJOB"));
    }

    @Test
    void listUsesZosmfDefaultsAndFieldNames() throws Exception {
        when(rpc.call(eq("listJobs"), any())).thenReturn(json("{\"items\":[" + job("JOB00001", "MYJOB") + "]}"));

        mockMvc.perform(get(BASE).param("owner", "ibmuser"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$[0].jobid").value("JOB00001"))
                .andExpect(jsonPath("$[0].jobname").value("MYJOB"))
                .andExpect(jsonPath("$[0].subsystem").value("JES2"))
                .andExpect(jsonPath("$[0].owner").value("IBMUSER"))
                .andExpect(jsonPath("$[0].status").value("OUTPUT"))
                .andExpect(jsonPath("$[0].type").value("JOB"))
                .andExpect(jsonPath("$[0].class").value("A"))
                .andExpect(jsonPath("$[0].retcode").value("CC 0000"))
                .andExpect(jsonPath("$[0].job-correlator").value(CORRELATOR))
                .andExpect(jsonPath("$[0].phase").value(20))
                .andExpect(jsonPath("$[0].phase-name").value("Job is on the hard copy queue"))
                .andExpect(jsonPath("$[0].url").value("http://localhost/zosmf/restjobs/jobs/MYJOB/JOB00001"))
                .andExpect(jsonPath("$[0].files-url").value("http://localhost/zosmf/restjobs/jobs/MYJOB/JOB00001/files"))
                .andExpect(jsonPath("$[0].exec-started").doesNotExist());

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("listJobs"), params.capture());
        assertEqualsMap(Map.of("owner", "IBMUSER", "prefix", "*", "maxItems", 1000), params.getValue());
    }

    @Test
    void listWithExecDataExposesExecFields() throws Exception {
        when(rpc.call(eq("listJobs"), any())).thenReturn(json("{\"items\":[" + job("JOB00001", "MYJOB") + "]}"));

        mockMvc.perform(get(BASE).param("owner", "*").param("prefix", "MY*").param("max-jobs", "5").param("status", "OUTPUT").param("exec-data", "Y"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$[0].exec-system").value("SY1"))
                .andExpect(jsonPath("$[0].exec-member").value("JES2"))
                .andExpect(jsonPath("$[0].exec-submitted").value("2026-01-01T10:00:00.000Z"))
                .andExpect(jsonPath("$[0].exec-started").value("2026-01-01T10:00:01.000Z"))
                .andExpect(jsonPath("$[0].exec-ended").value("2026-01-01T10:00:02.000Z"));

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("listJobs"), params.capture());
        assertEqualsMap(Map.of("owner", "*", "prefix", "MY*", "status", "OUTPUT", "maxItems", 5), params.getValue());
    }

    @Test
    void listByJobIdReturnsSingleJobOrEmpty() throws Exception {
        stubJob();

        mockMvc.perform(get(BASE).param("jobid", "JOB00001"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$", hasSize(1)));

        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00404")))
                .thenThrow(new ZoRpcException("getJobStatus", ZoRpcException.NOT_FOUND, "missing", null));
        mockMvc.perform(get(BASE).param("jobid", "JOB00404"))
                .andExpect(status().isOk())
                .andExpect(content().json("[]"));
    }

    @Test
    void listRejectsInvalidFilters() throws Exception {
        mockMvc.perform(get(BASE).param("owner", "x;y")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("prefix", "TOOLONGNAME")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("status", "BOGUS")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("max-jobs", "0")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void urlsHonorForwardedHeadersBehindGateway() throws Exception {
        stubJob();

        mockMvc.perform(get(JOB)
                        .header("X-Forwarded-Proto", "https")
                        .header("X-Forwarded-Host", "gateway.example.com:7554")
                        .header("X-Forwarded-Prefix", "/ibmzosmf/api/v1"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.url").value("https://gateway.example.com:7554/ibmzosmf/api/v1/zosmf/restjobs/jobs/MYJOB/JOB00001"))
                .andExpect(jsonPath("$.files-url").value("https://gateway.example.com:7554/ibmzosmf/api/v1/zosmf/restjobs/jobs/MYJOB/JOB00001/files"));
    }

    @Test
    void getJobWithMatchingNameReturnsExecDataOnlyWhenAsked() throws Exception {
        stubJob();

        mockMvc.perform(get(JOB).param("exec-data", "Y"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.jobid").value("JOB00001"))
                .andExpect(jsonPath("$.exec-ended").value("2026-01-01T10:00:02.000Z"));
        mockMvc.perform(get(JOB))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.exec-ended").doesNotExist());
    }

    @Test
    void getJobWithWrongNameIs404() throws Exception {
        stubJob();

        mockMvc.perform(get(BASE + "/OTHER/JOB00001"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.rc").value(ZoRpcException.NOT_FOUND))
                .andExpect(jsonPath("$.message").exists());
    }

    @Test
    void getJobWithInvalidIdsIs400() throws Exception {
        mockMvc.perform(get(BASE + "/MYJOB/NOTAJOB")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/BAD%20NAME/JOB00001")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void getJobByCorrelator() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", CORRELATOR))).thenReturn(job("JOB00001", "MYJOB"));

        mockMvc.perform(get(BASE + "/-" + CORRELATOR))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.jobname").value("MYJOB"));
        mockMvc.perform(get(BASE + "/-short")).andExpect(status().isBadRequest());
    }

    @Test
    void spoolFileListUsesZosmfFieldNames() throws Exception {
        stubJob();
        when(rpc.call("listSpools", Map.of("jobId", "JOB00001"))).thenReturn(json(
                "{\"items\":[{\"id\":2,\"ddname\":\"JESMSGLG\",\"stepname\":\"JES2\",\"dsname\":\"X\",\"procstep\":\"\"}]}"));

        mockMvc.perform(get(JOB + "/files"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$[0].id").value(2))
                .andExpect(jsonPath("$[0].ddname").value("JESMSGLG"))
                .andExpect(jsonPath("$[0].stepname").value("JES2"))
                .andExpect(jsonPath("$[0].procstep").doesNotExist())
                .andExpect(jsonPath("$[0].jobid").value("JOB00001"))
                .andExpect(jsonPath("$[0].jobname").value("MYJOB"))
                .andExpect(jsonPath("$[0].subsystem").value("JES2"))
                .andExpect(jsonPath("$[0].records-url").value("http://localhost/zosmf/restjobs/jobs/MYJOB/JOB00001/files/2/records"));
    }

    private void stubSpool(String text) throws Exception {
        stubJob();
        when(rpc.call(eq("readSpool"), any())).thenReturn(json("{\"data\":\"" + Base64.getEncoder().encodeToString(text.getBytes()) + "\"}"));
    }

    @Test
    void recordsReturnTextByDefault() throws Exception {
        stubSpool("line0\nline1\nline2\n");

        mockMvc.perform(get(JOB + "/files/2/records"))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.TEXT_PLAIN))
                .andExpect(content().string("line0\nline1\nline2\n"));

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("readSpool"), params.capture());
        assertEqualsMap(Map.of("jobId", "JOB00001", "spoolId", 2, "localEncoding", "UTF-8"), params.getValue());
    }

    @Test
    void recordsHonorFileEncodingAndBinaryMode() throws Exception {
        stubSpool("abc");

        mockMvc.perform(get(JOB + "/files/3/records").param("fileEncoding", "IBM-037")).andExpect(status().isOk());
        mockMvc.perform(get(JOB + "/files/3/records").param("mode", "binary"))
                .andExpect(status().isOk())
                .andExpect(content().contentType(MediaType.APPLICATION_OCTET_STREAM));

        verify(rpc).call("readSpool", Map.of("jobId", "JOB00001", "spoolId", 3, "encoding", "IBM-037", "localEncoding", "UTF-8"));
        verify(rpc).call("readSpool", Map.of("jobId", "JOB00001", "spoolId", 3, "encoding", "binary"));
    }

    @Test
    void recordRangeSlicesLines() throws Exception {
        stubSpool("line0\nline1\nline2\nline3\n");

        mockMvc.perform(get(JOB + "/files/2/records").header("X-IBM-Record-Range", "1-2"))
                .andExpect(content().string("line1\nline2\n"));
        mockMvc.perform(get(JOB + "/files/2/records").header("X-IBM-Record-Range", "2,1"))
                .andExpect(content().string("line2\n"));
        mockMvc.perform(get(JOB + "/files/2/records").header("X-IBM-Record-Range", "3-"))
                .andExpect(status().isBadRequest());
        mockMvc.perform(get(JOB + "/files/2/records").header("X-IBM-Record-Range", "9-12"))
                .andExpect(content().string(""));
    }

    @Test
    void recordModeAndBadIdAreRejected() throws Exception {
        mockMvc.perform(get(JOB + "/files/2/records").param("mode", "record")).andExpect(status().isNotImplemented());
        mockMvc.perform(get(JOB + "/files/2/records").param("mode", "weird")).andExpect(status().isBadRequest());
        mockMvc.perform(get(JOB + "/files/abc/records")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void binaryModeWithRecordRangeIs501() throws Exception {
        stubJob();

        mockMvc.perform(get(JOB + "/files/2/records").param("mode", "binary").header("X-IBM-Record-Range", "0-1"))
                .andExpect(status().isNotImplemented());
    }

    @Test
    void jclRecordsUseGetJcl() throws Exception {
        stubJob();
        when(rpc.call("getJcl", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"data\":\"//MYJOB JOB\\n//S1 EXEC PGM=IEFBR14\\n\"}"));

        mockMvc.perform(get(JOB + "/files/JCL/records"))
                .andExpect(status().isOk())
                .andExpect(content().string("//MYJOB JOB\n//S1 EXEC PGM=IEFBR14\n"));
        mockMvc.perform(get(JOB + "/files/JCL/records").header("X-IBM-Record-Range", "1-1"))
                .andExpect(content().string("//S1 EXEC PGM=IEFBR14\n"));
        mockMvc.perform(get(JOB + "/files/JCL/records").param("mode", "binary")).andExpect(status().isNotImplemented());
    }

    @Test
    void submitPlainTextJclReturns201WithJob() throws Exception {
        when(rpc.call(eq("submitJcl"), any())).thenReturn(json("{\"jobId\":\"JOB00001\",\"jobName\":\"MYJOB\"}"));
        stubJob();

        mockMvc.perform(put(BASE).contentType(MediaType.TEXT_PLAIN).content("//MYJOB JOB")
                        .header("X-IBM-Intrdr-Mode", "TEXT").header("X-IBM-Intrdr-Lrecl", "80").header("X-IBM-Intrdr-Recfm", "F")
                        .header("X-IBM-Intrdr-File-Encoding", "IBM-037"))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.jobid").value("JOB00001"))
                .andExpect(jsonPath("$.url").exists());

        verify(rpc).call("submitJcl", Map.of("jcl", Base64.getEncoder().encodeToString("//MYJOB JOB".getBytes()), "localEncoding", "IBM-037"));
    }

    @Test
    void submitFromDatasetUsesSubmitJob() throws Exception {
        when(rpc.call("submitJob", Map.of("dsname", "MY.JCL(MEM)"))).thenReturn(json("{\"jobId\":\"JOB00001\",\"jobName\":\"MYJOB\"}"));
        stubJob();

        mockMvc.perform(put(BASE).contentType(MediaType.APPLICATION_JSON).content("{\"file\":\"//'MY.JCL(MEM)'\"}"))
                .andExpect(status().isCreated())
                .andExpect(jsonPath("$.jobname").value("MYJOB"));
    }

    @Test
    void submitFromUssFileIs501() throws Exception {
        mockMvc.perform(put(BASE).contentType(MediaType.APPLICATION_JSON).content("{\"file\":\"/u/ibmuser/job.jcl\"}"))
                .andExpect(status().isNotImplemented())
                .andExpect(jsonPath("$.message", containsString("USS")));
        verifyNoInteractions(rpc);
    }

    @Test
    void submitRejectsUnsupportedOptionsAndBadInput() throws Exception {
        mockMvc.perform(put(BASE).contentType(MediaType.TEXT_PLAIN).content("//X JOB").header("X-IBM-Intrdr-Class", "B"))
                .andExpect(status().isNotImplemented());
        mockMvc.perform(put(BASE).contentType(MediaType.TEXT_PLAIN).content("//X JOB").header("X-IBM-JCL-Symbol-FOO", "bar"))
                .andExpect(status().isNotImplemented());
        mockMvc.perform(put(BASE).contentType(MediaType.TEXT_PLAIN).content("//X JOB").header("X-IBM-Intrdr-Lrecl", "133"))
                .andExpect(status().isNotImplemented());
        mockMvc.perform(put(BASE).contentType(MediaType.APPLICATION_JSON).content("{}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE).contentType(MediaType.APPLICATION_JSON).content("{\"file\":\"//'A B;C'\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE).contentType(MediaType.TEXT_PLAIN).content("")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void cancelReturnsSynchronousFeedback() throws Exception {
        stubJob();
        when(rpc.call("cancelJob", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"success\":true}"));

        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"cancel\",\"version\":\"2.0\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.jobid").value("JOB00001"))
                .andExpect(jsonPath("$.jobname").value("MYJOB"))
                .andExpect(jsonPath("$.original-jobid").value("JOB00001"))
                .andExpect(jsonPath("$.owner").value("IBMUSER"))
                .andExpect(jsonPath("$.member").value("JES2"))
                .andExpect(jsonPath("$.sysname").value("SY1"))
                .andExpect(jsonPath("$.job-correlator").value(CORRELATOR))
                .andExpect(jsonPath("$.status").value(0));
    }

    @Test
    void holdAndReleaseMapToNativeCalls() throws Exception {
        stubJob();
        when(rpc.call("holdJob", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"success\":true}"));
        when(rpc.call("releaseJob", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"success\":true}"));

        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"hold\",\"version\":\"2.0\"}")).andExpect(status().isOk());
        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"release\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.message").value("Request was successful."))
                .andExpect(jsonPath("$.original-jobid").doesNotExist());

        verify(rpc).call("holdJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("releaseJob", Map.of("jobId", "JOB00001"));
    }

    @Test
    void classChangeIs501AndUnknownRequestIs400() throws Exception {
        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"class\",\"class\":\"B\"}"))
                .andExpect(status().isNotImplemented());
        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"explode\"}"))
                .andExpect(status().isBadRequest());
        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"cancel\",\"version\":\"9.9\"}"))
                .andExpect(status().isBadRequest());
        mockMvc.perform(put(JOB).contentType(MediaType.APPLICATION_JSON).content("{}")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void deletePurgesJobAndHonorsModifyVersion() throws Exception {
        stubJob();
        when(rpc.call("deleteJob", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"success\":true}"));

        mockMvc.perform(delete(JOB).header("X-IBM-Job-Modify-Version", "2.0"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.original-jobid").value("JOB00001"))
                .andExpect(jsonPath("$.status").value(0));
        mockMvc.perform(delete(JOB))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.jobid").value("JOB00001"))
                .andExpect(jsonPath("$.message").exists());

        verify(rpc, times(2)).call("deleteJob", Map.of("jobId", "JOB00001"));
    }

    @Test
    void deleteWithWrongNameDoesNotPurge() throws Exception {
        stubJob();

        mockMvc.perform(delete(BASE + "/OTHER/JOB00001")).andExpect(status().isNotFound());
        verify(rpc, never()).call(eq("deleteJob"), any());
    }

    private static void assertEqualsMap(Map<String, Object> expected, Object actual) {
        org.junit.jupiter.api.Assertions.assertEquals(expected, actual);
    }
}
