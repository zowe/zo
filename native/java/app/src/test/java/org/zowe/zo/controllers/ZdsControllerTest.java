package org.zowe.zo.controllers;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZdsBindings;
import org.zowe.zo.ffm.ZdsService;

import java.util.List;

import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@ExtendWith(MockitoExtension.class)
class ZdsControllerTest {

    @Mock
    ZdsService zdsService;

    MockMvc mockMvc;
    ObjectMapper objectMapper = new ObjectMapper();

    private static final String BASE = "/zosmf/restfiles/ds";

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZdsController(zdsService))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    void listDataSets_returnsItemsAndCount() throws Exception {
        var entry = new ZdsBindings.ZDSEntry();
        entry.name = "USER.DATA";
        entry.dsorg = "PS";
        entry.volser = "VOL001";
        entry.recfm = "FB";
        entry.migrated = false;

        when(zdsService.listDataSets("USER.**")).thenReturn(List.of(entry));

        mockMvc.perform(get(BASE).param("dslevel", "USER"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedRows").value(1))
               .andExpect(jsonPath("$.items[0].name").value("USER.DATA"))
               .andExpect(jsonPath("$.items[0].dsorg").value("PS"));
    }

    @Test
    void listDataSets_emptyResult_returnsZeroCount() throws Exception {
        when(zdsService.listDataSets(any())).thenReturn(List.of());

        mockMvc.perform(get(BASE).param("dslevel", "SYS"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedRows").value(0))
               .andExpect(jsonPath("$.items").isArray());
    }

    @Test
    void listDataSets_emptyDslevel_returns400() throws Exception {
        mockMvc.perform(get(BASE).param("dslevel", ""))
               .andExpect(status().isBadRequest())
               .andExpect(jsonPath("$.message").value("dslevel parameter is required"));
    }

    @Test
    void listDataSets_appendsWildcardToDslevel() throws Exception {
        when(zdsService.listDataSets("SYS.**")).thenReturn(List.of());

        mockMvc.perform(get(BASE).param("dslevel", "SYS"))
               .andExpect(status().isOk());

        verify(zdsService).listDataSets("SYS.**");
    }

    @Test
    void listDataSets_serviceThrowsException_returns500() throws Exception {
        when(zdsService.listDataSets(any())).thenThrow(new RuntimeException("Native error"));

        mockMvc.perform(get(BASE).param("dslevel", "USER"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("Native error"));
    }

    @Test
    void readDataSet_returnsContent() throws Exception {
        when(zdsService.readDataSet("USER.DATA", "")).thenReturn("record content");

        mockMvc.perform(get(BASE + "/USER.DATA"))
               .andExpect(status().isOk())
               .andExpect(content().string("record content"));
    }

    @Test
    void readDataSet_withEncoding_passesEncodingToService() throws Exception {
        when(zdsService.readDataSet("USER.DATA", "IBM-1047")).thenReturn("content");

        mockMvc.perform(get(BASE + "/USER.DATA").param("encoding", "IBM-1047"))
               .andExpect(status().isOk());

        verify(zdsService).readDataSet("USER.DATA", "IBM-1047");
    }

    @Test
    void readDataSet_withoutEncoding_usesEmptyString() throws Exception {
        when(zdsService.readDataSet(any(), eq(""))).thenReturn("data");

        mockMvc.perform(get(BASE + "/USER.DATA"))
               .andExpect(status().isOk());

        verify(zdsService).readDataSet("USER.DATA", "");
    }

    @Test
    void createDataSet_withAttributes_returnsSuccessMessage() throws Exception {
        var attrs = new ZdsBindings.DSAttributes();
        attrs.dsorg = "PS";
        attrs.recfm = "FB";
        attrs.lrecl = 80;
        attrs.primary = 10;

        mockMvc.perform(post(BASE + "/USER.NEW.DS")
                .contentType(MediaType.APPLICATION_JSON)
                .content(objectMapper.writeValueAsString(attrs)))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Data set created successfully"));

        verify(zdsService).createDataSet(eq("USER.NEW.DS"), any(ZdsBindings.DSAttributes.class));
    }

    @Test
    void createDataSet_withNullBody_invokesServiceWithNull() throws Exception {
        mockMvc.perform(post(BASE + "/USER.NEW.DS")
                .contentType(MediaType.APPLICATION_JSON))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Data set created successfully"));

        verify(zdsService).createDataSet("USER.NEW.DS", null);
    }

    @Test
    void createMember_buildsMemberNameAndReturnsSuccess() throws Exception {
        mockMvc.perform(post(BASE + "/USER.PDS/member/MEMBER1"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Member created successfully"));

        verify(zdsService).createMember("USER.PDS(MEMBER1)");
    }

    @Test
    void listMembers_returnsAllMembers() throws Exception {
        var mem1 = new ZdsBindings.ZDSMem();
        mem1.name = "MEMBER1";
        var mem2 = new ZdsBindings.ZDSMem();
        mem2.name = "MEMBER2";

        when(zdsService.listMembers("USER.PDS")).thenReturn(List.of(mem1, mem2));

        mockMvc.perform(get(BASE + "/USER.PDS/members"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedRows").value(2))
               .andExpect(jsonPath("$.items[0].name").value("MEMBER1"))
               .andExpect(jsonPath("$.items[1].name").value("MEMBER2"));
    }

    @Test
    void listMembers_emptyPds_returnsZeroCount() throws Exception {
        when(zdsService.listMembers(any())).thenReturn(List.of());

        mockMvc.perform(get(BASE + "/USER.PDS/members"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedRows").value(0));
    }

    @Test
    void writeDataSet_returnsEtag() throws Exception {
        when(zdsService.writeDataSet("USER.DATA", "new content", "", "")).thenReturn("etag-abc123");

        mockMvc.perform(put(BASE + "/USER.DATA")
                .contentType(MediaType.TEXT_PLAIN)
                .content("new content"))
               .andExpect(status().isOk())
               .andExpect(content().string("etag-abc123"));
    }

    @Test
    void writeDataSet_withEncoding_passesEncodingToService() throws Exception {
        when(zdsService.writeDataSet(any(), any(), eq("IBM-1047"), any())).thenReturn("");

        mockMvc.perform(put(BASE + "/USER.DATA")
                .param("encoding", "IBM-1047")
                .contentType(MediaType.TEXT_PLAIN)
                .content("content"))
               .andExpect(status().isOk());

        verify(zdsService).writeDataSet("USER.DATA", "content", "IBM-1047", "");
    }

    @Test
    void deleteDataSet_returnsSuccessMessage() throws Exception {
        mockMvc.perform(delete(BASE + "/USER.DATA"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.message").value("Deleted successfully"));

        verify(zdsService).deleteDataSet("USER.DATA");
    }
}
