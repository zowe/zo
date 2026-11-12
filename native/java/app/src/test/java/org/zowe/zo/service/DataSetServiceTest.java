package org.zowe.zo.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.DataSetService.CopySource;
import org.zowe.zo.service.DataSetService.CreateSpec;
import org.zowe.zo.service.DataSetService.DataSetInfo;
import org.zowe.zo.service.DataSetService.ListPage;
import org.zowe.zo.service.DataSetService.ListQuery;
import org.zowe.zo.service.DataSetService.MemberInfo;
import org.zowe.zo.service.DataSetService.Target;

import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@ExtendWith(MockitoExtension.class)
class DataSetServiceTest {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    @Mock
    ZoContentIo contentIo;

    DataSetService service;

    @BeforeEach
    void setup() {
        service = new DataSetService(rpc, contentIo);
    }

    private static JsonNode json(String text) {
        try {
            return MAPPER.readTree(text);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private static final ListQuery Q1000 = new ListQuery(null, null, 1000, false, false);

    @SuppressWarnings("unchecked")
    private Map<String, Object> capturedParams(String method) throws Exception {
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq(method), params.capture());
        return (Map<String, Object>) params.getValue();
    }

    private static ZoRpcException failure(String method) {
        return new ZoRpcException(method, 8, "failed", null);
    }

    private static String items(String... names) {
        StringBuilder sb = new StringBuilder("{\"items\":[");
        for (int i = 0; i < names.length; i++) {
            sb.append(i > 0 ? "," : "").append("{\"name\":\"").append(names[i]).append("\"}");
        }
        return sb.append("],\"returnedRows\":").append(names.length).append("}").toString();
    }

    // ----------------------------------------------------------------- list

    @Test
    void listDataSets_sendsPatternWithoutExactMatchAndLimitPlusOne() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("USER.A")));

        ListPage<DataSetInfo> page = service.listDataSets("user.*", null, Q1000);

        assertEquals(Map.of("pattern", "USER.*", "maxItems", 1001), capturedParams("listDatasets"));
        assertEquals("USER.A", page.items().get(0).name());
        assertFalse(page.moreRows());
    }

    @Test
    void listDataSets_attributesAndMoreRows() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(
                "{\"items\":[{\"name\":\"A.B\",\"alloc\":15,\"allocx\":1,\"blksize\":80,\"cdate\":\"2020/01/01\","
                        + "\"devtype\":\"3390\",\"dsorg\":\"PO\",\"lrecl\":80,\"migrated\":false,\"multivolume\":false,"
                        + "\"recfm\":\"FB\",\"usedp\":3,\"volser\":\"V1\",\"volsers\":[\"V1\"]},"
                        + "{\"name\":\"A.C\"},{\"name\":\"A.D\"}]}"));

        ListPage<DataSetInfo> page = service.listDataSets("A", null, new ListQuery(null, null, 2, true, false));

        assertEquals(Map.of("pattern", "A", "attributes", true, "maxItems", 3), capturedParams("listDatasets"));
        assertEquals(2, page.items().size());
        assertTrue(page.moreRows());
        assertNull(page.totalRows());
        DataSetInfo first = page.items().get(0);
        assertEquals(15L, first.alloc());
        assertEquals("PO", first.dsorg());
        assertEquals(List.of("V1"), first.volsers());
    }

    @Test
    void listDataSets_unlimitedAndTotalFetchTheWholeListing() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("A.B", "A.C", "A.D")));

        ListPage<DataSetInfo> unlimited = service.listDataSets("A", null, new ListQuery(null, null, 0, false, false));
        assertEquals(3, unlimited.items().size());
        assertEquals(Map.of("pattern", "A", "maxItems", DataSetService.MAX_FETCH), capturedParams("listDatasets"));

        ListPage<DataSetInfo> counted = service.listDataSets("A", null, new ListQuery(null, null, 1, false, true));
        assertEquals(1, counted.items().size());
        assertEquals(3, counted.totalRows());
        assertTrue(counted.moreRows());
    }

    @Test
    void listDataSets_startIsAppliedClientSideInclusive() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("A.D", "A.B", "A.C")));

        ListPage<DataSetInfo> page = service.listDataSets("A", null, new ListQuery(null, "a.c", 1000, false, false));

        assertEquals(List.of("A.C", "A.D"), page.items().stream().map(DataSetInfo::name).toList());
        assertEquals(Map.of("pattern", "A", "maxItems", DataSetService.MAX_FETCH), capturedParams("listDatasets"));
    }

    @Test
    void listDataSets_volserFiltersOnVolumesAndRequestsAttributes() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(
                "{\"items\":[{\"name\":\"A.1\",\"volser\":\"V1\",\"volsers\":[\"V1\"]},"
                        + "{\"name\":\"A.2\",\"volser\":\"V2\",\"volsers\":[\"V2\",\"V3\"]}]}"));

        ListPage<DataSetInfo> page = service.listDataSets("A", "v3", Q1000);

        assertEquals(List.of("A.2"), page.items().stream().map(DataSetInfo::name).toList());
        assertEquals(Map.of("pattern", "A", "attributes", true, "maxItems", DataSetService.MAX_FETCH),
                capturedParams("listDatasets"));
    }

    @Test
    void listDataSets_rejectsBadInput() {
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A B", null, Q1000));
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A\u0000", null, Q1000));
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A..B", null, Q1000));
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A", "TOOLONG1", Q1000));
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A", "V-1", Q1000));
        assertThrows(IllegalArgumentException.class, () -> service.listDataSets("A", null,
                new ListQuery(null, "A.*", 10, false, false)));
        org.mockito.Mockito.verifyNoInteractions(rpc);
    }

    @Test
    void listMembers_sendsDsnamePatternAttributesAndTranslatesWildcard() throws Exception {
        when(rpc.call(eq("listDsMembers"), any())).thenReturn(json(
                "{\"items\":[{\"name\":\"M1\",\"vers\":1,\"mod\":2,\"user\":\"U\",\"sclm\":\"N\"},{\"name\":\"M2\"}]}"));

        ListPage<MemberInfo> page = service.listMembers("user.pds", "m%*", new ListQuery(null, null, 5, true, false));

        assertEquals(Map.of("dsname", "USER.PDS", "pattern", "M?*", "attributes", true, "maxItems", 6),
                capturedParams("listDsMembers"));
        assertEquals(2, page.items().size());
        assertEquals(1, page.items().get(0).vers());
    }

    @Test
    void listMembers_startAndPaging() throws Exception {
        when(rpc.call(eq("listDsMembers"), any())).thenReturn(json(items("M3", "M1", "M2")));

        ListPage<MemberInfo> page = service.listMembers("U.P", null, new ListQuery(null, "m2", 1, false, true));

        assertEquals(List.of("M2"), page.items().stream().map(MemberInfo::name).toList());
        assertTrue(page.moreRows());
        assertEquals(2, page.totalRows());
    }

    @Test
    void listMembers_missingDataSetBecomesNotFound() throws Exception {
        when(rpc.call(eq("listDsMembers"), any())).thenThrow(failure("listDsMembers"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items()));

        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.listMembers("NO.PDS", null, Q1000));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    // ----------------------------------------------------------------- content

    @Test
    void read_usesContentIoWithFullNameAndVolume() throws Exception {
        when(contentIo.readDataset(any(), any())).thenReturn(new ZoContentIo.Content(new byte[]{1}, "e"));

        service.read(Target.of("user.pds", "mem", "vol001"), new ZoContentIo.Options("IBM-037", null, null));

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(contentIo).readDataset(eq("USER.PDS(MEM)"), options.capture());
        assertEquals(new ZoContentIo.Options("IBM-037", null, "VOL001"), options.getValue());
        verify(rpc, never()).call(any(), any());
    }

    @Test
    void read_unwrapsZoRpcExceptionWrappedByTheSecurityAspect() throws Exception {
        when(contentIo.readDataset(any(), any()))
                .thenThrow(new RuntimeException(new RuntimeException(
                        new ZoRpcException("readDataset", ZoRpcException.ETAG_MISMATCH, "x", null))));

        ZoRpcException e = assertThrows(ZoRpcException.class,
                () -> service.read(Target.of("U.D", null, null), ZoContentIo.Options.DEFAULT));
        assertEquals(ZoRpcException.ETAG_MISMATCH, e.getCode());
    }

    @Test
    void read_nonZoRuntimeExceptionPropagates() throws Exception {
        when(contentIo.readDataset(any(), any())).thenThrow(new IllegalStateException("boom"));
        assertThrows(IllegalStateException.class,
                () -> service.read(Target.of("U.D", null, null), ZoContentIo.Options.DEFAULT));
    }

    @Test
    void read_missingMemberBecomesNotFound() throws Exception {
        when(contentIo.readDataset(any(), any())).thenThrow(failure("readDataset"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("U.PDS")));
        when(rpc.call(eq("listDsMembers"), any())).thenReturn(json(items()));

        ZoRpcException e = assertThrows(ZoRpcException.class,
                () -> service.read(Target.of("U.PDS", "NOPE", null), ZoContentIo.Options.DEFAULT));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void read_failureOnExistingTargetKeepsOriginalError() throws Exception {
        when(contentIo.readDataset(any(), any())).thenThrow(failure("readDataset"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("U.D")));

        ZoRpcException e = assertThrows(ZoRpcException.class,
                () -> service.read(Target.of("U.D", null, null), ZoContentIo.Options.DEFAULT));
        assertEquals(8, e.getCode());
    }

    @Test
    void write_passesBytesOptionsAndEtagAndReturnsResult() throws Exception {
        byte[] data = {1, 2};
        when(contentIo.writeDataset(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("n", "w", true));

        ZoContentIo.WriteResult result = service.write(Target.of("U.PDS", "M", null), data,
                new ZoContentIo.Options("binary", null, null), "old");

        verify(contentIo).writeDataset("U.PDS(M)", data, new ZoContentIo.Options("binary", null, null), "old");
        assertTrue(result.created());
        assertEquals("w", result.truncationWarning());
    }

    @Test
    void write_rejectsBadEncodings() {
        assertThrows(IllegalArgumentException.class, () -> service.write(Target.of("U.D", null, null), new byte[0],
                new ZoContentIo.Options("IBM 1047", null, null), null));
        assertThrows(IllegalArgumentException.class, () -> service.write(Target.of("U.D", null, null), new byte[0],
                new ZoContentIo.Options(null, "UTF-8;x", null), null));
    }

    @Test
    void write_toMissingDataSetBecomesNotFound() throws Exception {
        when(contentIo.writeDataset(any(), any(), any(), any())).thenThrow(failure("writeDataset"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items()));

        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.write(Target.of("NO.DS", null, null),
                new byte[0], ZoContentIo.Options.DEFAULT, null));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void write_etagMismatchIsNotMaskedByExistenceCheck() throws Exception {
        when(contentIo.writeDataset(any(), any(), any(), any()))
                .thenThrow(new ZoRpcException("writeDataset", ZoRpcException.ETAG_MISMATCH, "m", null));

        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.write(Target.of("U.D", null, null),
                new byte[0], ZoContentIo.Options.DEFAULT, "x"));
        assertEquals(ZoRpcException.ETAG_MISMATCH, e.getCode());
        verify(rpc, never()).call(any(), any());
    }

    // ----------------------------------------------------------------- mutations

    @Test
    void createMember_sendsCombinedNameAndOverwrite() throws Exception {
        when(rpc.call(eq("createMember"), any())).thenReturn(json("{\"success\":true}"));

        service.createMember("u.pds", "mem", true);

        assertEquals(Map.of("dsname", "U.PDS(MEM)", "overwrite", true), capturedParams("createMember"));
    }

    @Test
    void createMember_existingWithoutOverwriteIsConflict() throws Exception {
        when(rpc.call(eq("createMember"), any())).thenThrow(failure("createMember"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("U.PDS")));
        when(rpc.call(eq("listDsMembers"), any())).thenReturn(json(items("MEM")));

        assertThrows(DataSetConflictException.class, () -> service.createMember("U.PDS", "MEM", false));
    }

    @Test
    void createMember_missingDataSetIsNotFound() throws Exception {
        when(rpc.call(eq("createMember"), any())).thenThrow(failure("createMember"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items()));

        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.createMember("U.PDS", "MEM", false));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void createMember_validatesNames() {
        assertThrows(IllegalArgumentException.class, () -> service.createMember("U.PDS", "1MEM", false));
        assertThrows(IllegalArgumentException.class, () -> service.createMember("U.PDS", "TOOLONGMEM", false));
        assertThrows(IllegalArgumentException.class, () -> service.createMember("U.PDS", "A(B)", false));
        assertThrows(IllegalArgumentException.class, () -> service.createMember("U.PDS", "A\nB", false));
    }

    @Test
    void delete_sendsFullNameAndMapsMissingToNotFound() throws Exception {
        when(rpc.call(eq("deleteDataset"), any())).thenReturn(json("{\"success\":true}"));
        service.delete(Target.of("u.pds", "m", null));
        assertEquals(Map.of("dsname", "U.PDS(M)"), capturedParams("deleteDataset"));
    }

    @Test
    void delete_missingDataSetIsNotFound() throws Exception {
        when(rpc.call(eq("deleteDataset"), any())).thenThrow(failure("deleteDataset"));
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items()));

        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.delete(Target.of("NO.DS", null, null)));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void existenceCheckSendsExactMatchWithOneItem() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items("U.DS")));
        assertTrue(service.dataSetExists("U.DS"));
        assertEquals(Map.of("pattern", "U.DS", "exactMatch", true, "maxItems", 1), capturedParams("listDatasets"));
    }

    @Test
    void deleteVsam_sendsOnlyProvidedFlags() throws Exception {
        when(rpc.call(eq("deleteVsam"), any())).thenReturn(json("{}"));
        service.deleteVsam("u.vsam", null, true);
        assertEquals(Map.of("dsname", "U.VSAM", "purge", true), capturedParams("deleteVsam"));
    }

    @Test
    void renameDataSet_sendsBeforeAndAfter() throws Exception {
        when(rpc.call(eq("renameDataset"), any())).thenReturn(json("{}"));
        service.renameDataSet("u.old", "u.new");
        assertEquals(Map.of("dsnameBefore", "U.OLD", "dsnameAfter", "U.NEW"), capturedParams("renameDataset"));
    }

    @Test
    void renameMember_sendsMemberBeforeAndAfter() throws Exception {
        when(rpc.call(eq("renameMember"), any())).thenReturn(json("{}"));
        service.renameMember("u.pds", "a", "b");
        assertEquals(Map.of("dsname", "U.PDS", "memberBefore", "A", "memberAfter", "B"),
                capturedParams("renameMember"));
    }

    @Test
    void copy_sendsSourceTargetAndReplace() throws Exception {
        when(rpc.call(eq("copyDatasetOrMember"), any())).thenReturn(json("{}"));
        service.copy(new CopySource("u.from", "a"), Target.of("u.to", "b", null), true);
        assertEquals(Map.of("source", "U.FROM(A)", "target", "U.TO(B)", "replace", true),
                capturedParams("copyDatasetOrMember"));
    }

    @Test
    void copy_dataSetWithoutReplaceOmitsFlag() throws Exception {
        when(rpc.call(eq("copyDatasetOrMember"), any())).thenReturn(json("{}"));
        service.copy(new CopySource("u.from", null), Target.of("u.to", null, null), false);
        assertEquals(Map.of("source", "U.FROM", "target", "U.TO"), capturedParams("copyDatasetOrMember"));
    }

    @Test
    void recall_sendsRestoreDataset() throws Exception {
        when(rpc.call(eq("restoreDataset"), any())).thenReturn(json("{}"));
        service.recall("u.migr");
        assertEquals(Map.of("dsname", "U.MIGR"), capturedParams("restoreDataset"));
    }

    @Test
    void resolveAlias_returnsTargetOrNotFound() throws Exception {
        when(rpc.call(eq("resolveDsAlias"), any())).thenReturn(json("{\"targetDsn\":\"REAL.DS\"}"));
        assertEquals("REAL.DS", service.resolveAlias("my.alias"));
        assertEquals(Map.of("dsname", "MY.ALIAS"), capturedParams("resolveDsAlias"));
    }

    @Test
    void resolveAlias_emptyTargetIsNotFound() throws Exception {
        when(rpc.call(eq("resolveDsAlias"), any())).thenReturn(json("{\"targetDsn\":\"\"}"));
        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.resolveAlias("my.alias"));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void search_sendsParamsAndValidatesThem() throws Exception {
        when(rpc.call(eq("toolSearch"), any())).thenReturn(json("{\"data\":\"hits\"}"));

        assertEquals("hits", service.search("u.pds", "needle", "ANYC,SEARCH"));
        assertEquals(Map.of("dsname", "U.PDS", "string", "needle", "parms", "ANYC,SEARCH"), capturedParams("toolSearch"));

        assertThrows(IllegalArgumentException.class, () -> service.search("U.PDS", "", null));
        assertThrows(IllegalArgumentException.class, () -> service.search("U.PDS", "a\u0000b", null));
        assertThrows(IllegalArgumentException.class, () -> service.search("U.PDS", "x", "A;B"));
        verify(rpc, times(1)).call(eq("toolSearch"), anyMap());
    }

    // ----------------------------------------------------------------- create

    @SuppressWarnings("unchecked")
    private Map<String, Object> createAttributes() throws Exception {
        return (Map<String, Object>) capturedParams("createDataset").get("attributes");
    }

    private static CreateSpec spec(String dsorg, String alcunit, Integer primary, String recfm, Integer lrecl) {
        return new CreateSpec(null, null, dsorg, alcunit, primary, null, null, null, recfm, null, lrecl, null, null,
                null, null, null);
    }

    @Test
    void createDataSet_mapsEveryAttribute() throws Exception {
        when(rpc.call(eq("createDataset"), any())).thenReturn(json("{}"));

        service.createDataSet("u.new", new CreateSpec("vol001", "3390", "po", "cyl", 10, 5, 20, 500, "fb", 27920, 80,
                "sc1", "mc1", "dc1", "library", null));

        Map<String, Object> params = capturedParams("createDataset");
        assertEquals("U.NEW", params.get("dsname"));
        assertEquals(Map.ofEntries(Map.entry("alcunit", "CYL"), Map.entry("blksize", 27920),
                Map.entry("dirblk", 20), Map.entry("dsorg", "PO"), Map.entry("primary", 10), Map.entry("recfm", "FB"),
                Map.entry("lrecl", 80), Map.entry("dataclass", "DC1"), Map.entry("unit", "3390"),
                Map.entry("dsntype", "LIBRARY"), Map.entry("mgntclass", "MC1"), Map.entry("avgblk", 500),
                Map.entry("secondary", 5), Map.entry("storclass", "SC1"), Map.entry("vol", "VOL001")),
                createAttributes());
    }

    @Test
    void createDataSet_poEIsPdseAndLreclDefaults() throws Exception {
        when(rpc.call(eq("createDataset"), any())).thenReturn(json("{}"));

        service.createDataSet("U.NEW", spec("PO-E", null, 5, "U", null));

        Map<String, Object> attrs = createAttributes();
        assertEquals("PO", attrs.get("dsorg"));
        assertEquals("LIBRARY", attrs.get("dsntype"));
        assertEquals(0, attrs.get("lrecl"));
        assertFalse(attrs.containsKey("blksize"));
    }

    @Test
    void createDataSet_defaultLreclIs80() throws Exception {
        when(rpc.call(eq("createDataset"), any())).thenReturn(json("{}"));
        service.createDataSet("U.NEW", spec("PS", null, 1, "FB", null));
        assertEquals(80, createAttributes().get("lrecl"));
    }

    @Test
    void createDataSet_likeCopiesAttributesWithExplicitValuesWinning() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(
                "{\"items\":[{\"name\":\"U.MODEL\",\"dsorg\":\"PO\",\"recfm\":\"FB\",\"lrecl\":80,\"blksize\":27920,"
                        + "\"primary\":15,\"secondary\":5,\"spacu\":\"CYLINDERS\",\"dsntype\":\"LIBRARY\"}]}"));
        when(rpc.call(eq("createDataset"), any())).thenReturn(json("{}"));

        service.createDataSet("U.NEW", new CreateSpec(null, null, null, null, 3, null, null, null, null, null, null,
                null, null, null, null, "u.model"));

        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc, times(2)).call(any(), params.capture());
        @SuppressWarnings("unchecked")
        Map<String, Object> likeQuery = (Map<String, Object>) params.getAllValues().get(0);
        assertEquals(Map.of("pattern", "U.MODEL", "exactMatch", true, "attributes", true, "maxItems", 1), likeQuery);
        @SuppressWarnings("unchecked")
        Map<String, Object> attrs = (Map<String, Object>) ((Map<String, Object>) params.getAllValues().get(1))
                .get("attributes");
        assertEquals("CYL", attrs.get("alcunit"));
        assertEquals(3, attrs.get("primary"));
        assertEquals(5, attrs.get("secondary"));
        assertEquals("PO", attrs.get("dsorg"));
        assertEquals("LIBRARY", attrs.get("dsntype"));
        assertEquals(10, attrs.get("dirblk"));
        assertEquals(27920, attrs.get("blksize"));
    }

    @Test
    void createDataSet_missingLikeIsNotFound() throws Exception {
        when(rpc.call(eq("listDatasets"), any())).thenReturn(json(items()));
        ZoRpcException e = assertThrows(ZoRpcException.class, () -> service.createDataSet("U.NEW",
                new CreateSpec(null, null, null, null, null, null, null, null, null, null, null, null, null, null,
                        null, "U.MISSING")));
        assertEquals(ZoRpcException.NOT_FOUND, e.getCode());
    }

    @Test
    void createDataSet_rejectsInvalidAttributes() {
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW", spec("PS", null, null, "FB", 80)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW", spec("IS", null, 1, "FB", 80)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW", spec("PS", "BLK", 1, "FB", 80)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW", spec("PS", null, 1, "XX!", 80)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW", spec("PS", null, -1, "FB", 80)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW",
                spec("PS", null, 1, "FB", Integer.MAX_VALUE)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW",
                new CreateSpec("TOOLONG1", null, "PS", null, 1, null, null, null, "FB", null, 80, null, null, null,
                        null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("U.NEW",
                new CreateSpec(null, null, "PS", null, 1, null, null, null, "FB", null, 80, "BAD CLASS", null, null,
                        null, null)));
        assertThrows(IllegalArgumentException.class, () -> service.createDataSet("1BAD", spec("PS", null, 1, "FB", 80)));
        org.mockito.Mockito.verifyNoInteractions(rpc);
    }

    // ----------------------------------------------------------------- names

    @Test
    void dataSetNames_enforceTheNamingRules() {
        assertEquals("A.B1.C@#$-D", DataSetNames.dsn("a.b1.c@#$-d"));
        assertEquals("A".repeat(8) + ".B", DataSetNames.dsn("a".repeat(8) + ".b"));
        for (String bad : new String[]{"", "A.", ".A", "A..B", "1A", "-A", "A.1B", "A.-B", "A".repeat(9),
                "A.B C", "A/B", "A\tB", "A.B\u007f", "A.*", "A".repeat(8) + "." + "B".repeat(8) + "." + "C".repeat(8)
                + "." + "D".repeat(8) + "." + "E".repeat(8) + ".F"}) {
            assertThrows(IllegalArgumentException.class, () -> DataSetNames.dsn(bad), bad);
        }
        assertThrows(IllegalArgumentException.class, () -> DataSetNames.dsn(null));
        assertEquals("A.*.B%", DataSetNames.dslevel("a.*.b%"));
        assertEquals("A.**", DataSetNames.dslevel("a.**"));
        assertEquals("M?*", DataSetNames.memberPattern("m%*"));
        assertEquals("VOL001", DataSetNames.volser("vol001"));
        assertThrows(IllegalArgumentException.class, () -> DataSetNames.volser("VOL0011"));
        assertThrows(IllegalArgumentException.class, () -> DataSetNames.volser(""));
        assertThrows(IllegalArgumentException.class, () -> DataSetNames.member("-A"));
        assertEquals("@A", DataSetNames.member("@a"));
    }
}
