package org.zowe.zo.controllers.zo;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.SystemService;

import java.util.Map;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@ExtendWith(MockitoExtension.class)
class ZoSystemControllerTest {

    private static final String BASE = "/zo/api/v1/system";
    private static final ObjectMapper MAPPER = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZoSystemController(new SystemService(rpc)))
                .setControllerAdvice(new GlobalExceptionHandler(), new ZoExceptionAdvice())
                .build();
    }

    @Test
    void info_returnsRpcResult() throws Exception {
        when(rpc.call("getInfo", Map.of())).thenReturn(MAPPER.createObjectNode().put("version", "1.2.3"));
        mockMvc.perform(get(BASE + "/info"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.version").value("1.2.3"));
    }

    @Test
    void syslog_passesQueryParams() throws Exception {
        when(rpc.call("viewSyslog", Map.of("date", "2026-03-13", "time", "10:41:00", "maxLines", 5)))
                .thenReturn(MAPPER.createObjectNode().put("returnedLines", 5));
        mockMvc.perform(get(BASE + "/syslog").param("date", "2026-03-13").param("time", "10:41:00").param("maxLines", "5"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedLines").value(5));
    }

    @Test
    void syslog_rejectsConflictingAndOutOfRangeValues() throws Exception {
        mockMvc.perform(get(BASE + "/syslog").param("date", "2026-03-13").param("secondsAgo", "30"))
               .andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/syslog").param("maxLines", "10001"))
               .andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/syslog").param("secondsAgo", "0"))
               .andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/syslog").param("maxLines", "abc"))
               .andExpect(status().isBadRequest())
               .andExpect(jsonPath("$.message").value("Invalid value for parameter 'maxLines'"));
        verifyNoInteractions(rpc);
    }

    @Test
    void lists_returnRpcResults() throws Exception {
        for (String[] pair : new String[][]{{"parmlib", "listParmlib"}, {"proclib", "listProclib"},
                {"apf", "listApf"}, {"linklist", "listLinklist"}}) {
            when(rpc.call(pair[1], Map.of())).thenReturn(MAPPER.createObjectNode().put("returnedRows", 0));
            mockMvc.perform(get(BASE + "/" + pair[0]))
                   .andExpect(status().isOk())
                   .andExpect(jsonPath("$.returnedRows").value(0));
        }
    }

    @Test
    void rpcFailure_isMappedByGlobalHandler() throws Exception {
        when(rpc.call(any(), any())).thenThrow(new ZoRpcException("listApf", 1, "could not list apf", null));
        mockMvc.perform(get(BASE + "/apf"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.rc").value(1))
               .andExpect(jsonPath("$.message").value("could not list apf"));
    }
}
