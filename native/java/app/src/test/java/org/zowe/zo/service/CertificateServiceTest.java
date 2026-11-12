package org.zowe.zo.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.HttpStatus;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.CertificateRequests.*;

import java.util.LinkedHashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class CertificateServiceTest {

    static final ObjectMapper MAPPER = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    CertificateService service;
    final JsonNode result = MAPPER.createObjectNode().put("ok", true);

    @BeforeEach
    void setup() throws Exception {
        service = new CertificateService(rpc);
        lenient().when(rpc.call(any(), any())).thenReturn(result);
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> sent(String method) throws Exception {
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq(method), params.capture());
        return (Map<String, Object>) params.getValue();
    }

    private static Map<String, Object> map(Object... kv) {
        Map<String, Object> m = new LinkedHashMap<>();
        for (int i = 0; i < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]);
        return m;
    }

    @Test
    void listRings_allAndNarrowed() throws Exception {
        service.listRings("USER01", null);
        service.listRings("USER01", "RING1");
        verify(rpc).call("listRings", map("owner", "USER01"));
        verify(rpc).call("listRings", map("owner", "USER01", "keyring", "RING1"));
    }

    @Test
    void countRing_allowsVirtualRing() throws Exception {
        service.countRing("USER01", "*");
        assertEquals(map("owner", "USER01", "keyring", "*"), sent("countRing"));
    }

    @Test
    void listCertificates_sendsOnlyProvidedFilters() throws Exception {
        service.listCertificates("USER01", "RING1", new CertificateQuery("my cert", "personal", true, null, 0));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "my cert", "usage", "PERSONAL",
                "labelOnly", true, "maxEntries", 0), sent("listCertificates"));
    }

    @Test
    void showCertificate_sendsOwnerRingLabel() throws Exception {
        service.showCertificate("USER01", "RING1", "LBL");
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL"), sent("showCertificate"));
    }

    @Test
    void exportPem_sendsNoPassword() throws Exception {
        service.exportCertificate("USER01", "RING1", "LBL", new ExportRequest(null, null, null, null));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "format", "pem"),
                sent("exportCertificate"));
    }

    @Test
    void exportP12_sendsPasswordAndUppercasedDsn() throws Exception {
        service.exportCertificate("USER01", "RING1", "LBL",
                new ExportRequest("P12", null, "my.cert.p12(mem1)", "correct-horse-battery"));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "format", "p12",
                "password", "correct-horse-battery", "dsn", "MY.CERT.P12(MEM1)"), sent("exportCertificate"));
    }

    @Test
    void export_rejectsInvalidCombinations() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", "/tmp/a.p12", null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", null, null, "longenough1")));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", "/tmp/a.p12", null, "short")));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", "/tmp/a.p12", "A.B", "longenough1")));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("pem", null, null, "longenough1")));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("der", null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("pem", "relative/path", null, null)));
        verifyNoInteractions(rpc);
    }

    @Test
    void importCertificate_sendsAllFields() throws Exception {
        service.importCertificate(new ImportRequest("USER01", "RING1", "LBL", "personal", "/u/u1/a.p12", null, "pw", true));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "usage", "PERSONAL",
                "file", "/u/u1/a.p12", "password", "pw", "skipRefresh", true), sent("importCertificate"));
    }

    @Test
    void importCertificate_rejectsBadInput() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "*", "L", "PERSONAL", "/a", null, "pw", null)));
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "R", "L", "OTHER", "/a", null, "pw", null)));
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "R", "L", "PERSONAL", null, null, "pw", null)));
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "R", "L", "PERSONAL", "/a", "A.B", "pw", null)));
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "R", "L", "PERSONAL", "/a", null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.importCertificate(
                new ImportRequest("USER01", "R", "L", "PERSONAL", null, "bad dsn!", "pw", null)));
        verifyNoInteractions(rpc);
    }

    @Test
    void createAndDeleteKeyring_sendOwnerAndKeyring() throws Exception {
        service.createKeyring("USER01", "RING1");
        service.deleteKeyring("USER01", "RING1");
        verify(rpc).call("createKeyring", map("owner", "USER01", "keyring", "RING1"));
        verify(rpc).call("deleteKeyring", map("owner", "USER01", "keyring", "RING1"));
    }

    @Test
    void refreshDigtcert_sendsEmptyParams() throws Exception {
        service.refreshDigtcert();
        assertEquals(Map.of(), sent("refreshDigtcert"));
    }

    @Test
    void deleteCertificate_fromRing() throws Exception {
        service.deleteCertificate("USER01", "RING1", "LBL", true);
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "skipRefresh", true),
                sent("deleteCertificate"));
    }

    @Test
    void deleteCertificate_fromDatabase() throws Exception {
        service.deleteCertificate("USER01", null, "LBL", null);
        assertEquals(map("owner", "USER01", "database", true, "label", "LBL"), sent("deleteCertificate"));
    }

    @Test
    void connect_fromRingWithDefault() throws Exception {
        service.ringAction("USER01", "RING1", "LBL", new RingCertificateAction("connect", "RING0", null, "certauth", true));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "fromRing", "RING0",
                "usage", "CERTAUTH", "default", true), sent("connectCertificate"));
    }

    @Test
    void connect_fromDatabase() throws Exception {
        service.ringAction("USER01", "RING1", "LBL", new RingCertificateAction("connect", null, true, null, null));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL", "fromDatabase", true),
                sent("connectCertificate"));
    }

    @Test
    void connect_rejectsBadSources() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "R", "L",
                new RingCertificateAction("connect", null, null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "R", "L",
                new RingCertificateAction("connect", "R0", true, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "R", "L",
                new RingCertificateAction("connect", "*", null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "*", "L",
                new RingCertificateAction("connect", "R0", null, null, null)));
        verifyNoInteractions(rpc);
    }

    @Test
    void setDefault_sendsOwnerRingLabel() throws Exception {
        service.ringAction("USER01", "RING1", "LBL", new RingCertificateAction("set-default", null, null, null, null));
        assertEquals(map("owner", "USER01", "keyring", "RING1", "label", "LBL"), sent("setDefaultCertificate"));
    }

    @Test
    void ringAction_rejectsUnknownRequestAndVirtualRing() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "R", "L",
                new RingCertificateAction("rename", null, null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "R", "L",
                new RingCertificateAction(null, null, null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ringAction("U", "*", "L",
                new RingCertificateAction("set-default", null, null, null, null)));
    }

    @Test
    void rename_andTrust() throws Exception {
        service.ownerAction("USER01", "OLD", new OwnerCertificateAction("rename", "NEW", null));
        service.ownerAction("USER01", "OLD", new OwnerCertificateAction("trust", null, "hightrust"));
        verify(rpc).call("renameCertificate", map("owner", "USER01", "label", "OLD", "newLabel", "NEW"));
        verify(rpc).call("trustCertificate", map("owner", "USER01", "label", "OLD", "status", "HIGHTRUST"));
    }

    @Test
    void ownerAction_rejectsBadInput() throws Exception {
        assertThrows(IllegalArgumentException.class, () -> service.ownerAction("U", "L",
                new OwnerCertificateAction("rename", null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.ownerAction("U", "L",
                new OwnerCertificateAction("trust", null, "MAYBE")));
        assertThrows(IllegalArgumentException.class, () -> service.ownerAction("U", "L",
                new OwnerCertificateAction("connect", null, null)));
        verifyNoInteractions(rpc);
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "TOOLONG12", "BAD ID", "US'ER", "US\nER", "A;B", "A\"B"})
    void owner_rejectsInvalid(String owner) {
        assertThrows(IllegalArgumentException.class, () -> service.createKeyring(owner, "RING"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "*", "R'ING", "R\"ING", "R(ING)", "R\u0007ING", "R\nING", "R/ING", "   "})
    void ring_rejectsInvalidForWrites(String ring) {
        assertThrows(IllegalArgumentException.class, () -> service.createKeyring("USER01", ring));
    }

    @Test
    void ring_rejectsOverLengthInsteadOfTruncating() {
        assertThrows(IllegalArgumentException.class, () -> service.createKeyring("USER01", "R".repeat(237)));
        assertDoesNotThrow(() -> service.createKeyring("USER01", "R".repeat(236)));
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "  ", "L\u0000", "L\nX", "a/b", "a;b", "a%b", "a\\b"})
    void label_rejectsInvalid(String label) {
        assertThrows(IllegalArgumentException.class, () -> service.showCertificate("USER01", "RING", label));
    }

    @Test
    void label_rejectsOverLength() {
        assertThrows(IllegalArgumentException.class, () -> service.showCertificate("USER01", "RING", "L".repeat(33)));
        assertThrows(IllegalArgumentException.class,
                () -> service.ownerAction("USER01", "OLD", new OwnerCertificateAction("rename", "N".repeat(33), null)));
    }

    @Test
    void listCertificates_rejectsBadFilters() {
        assertThrows(IllegalArgumentException.class, () -> service.listCertificates("U", "R",
                new CertificateQuery(null, "BOGUS", null, null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.listCertificates("U", "R",
                new CertificateQuery(null, null, null, null, -1)));
    }

    @ParameterizedTest
    @ValueSource(strings = {"1BAD.NAME", "TOOLONGSEGMENT.X", "A..B", "A.B(TOOLONGMEM)", "A.B(1X)", "has space", "A.B'", ""})
    void dsn_rejectsInvalid(String dsn) {
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", null, dsn, "longenough1")));
    }

    @Test
    void dsn_rejectsOver44Characters() {
        String longName = "AAAAAAAA.BBBBBBBB.CCCCCCCC.DDDDDDDD.EEEEEEEE.F";
        assertEquals(46, longName.length());
        assertThrows(IllegalArgumentException.class, () -> service.exportCertificate("U", "R", "L",
                new ExportRequest("p12", null, longName, "longenough1")));
    }

    @Test
    void notFoundMessage_isMappedTo404WithSafReturns() throws Exception {
        JsonNode data = MAPPER.readTree("{\"safReturns\":{\"safReturnCode\":8}}");
        when(rpc.call(eq("showCertificate"), any()))
                .thenThrow(new ZoRpcException("showCertificate", 1, "Certificate 'X' not found in USER01/RING1", data));
        ZoCertificateException ex = assertThrows(ZoCertificateException.class,
                () -> service.showCertificate("USER01", "RING1", "X"));
        assertEquals(HttpStatus.NOT_FOUND, ex.getStatus());
        assertEquals(8, ex.getSafReturns().get("safReturnCode").asInt());
    }

    @Test
    void otherRpcErrors_propagateUnchanged() throws Exception {
        when(rpc.call(eq("createKeyring"), any()))
                .thenThrow(new ZoRpcException("createKeyring", 1, "boom", null));
        assertThrows(ZoRpcException.class, () -> service.createKeyring("USER01", "RING1"));
    }

    @Test
    void requestToString_hidesPasswords() {
        assertFalse(new ImportRequest("U", "R", "L", "PERSONAL", "/a", null, "s3cret!!", null).toString().contains("s3cret"));
        assertFalse(new ExportRequest("p12", "/a", null, "s3cret!!").toString().contains("s3cret"));
    }
}
