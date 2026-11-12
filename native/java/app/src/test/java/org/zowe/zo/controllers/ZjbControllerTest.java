package org.zowe.zo.controllers;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZjbBindings;
import org.zowe.zo.ffm.ZjbService;

import java.util.List;

import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@ExtendWith(MockitoExtension.class)
class ZjbControllerTest {

    @Mock
    ZjbService zjbService;

    MockMvc mockMvc;

    private static final String BASE = "/zosmf/restjobs/jobs";

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZjbController(zjbService))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    private ZjbBindings.ZJob makeJob(String jobname, String jobid, String owner, String status) {
        var job = new ZjbBindings.ZJob();
        job.jobname = jobname;
        job.jobid = jobid;
        job.owner = owner;
        job.status = status;
        job.retcode = "CC 0000";
        return job;
    }

    @Test
    void listJobs_withDefaults_usesWildcards() throws Exception {
        when(zjbService.listJobsByOwner("*", "*", "")).thenReturn(List.of());

        mockMvc.perform(get(BASE))
               .andExpect(status().isOk())
               .andExpect(content().json("[]"));

        verify(zjbService).listJobsByOwner("*", "*", "");
    }

    @Test
    void listJobs_withOwnerAndPrefix_returnsMatchingJobs() throws Exception {
        var job = makeJob("MYJOB", "JOB00001", "MYUSER", "OUTPUT");
        when(zjbService.listJobsByOwner("MYUSER", "MYJOB*", "")).thenReturn(List.of(job));

        mockMvc.perform(get(BASE)
                .param("owner", "MYUSER")
                .param("prefix", "MYJOB*"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$[0].jobname").value("MYJOB"))
               .andExpect(jsonPath("$[0].jobid").value("JOB00001"))
               .andExpect(jsonPath("$[0].status").value("OUTPUT"));
    }

    @Test
    void listJobs_withStatus_passesStatusToService() throws Exception {
        when(zjbService.listJobsByOwner("*", "*", "ACTIVE")).thenReturn(List.of());

        mockMvc.perform(get(BASE).param("status", "ACTIVE"))
               .andExpect(status().isOk());

        verify(zjbService).listJobsByOwner("*", "*", "ACTIVE");
    }

    @Test
    void listJobs_multipleResults_returnsAll() throws Exception {
        var job1 = makeJob("JOB1", "JOB00001", "USER1", "OUTPUT");
        var job2 = makeJob("JOB2", "JOB00002", "USER2", "ACTIVE");
        when(zjbService.listJobsByOwner(any(), any(), any())).thenReturn(List.of(job1, job2));

        mockMvc.perform(get(BASE))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.length()").value(2))
               .andExpect(jsonPath("$[0].jobname").value("JOB1"))
               .andExpect(jsonPath("$[1].jobname").value("JOB2"));
    }

    @Test
    void getJobStatus_returnsJobDetails() throws Exception {
        var job = makeJob("MYJOB", "JOB00042", "MYUSER", "OUTPUT");
        job.fullStatus = "CC 0000";
        job.correlator = "correlator-xyz";
        when(zjbService.getJobStatus("JOB00042")).thenReturn(job);

        mockMvc.perform(get(BASE + "/JOB00042"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.jobname").value("MYJOB"))
               .andExpect(jsonPath("$.jobid").value("JOB00042"))
               .andExpect(jsonPath("$.owner").value("MYUSER"))
               .andExpect(jsonPath("$.status").value("OUTPUT"));
    }

    @Test
    void getJobStatus_serviceThrowsException_returns500() throws Exception {
        when(zjbService.getJobStatus(any())).thenThrow(new RuntimeException("Job not found"));

        mockMvc.perform(get(BASE + "/JOB99999"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("Job not found"));
    }

    @Test
    void submitJob_returnsJobId() throws Exception {
        when(zjbService.submitJob(any())).thenReturn("JOB00099");

        mockMvc.perform(put(BASE)
                .contentType(MediaType.TEXT_PLAIN)
                .content("//MYJOB JOB ...\n//STEP1 EXEC PGM=IEFBR14"))
               .andExpect(status().isOk())
               .andExpect(content().string("JOB00099"));
    }

    @Test
    void submitJob_passesJclContentToService() throws Exception {
        String jcl = "//MYJOB JOB CLASS=A\n//STEP EXEC PGM=IEFBR14";
        when(zjbService.submitJob(jcl)).thenReturn("JOB00100");

        mockMvc.perform(put(BASE)
                .contentType(MediaType.TEXT_PLAIN)
                .content(jcl))
               .andExpect(status().isOk());

        verify(zjbService).submitJob(jcl);
    }

    @Test
    void listSpoolFiles_returnsSpoolEntries() throws Exception {
        var dd = new ZjbBindings.ZJobDD();
        dd.jobid = "JOB00001";
        dd.ddn = "SYSOUT";
        dd.dsn = "JOB00001.SYSOUT";
        dd.stepname = "STEP1";
        dd.procstep = "";
        dd.key = 1;
        when(zjbService.listSpoolFiles("JOB00001")).thenReturn(List.of(dd));

        mockMvc.perform(get(BASE + "/JOB00001/spool"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$[0].jobid").value("JOB00001"))
               .andExpect(jsonPath("$[0].ddn").value("SYSOUT"))
               .andExpect(jsonPath("$[0].key").value(1));
    }

    @Test
    void listSpoolFiles_emptyJob_returnsEmptyList() throws Exception {
        when(zjbService.listSpoolFiles(any())).thenReturn(List.of());

        mockMvc.perform(get(BASE + "/JOB00001/spool"))
               .andExpect(status().isOk())
               .andExpect(content().json("[]"));
    }

    @Test
    void readSpoolFile_returnsContent() throws Exception {
        when(zjbService.readSpoolFile("JOB00001", 1)).thenReturn("SYSOUT line 1\nSYSOUT line 2");

        mockMvc.perform(get(BASE + "/JOB00001/spool/1"))
               .andExpect(status().isOk())
               .andExpect(content().string("SYSOUT line 1\nSYSOUT line 2"));
    }

    @Test
    void readSpoolFile_passesKeyToService() throws Exception {
        when(zjbService.readSpoolFile(any(), eq(3))).thenReturn("data");

        mockMvc.perform(get(BASE + "/JOB00001/spool/3"))
               .andExpect(status().isOk());

        verify(zjbService).readSpoolFile("JOB00001", 3);
    }

    @Test
    void getJobJcl_returnsJclContent() throws Exception {
        String jcl = "//MYJOB JOB CLASS=A\n//STEP EXEC PGM=IEFBR14";
        when(zjbService.getJobJcl("JOB00001")).thenReturn(jcl);

        mockMvc.perform(get(BASE + "/JOB00001/jcl"))
               .andExpect(status().isOk())
               .andExpect(content().string(jcl));
    }

    @Test
    void deleteJob_returnsSuccessMessage() throws Exception {
        mockMvc.perform(delete(BASE + "/JOB00001"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Job deleted successfully"));

        verify(zjbService).deleteJob("JOB00001");
    }

    @Test
    void deleteJob_serviceThrowsException_returns500() throws Exception {
        doThrow(new RuntimeException("Delete failed")).when(zjbService).deleteJob(any());

        mockMvc.perform(delete(BASE + "/JOB00001"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("Delete failed"));
    }
}
