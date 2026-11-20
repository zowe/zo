package org.zowe.zo.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.zowe.zo.ffm.ZoRpcClient;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class SystemServiceTest {

    @Mock
    ZoRpcClient rpc;

    SystemService service;
    final JsonNode result = new ObjectMapper().createObjectNode().put("ok", true);

    @BeforeEach
    void setup() throws Exception {
        service = new SystemService(rpc);
        lenient().when(rpc.call(any(), any())).thenReturn(result);
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> verifyCall(String method) throws Exception {
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq(method), params.capture());
        return (Map<String, Object>) params.getValue();
    }

    @Test
    void getInfo_sendsEmptyParams() throws Exception {
        assertSame(result, service.getInfo());
        assertEquals(Map.of(), verifyCall("getInfo"));
    }

    @Test
    void listMethods_sendEmptyParams() throws Exception {
        service.listParmlib();
        service.listProclib();
        service.listApf();
        service.listLinklist();
        for (String method : new String[]{"listParmlib", "listProclib", "listApf", "listLinklist"}) {
            assertEquals(Map.of(), verifyCall(method));
        }
    }

    @Test
    void viewSyslog_defaults_sendEmptyParams() throws Exception {
        service.viewSyslog(null, null, null, null);
        assertEquals(Map.of(), verifyCall("viewSyslog"));
    }

    @Test
    void viewSyslog_absolute_sendsDateTimeAndMaxLines() throws Exception {
        service.viewSyslog("2026-03-13", "10:41:00", null, 100);
        assertEquals(Map.of("date", "2026-03-13", "time", "10:41:00", "maxLines", 100), verifyCall("viewSyslog"));
    }

    @Test
    void viewSyslog_relative_sendsSecondsAgo() throws Exception {
        service.viewSyslog(null, null, 300, null);
        assertEquals(Map.of("secondsAgo", 300), verifyCall("viewSyslog"));
    }

    @Test
    void viewSyslog_rejectsBadInput() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, null, 0, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, null, 86401, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, null, null, 0));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, null, null, 10001));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog("2026-02-30", null, null, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog("13/03/2026", null, null, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, "25:00:00", null, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, "10:41:00.50", null, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog("2026-03-13", null, 30, null));
        assertThrows(IllegalArgumentException.class, () -> service.viewSyslog(null, "10:00:00", 30, null));
        verifyNoInteractions(rpc);
    }
}
