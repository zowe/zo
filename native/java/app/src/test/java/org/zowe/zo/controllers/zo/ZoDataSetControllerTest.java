package org.zowe.zo.controllers.zo;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.DataSetConflictException;
import org.zowe.zo.service.DataSetService;
import org.zowe.zo.service.DataSetService.DataSetInfo;
import org.zowe.zo.service.DataSetService.ListPage;
import org.zowe.zo.service.DataSetService.ListQuery;
import org.zowe.zo.service.DataSetService.MemberInfo;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.delete;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class ZoDataSetControllerTest {

    private static final String BASE = "/zo/api/v1/ds";

    @Mock
    DataSetService service;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders.standaloneSetup(new ZoDataSetController(service))
                .setControllerAdvice(new GlobalExceptionHandler()).build();
    }

    @Test
    void list_returnsFullZoAttributesAndPaging() throws Exception {
        DataSetInfo info = new DataSetInfo("U.PDS", 15L, 1, 80, "2020/01/01", "DC", "3390", "LIBRARY", "PO", null,
                false, 80, "MC", false, false, 5L, null, "FB", 1L, "TRACKS", "SC", 10, 1, "V1", List.of("V1"));
        when(service.listDataSets(any(), any(), any())).thenReturn(new ListPage<>(List.of(info), true, 9));

        mockMvc.perform(get(BASE).param("pattern", "U.*").param("maxItems", "1"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].name").value("U.PDS"))
                .andExpect(jsonPath("$.items[0].dsntype").value("LIBRARY"))
                .andExpect(jsonPath("$.items[0].dataclass").value("DC"))
                .andExpect(jsonPath("$.items[0].volsers[0]").value("V1"))
                .andExpect(jsonPath("$.items[0].edate").doesNotExist())
                .andExpect(jsonPath("$.returnedRows").value(1))
                .andExpect(jsonPath("$.moreRows").value(true))
                .andExpect(jsonPath("$.totalRows").value(9));

        ArgumentCaptor<ListQuery> query = ArgumentCaptor.forClass(ListQuery.class);
        verify(service).listDataSets(eq("U.*"), eq(null), query.capture());
        assertEquals(1, query.getValue().maxItems());
        assertEquals(true, query.getValue().attributes());
        assertEquals(true, query.getValue().total());
    }

    @Test
    void list_requiresPattern() throws Exception {
        mockMvc.perform(get(BASE)).andExpect(status().isBadRequest());
    }

    @Test
    void members_returnFullZoAttributes() throws Exception {
        when(service.listMembers(any(), any(), any())).thenReturn(new ListPage<>(
                List.of(new MemberInfo("M1", "U", 1, 0, "2020/01/01", null, null, 1, 1, 0, "Y")), false, null));

        mockMvc.perform(get(BASE + "/U.PDS/members").param("pattern", "M*"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].name").value("M1"))
                .andExpect(jsonPath("$.items[0].sclm").value("Y"))
                .andExpect(jsonPath("$.moreRows").value(false));
        verify(service).listMembers(eq("U.PDS"), eq("M*"), any());
    }

    @Test
    void createMember_returns201() throws Exception {
        mockMvc.perform(post(BASE + "/U.PDS/member/MEM1")).andExpect(status().isCreated());
        verify(service).createMember("U.PDS", "MEM1", false);
    }

    @Test
    void createMember_overwriteFlagAndConflict() throws Exception {
        mockMvc.perform(post(BASE + "/U.PDS/member/MEM1").param("overwrite", "true")).andExpect(status().isCreated());
        verify(service).createMember("U.PDS", "MEM1", true);

        doThrow(new DataSetConflictException("exists")).when(service).createMember("U.PDS", "MEM2", false);
        mockMvc.perform(post(BASE + "/U.PDS/member/MEM2")).andExpect(status().isConflict())
                .andExpect(jsonPath("$.message").value("exists"));
    }

    @Test
    void createMember_missingDataSetIs404() throws Exception {
        doThrow(new ZoRpcException("createMember", ZoRpcException.NOT_FOUND, "nf", null)).when(service)
                .createMember(any(), any(), eq(false));
        mockMvc.perform(post(BASE + "/NO.PDS/member/M")).andExpect(status().isNotFound());
    }

    @Test
    void search_returnsData() throws Exception {
        when(service.search("U.PDS", "needle", "ANYC")).thenReturn("found");
        mockMvc.perform(get(BASE + "/U.PDS/search").param("string", "needle").param("parms", "ANYC"))
                .andExpect(status().isOk()).andExpect(jsonPath("$.data").value("found"));
    }

    @Test
    void search_requiresString() throws Exception {
        mockMvc.perform(get(BASE + "/U.PDS/search")).andExpect(status().isBadRequest());
    }

    @Test
    void alias_returnsTarget() throws Exception {
        when(service.resolveAlias("MY.ALIAS")).thenReturn("REAL.DS");
        mockMvc.perform(get(BASE + "/MY.ALIAS/alias")).andExpect(status().isOk())
                .andExpect(jsonPath("$.targetDsn").value("REAL.DS"));
    }

    @Test
    void alias_unresolvedIs404() throws Exception {
        when(service.resolveAlias(any())).thenThrow(new ZoRpcException("resolveDsAlias", ZoRpcException.NOT_FOUND, "nf", null));
        mockMvc.perform(get(BASE + "/MY.ALIAS/alias")).andExpect(status().isNotFound());
    }

    @Test
    void deleteVsam_returns204AndPassesFlags() throws Exception {
        mockMvc.perform(delete(BASE + "/U.VSAM/vsam").param("erase", "false").param("purge", "true"))
                .andExpect(status().isNoContent()).andExpect(content().string(""));
        verify(service).deleteVsam("U.VSAM", false, true);
        mockMvc.perform(delete(BASE + "/U.VSAM2/vsam")).andExpect(status().isNoContent());
        verify(service).deleteVsam("U.VSAM2", null, null);
    }

    @Test
    void invalidInputIs400() throws Exception {
        doThrow(new IllegalArgumentException("bad")).when(service).deleteVsam(any(), any(), any());
        mockMvc.perform(delete(BASE + "/bad/vsam")).andExpect(status().isBadRequest());
    }
}
