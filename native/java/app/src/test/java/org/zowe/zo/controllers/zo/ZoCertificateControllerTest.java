package org.zowe.zo.controllers.zo;

import com.fasterxml.jackson.databind.JsonNode;
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
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.CertificateService;

import java.util.LinkedHashMap;
import java.util.Map;

import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.containsString;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

@ExtendWith(MockitoExtension.class)
class ZoCertificateControllerTest {

    private static final String API = "/zo/api/v1";
    private static final String CERT = API + "/keyrings/USER01/RING1/certificates/MyCert";
    private static final ObjectMapper MAPPER = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    MockMvc mockMvc;
    final JsonNode ok = MAPPER.createObjectNode().put("success", true);

    @BeforeEach
    void setup() throws Exception {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZoCertificateController(new CertificateService(rpc)))
                // global handler first on purpose: the zo advice must win via @Order, not position
                .setControllerAdvice(new GlobalExceptionHandler(), new ZoExceptionAdvice())
                .build();
        lenient().when(rpc.call(any(), any())).thenReturn(ok);
    }

    private static Map<String, Object> map(Object... kv) {
        Map<String, Object> m = new LinkedHashMap<>();
        for (int i = 0; i < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]);
        return m;
    }

    @Test
    void listRings_andGetRing() throws Exception {
        mockMvc.perform(get(API + "/keyrings/USER01")).andExpect(status().isOk()).andExpect(jsonPath("$.success").value(true));
        mockMvc.perform(get(API + "/keyrings/USER01/RING1")).andExpect(status().isOk());
        verify(rpc).call("listRings", map("owner", "USER01"));
        verify(rpc).call("listRings", map("owner", "USER01", "keyring", "RING1"));
    }

    @Test
    void countRing_acceptsVirtualRing() throws Exception {
        mockMvc.perform(get(API + "/keyrings/USER01/*/count")).andExpect(status().isOk());
        verify(rpc).call("countRing", map("owner", "USER01", "keyring", "*"));
    }

    @Test
    void createKeyring_returns201() throws Exception {
        mockMvc.perform(post(API + "/keyrings/USER01/RING1")).andExpect(status().isCreated());
        verify(rpc).call("createKeyring", map("owner", "USER01", "keyring", "RING1"));
    }

    @Test
    void deleteKeyring_returns204() throws Exception {
        mockMvc.perform(delete(API + "/keyrings/USER01/RING1")).andExpect(status().isNoContent());
        verify(rpc).call("deleteKeyring", map("owner", "USER01", "keyring", "RING1"));
    }

    @Test
    void refresh_callsRefreshDigtcert() throws Exception {
        mockMvc.perform(post(API + "/keyrings/refresh")).andExpect(status().isOk());
        verify(rpc).call("refreshDigtcert", Map.of());
    }

    @Test
    void listCertificates_passesFilters() throws Exception {
        mockMvc.perform(get(API + "/keyrings/USER01/RING1/certificates")
                       .param("usage", "personal").param("maxEntries", "0").param("labelOnly", "true"))
               .andExpect(status().isOk());
        verify(rpc).call("listCertificates",
                map("owner", "USER01", "keyring", "RING1", "usage", "PERSONAL", "labelOnly", true, "maxEntries", 0));
    }

    @Test
    void showCertificate_decodesLabel() throws Exception {
        mockMvc.perform(get(API + "/keyrings/USER01/RING1/certificates/My Cert")).andExpect(status().isOk());
        verify(rpc).call("showCertificate", map("owner", "USER01", "keyring", "RING1", "label", "My Cert"));
    }

    @Test
    void showCertificate_notFoundIs404WithSafReturns() throws Exception {
        JsonNode data = MAPPER.readTree("{\"safReturns\":{\"safReturnCode\":8,\"esmReturnCode\":8}}");
        when(rpc.call(eq("showCertificate"), any()))
                .thenThrow(new ZoRpcException("showCertificate", 1, "Certificate 'MyCert' not found in USER01/RING1", data));
        mockMvc.perform(get(CERT))
               .andExpect(status().isNotFound())
               .andExpect(jsonPath("$.rc").value(1))
               .andExpect(jsonPath("$.safReturns.safReturnCode").value(8));
    }

    @Test
    void otherRpcFailureFallsToGlobalHandler() throws Exception {
        when(rpc.call(eq("createKeyring"), any())).thenThrow(new ZoRpcException("createKeyring", 8, "failed", null));
        mockMvc.perform(post(API + "/keyrings/USER01/RING1"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.rc").value(8));
    }

    @Test
    void exportGet_pem_isNoStore() throws Exception {
        mockMvc.perform(get(CERT + "/export"))
               .andExpect(status().isOk())
               .andExpect(header().string("Cache-Control", "no-store"));
        verify(rpc).call("exportCertificate",
                map("owner", "USER01", "keyring", "RING1", "label", "MyCert", "format", "pem"));
    }

    @Test
    void exportGet_p12_isRejected() throws Exception {
        mockMvc.perform(get(CERT + "/export").param("format", "p12")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void exportPost_p12_sendsPasswordFromBodyAndIsNoStore() throws Exception {
        mockMvc.perform(post(CERT + "/export").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"format\":\"p12\",\"file\":\"/tmp/c.p12\",\"password\":\"correct-horse-1\"}"))
               .andExpect(status().isOk())
               .andExpect(header().string("Cache-Control", "no-store"));
        verify(rpc).call("exportCertificate", map("owner", "USER01", "keyring", "RING1", "label", "MyCert",
                "format", "p12", "password", "correct-horse-1", "file", "/tmp/c.p12"));
    }

    @Test
    void exportPost_p12_withoutPassword_is400() throws Exception {
        mockMvc.perform(post(CERT + "/export").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"format\":\"p12\",\"file\":\"/tmp/c.p12\"}"))
               .andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void import_returns201() throws Exception {
        mockMvc.perform(post(API + "/certificates/import").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"owner\":\"USER01\",\"keyring\":\"RING1\",\"label\":\"L\",\"usage\":\"PERSONAL\","
                               + "\"dsn\":\"user01.cert(p12)\",\"password\":\"pw\"}"))
               .andExpect(status().isCreated())
               .andExpect(content().string(not(containsString("pw\""))));
        verify(rpc).call("importCertificate", map("owner", "USER01", "keyring", "RING1", "label", "L",
                "usage", "PERSONAL", "dsn", "USER01.CERT(P12)", "password", "pw"));
    }

    @Test
    void import_missingFields_is400() throws Exception {
        mockMvc.perform(post(API + "/certificates/import").contentType(MediaType.APPLICATION_JSON).content("{}"))
               .andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void malformedBody_is400WithoutEchoingContent() throws Exception {
        mockMvc.perform(post(API + "/certificates/import").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"password\": hunter2secret"))
               .andExpect(status().isBadRequest())
               .andExpect(content().string(not(containsString("hunter2secret"))));
        verifyNoInteractions(rpc);
    }

    @Test
    void wrongContentType_is415() throws Exception {
        mockMvc.perform(post(API + "/certificates/import").contentType(MediaType.TEXT_PLAIN).content("x"))
               .andExpect(status().isUnsupportedMediaType());
    }

    @Test
    void put_connect_andSetDefault() throws Exception {
        mockMvc.perform(put(CERT).contentType(MediaType.APPLICATION_JSON)
                       .content("{\"request\":\"connect\",\"fromDatabase\":true,\"default\":true}"))
               .andExpect(status().isOk());
        mockMvc.perform(put(CERT).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"set-default\"}"))
               .andExpect(status().isOk());
        verify(rpc).call("connectCertificate", map("owner", "USER01", "keyring", "RING1", "label", "MyCert",
                "fromDatabase", true, "default", true));
        verify(rpc).call("setDefaultCertificate", map("owner", "USER01", "keyring", "RING1", "label", "MyCert"));
    }

    @Test
    void put_rename_andTrust() throws Exception {
        mockMvc.perform(put(API + "/certificates/USER01/MyCert").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"request\":\"rename\",\"newLabel\":\"New\"}"))
               .andExpect(status().isOk());
        mockMvc.perform(put(API + "/certificates/USER01/MyCert").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"request\":\"trust\",\"status\":\"NOTRUST\"}"))
               .andExpect(status().isOk());
        verify(rpc).call("renameCertificate", map("owner", "USER01", "label", "MyCert", "newLabel", "New"));
        verify(rpc).call("trustCertificate", map("owner", "USER01", "label", "MyCert", "status", "NOTRUST"));
    }

    @Test
    void put_unknownRequest_is400() throws Exception {
        mockMvc.perform(put(CERT).contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"rename\"}"))
               .andExpect(status().isBadRequest());
        mockMvc.perform(put(API + "/certificates/USER01/MyCert").contentType(MediaType.APPLICATION_JSON)
                       .content("{\"request\":\"connect\"}"))
               .andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void deleteCertificate_fromRingAndDatabase() throws Exception {
        mockMvc.perform(delete(CERT).param("skipRefresh", "true")).andExpect(status().isNoContent());
        mockMvc.perform(delete(API + "/certificates/USER01/MyCert")).andExpect(status().isNoContent());
        verify(rpc).call("deleteCertificate",
                map("owner", "USER01", "keyring", "RING1", "label", "MyCert", "skipRefresh", true));
        verify(rpc).call("deleteCertificate", map("owner", "USER01", "database", true, "label", "MyCert"));
    }

    @Test
    void validation_rejectsBadPathValues() throws Exception {
        mockMvc.perform(get(API + "/keyrings/TOOLONG12/RING1")).andExpect(status().isBadRequest());
        mockMvc.perform(get(API + "/keyrings/USER01/R'ING")).andExpect(status().isBadRequest());
        mockMvc.perform(get(API + "/keyrings/USER01/R(ING)")).andExpect(status().isBadRequest());
        mockMvc.perform(post(API + "/keyrings/USER01/*")).andExpect(status().isBadRequest());
        mockMvc.perform(get(API + "/keyrings/USER01/RING1/certificates/" + "L".repeat(33))).andExpect(status().isBadRequest());
        mockMvc.perform(get(API + "/keyrings/USER01/RING1/certificates").param("usage", "BOGUS")).andExpect(status().isBadRequest());
        mockMvc.perform(get(API + "/keyrings/USER01/RING1/certificates").param("maxEntries", "x")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }
}
