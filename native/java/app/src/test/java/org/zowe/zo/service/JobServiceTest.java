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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.JobService.JobAction;
import org.zowe.zo.service.JobService.JobInfo;
import org.zowe.zo.service.JobService.JobQuery;

import java.util.Base64;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class JobServiceTest {

    private static final ObjectMapper JSON = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    @InjectMocks
    JobService service;

    @AfterEach
    void clearContext() {
        SecurityContextHolder.clearContext();
    }

    static JsonNode json(String text) throws Exception {
        return JSON.readTree(text);
    }

    static JsonNode jobNode(String id, String name) throws Exception {
        return json("{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"subsystem\":\"JES2\",\"owner\":\"IBMUSER\","
                + "\"status\":\"OUTPUT\",\"type\":\"JOB\",\"class\":\"A\",\"retcode\":\"CC 0000\",\"correlator\":\"J0000001SY1.....ABCDEF\","
                + "\"phase\":20,\"phaseName\":\"Job is on the hard copy queue\",\"execSystem\":\"SY1\",\"execMember\":\"JES2\","
                + "\"execSubmitted\":\"2026-01-01T10:00:00.000Z\",\"execStarted\":\"2026-01-01T10:00:01.000Z\","
                + "\"execEnded\":\"2026-01-01T10:00:02.000Z\"}");
    }

    @Test
    void listJobsSendsDefaultsFromAuthenticatedUser() throws Exception {
        SecurityContextHolder.getContext().setAuthentication(new UsernamePasswordAuthenticationToken("ibmuser", "x"));
        when(rpc.call(eq("listJobs"), any())).thenReturn(json("{\"success\":true,\"items\":[]}"));

        assertTrue(service.listJobs(new JobQuery(null, null, null, null, null)).isEmpty());

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("listJobs"), params.capture());
        assertEquals(Map.of("owner", "IBMUSER", "prefix", "*", "maxItems", 1000), params.getValue());
    }

    @Test
    void listJobsPassesFiltersAndMapsFields() throws Exception {
        JsonNode result = JSON.createObjectNode().set("items", JSON.createArrayNode().add(jobNode("JOB00001", "MYJOB")));
        when(rpc.call(eq("listJobs"), any())).thenReturn(result);

        List<JobInfo> jobs = service.listJobs(new JobQuery("me", "my*", null, "output", 5));

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("listJobs"), params.capture());
        assertEquals(Map.of("owner", "ME", "prefix", "MY*", "status", "OUTPUT", "maxItems", 5), params.getValue());
        JobInfo job = jobs.get(0);
        assertEquals("JOB00001", job.id());
        assertEquals("MYJOB", job.name());
        assertEquals("A", job.jobClass());
        assertEquals(20, job.phase());
        assertEquals("2026-01-01T10:00:01.000Z", job.execStarted());
    }

    @Test
    void listJobsWithJobIdUsesGetJobStatusAndToleratesMissing() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001")))
                .thenThrow(new ZoRpcException("getJobStatus", ZoRpcException.NOT_FOUND, "nope", null));

        assertTrue(service.listJobs(new JobQuery(null, null, "job00001", null, null)).isEmpty());
        verify(rpc, never()).call(eq("listJobs"), any());
    }

    @Test
    void listJobsRejectsBadInput() {
        assertThrows(IllegalArgumentException.class, () -> service.listJobs(new JobQuery("bad;name", null, null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.listJobs(new JobQuery("A", "TOOLONGNAME", null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.listJobs(new JobQuery("A", null, null, "weird", null)));
        assertThrows(IllegalArgumentException.class, () -> service.listJobs(new JobQuery("A", null, null, null, 0)));
        verifyNoInteractions(rpc);
    }

    @Test
    void getJobVerifiesJobName() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(jobNode("JOB00001", "MYJOB"));

        assertEquals("MYJOB", service.getJob("myjob", "JOB00001").name());
        ZoRpcException ex = assertThrows(ZoRpcException.class, () -> service.getJob("OTHER", "JOB00001"));
        assertEquals(ZoRpcException.NOT_FOUND, ex.getCode());
    }

    @Test
    void getJobByCorrelatorPassesLongRef() throws Exception {
        String correlator = "J0000001SY1.....ABCDEF";
        when(rpc.call("getJobStatus", Map.of("jobId", correlator))).thenReturn(jobNode("JOB00001", "MYJOB"));

        assertEquals("JOB00001", service.getJobByRef(correlator).id());
    }

    @Test
    void unknownJobMessageBecomesNotFound() throws Exception {
        when(rpc.call(eq("getJobStatus"), any())).thenThrow(new ZoRpcException("getJobStatus", ZoRpcException.INTERNAL_ERROR,
                "failed", JSON.getNodeFactory().textNode("Could not locate job with id 'JOB00009'")));

        ZoRpcException ex = assertThrows(ZoRpcException.class, () -> service.getJobByRef("JOB00009"));
        assertEquals(ZoRpcException.NOT_FOUND, ex.getCode());
    }

    @Test
    void invalidJobIdsNeverReachNative() {
        for (String bad : new String[]{"JOB1", "JOB00001; rm", "../x", "ABC12345", "J012"}) {
            assertThrows(IllegalArgumentException.class, () -> service.getJobByRef(bad), bad);
        }
        verifyNoInteractions(rpc);
    }

    @Test
    void modifySendsActionMethodWithJobId() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(jobNode("JOB00001", "MYJOB"));
        for (String method : new String[]{"holdJob", "releaseJob", "cancelJob", "deleteJob"}) {
            when(rpc.call(method, Map.of("jobId", "JOB00001"))).thenReturn(json("{\"success\":true}"));
        }

        for (JobAction action : JobAction.values()) service.modify("MYJOB", "JOB00001", action);

        verify(rpc).call("holdJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("releaseJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("cancelJob", Map.of("jobId", "JOB00001"));
        verify(rpc).call("deleteJob", Map.of("jobId", "JOB00001"));
    }

    @Test
    void modifyDoesNotActWhenJobNameMismatches() throws Exception {
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00001"))).thenReturn(jobNode("JOB00001", "MYJOB"));

        assertThrows(ZoRpcException.class, () -> service.modify("OTHER", "JOB00001", JobAction.DELETE));
        verify(rpc, never()).call(eq("deleteJob"), any());
    }

    @Test
    void listSpoolsMapsItems() throws Exception {
        when(rpc.call("listSpools", Map.of("jobId", "JOB00001"))).thenReturn(json(
                "{\"items\":[{\"id\":2,\"ddname\":\"JESMSGLG\",\"stepname\":\"JES2\",\"dsname\":\"D\",\"procstep\":\"\"}]}"));

        var spools = service.listSpools("JOB00001");
        assertEquals(1, spools.size());
        assertEquals(2, spools.get(0).id());
        assertEquals("JESMSGLG", spools.get(0).ddname());
    }

    @Test
    void readSpoolDecodesBase64AndSendsEncodings() throws Exception {
        when(rpc.call(eq("readSpool"), any())).thenReturn(json("{\"data\":\"" + Base64.getEncoder().encodeToString("hello".getBytes()) + "\"}"));

        assertArrayEquals("hello".getBytes(), service.readSpool("JOB00001", 3, "IBM-037"));

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("readSpool"), params.capture());
        assertEquals(Map.of("jobId", "JOB00001", "spoolId", 3, "encoding", "IBM-037", "localEncoding", "UTF-8"), params.getValue());
    }

    @Test
    void readSpoolBinaryOmitsLocalEncoding() throws Exception {
        when(rpc.call(eq("readSpool"), any())).thenReturn(json("{\"data\":\"\"}"));

        service.readSpool("JOB00001", 1, "binary");

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("readSpool"), params.capture());
        assertEquals(Map.of("jobId", "JOB00001", "spoolId", 1, "encoding", "binary"), params.getValue());
    }

    @Test
    void readSpoolRejectsBadEncodingAndId() {
        assertThrows(IllegalArgumentException.class, () -> service.readSpool("JOB00001", 1, "x; rm -rf"));
        assertThrows(IllegalArgumentException.class, () -> service.readSpool("JOB00001", 0, null));
    }

    @Test
    void getJclReturnsText() throws Exception {
        when(rpc.call("getJcl", Map.of("jobId", "JOB00001"))).thenReturn(json("{\"data\":\"//JOB JOB\"}"));

        assertEquals("//JOB JOB", service.getJcl("JOB00001"));
    }

    @Test
    void submitJclSendsBase64AndFetchesJob() throws Exception {
        when(rpc.call(eq("submitJcl"), any())).thenReturn(json("{\"jobId\":\"JOB00002\",\"jobName\":\"MYJOB\"}"));
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00002"))).thenReturn(jobNode("JOB00002", "MYJOB"));

        JobInfo job = service.submitJcl("//MYJOB JOB".getBytes(), null);

        assertEquals("JOB00002", job.id());
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq("submitJcl"), params.capture());
        assertEquals(Map.of("jcl", Base64.getEncoder().encodeToString("//MYJOB JOB".getBytes()), "localEncoding", "UTF-8"), params.getValue());
    }

    @Test
    void submitJclRejectsEmptyBody() {
        assertThrows(IllegalArgumentException.class, () -> service.submitJcl(new byte[0], null));
    }

    @Test
    void submitDatasetSendsDsname() throws Exception {
        when(rpc.call("submitJob", Map.of("dsname", "MY.JCL(MEM)"))).thenReturn(json("{\"jobId\":\"JOB00003\",\"jobName\":\"X\"}"));
        when(rpc.call("getJobStatus", Map.of("jobId", "JOB00003"))).thenReturn(jobNode("JOB00003", "X"));

        assertEquals("JOB00003", service.submitDataset("my.jcl(mem)").id());
        assertThrows(IllegalArgumentException.class, () -> service.submitDataset("A.B'; rm"));
    }
}
