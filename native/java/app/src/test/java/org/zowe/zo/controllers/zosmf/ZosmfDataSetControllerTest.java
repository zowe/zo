package org.zowe.zo.controllers.zosmf;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.DataSetService;
import org.zowe.zo.service.DataSetService.CopySource;
import org.zowe.zo.service.DataSetService.CreateSpec;
import org.zowe.zo.service.DataSetService.DataSetInfo;
import org.zowe.zo.service.DataSetService.ListPage;
import org.zowe.zo.service.DataSetService.ListQuery;
import org.zowe.zo.service.DataSetService.MemberInfo;
import org.zowe.zo.service.DataSetService.Target;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.delete;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.put;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class ZosmfDataSetControllerTest {

    private static final String BASE = "/zosmf/restfiles/ds";

    @Mock
    DataSetService service;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZosmfDataSetController(service, new ObjectMapper()))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    private static DataSetInfo info(String name) {
        return new DataSetInfo(name, 15L, 1, 6160, "2020/01/21", null, "0x3390", "PDS", "PO", "", false, 80, null,
                false, false, 5L, "2021/03/23", "FB", 1L, "TRACKS", null, 40, 1, "VOL001", List.of("VOL001"));
    }

    private static ListPage<DataSetInfo> page(boolean more, Integer total, String... names) {
        return new ListPage<>(java.util.Arrays.stream(names).map(ZosmfDataSetControllerTest::info).toList(), more,
                total);
    }

    private ListQuery capturedQuery(String dslevel, String volser) throws Exception {
        ArgumentCaptor<ListQuery> captor = ArgumentCaptor.forClass(ListQuery.class);
        verify(service).listDataSets(eq(dslevel), eq(volser), captor.capture());
        return captor.getValue();
    }

    // ------------------------------------------------------------ data set list

    @Test
    void list_defaultReturnsDsnameOnlyAndPassesPatternUnchanged() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(false, null, "USER.A", "USER.B"));

        mockMvc.perform(get(BASE).param("dslevel", "USER.*"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].dsname").value("USER.A"))
                .andExpect(jsonPath("$.items[0].dsorg").doesNotExist())
                .andExpect(jsonPath("$.returnedRows").value(2))
                .andExpect(jsonPath("$.moreRows").value(false))
                .andExpect(jsonPath("$.totalRows").doesNotExist())
                .andExpect(jsonPath("$.JSONversion").value(1));

        ListQuery q = capturedQuery("USER.*", null);
        assertEquals(1000, q.maxItems());
        assertEquals(false, q.attributes());
    }

    @Test
    void list_baseAttributesUseZosmfFieldNamesAndStringValues() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(false, null, "USER.PDS"));

        mockMvc.perform(get(BASE).param("dslevel", "USER").header("X-IBM-Attributes", "base"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].dsname").value("USER.PDS"))
                .andExpect(jsonPath("$.items[0].blksz").value("6160"))
                .andExpect(jsonPath("$.items[0].cdate").value("2020/01/21"))
                .andExpect(jsonPath("$.items[0].dev").value("3390"))
                .andExpect(jsonPath("$.items[0].dsntp").value("PDS"))
                .andExpect(jsonPath("$.items[0].dsorg").value("PO"))
                .andExpect(jsonPath("$.items[0].edate").value("***None***"))
                .andExpect(jsonPath("$.items[0].extx").value("1"))
                .andExpect(jsonPath("$.items[0].lrecl").value("80"))
                .andExpect(jsonPath("$.items[0].migr").value("NO"))
                .andExpect(jsonPath("$.items[0].mvol").value("N"))
                .andExpect(jsonPath("$.items[0].rdate").value("2021/03/23"))
                .andExpect(jsonPath("$.items[0].recfm").value("FB"))
                .andExpect(jsonPath("$.items[0].sizex").value("15"))
                .andExpect(jsonPath("$.items[0].spacu").value("TRACKS"))
                .andExpect(jsonPath("$.items[0].used").value("40"))
                .andExpect(jsonPath("$.items[0].vol").value("VOL001"))
                .andExpect(jsonPath("$.items[0].vols").value("VOL001"));
        assertEquals(true, capturedQuery("USER", null).attributes());
    }

    @Test
    void list_volAttributesReturnOnlyDsnameAndVol() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(false, null, "USER.PDS"));

        mockMvc.perform(get(BASE).param("dslevel", "USER").header("X-IBM-Attributes", "vol"))
                .andExpect(jsonPath("$.items[0].vol").value("VOL001"))
                .andExpect(jsonPath("$.items[0].dsorg").doesNotExist());
    }

    @Test
    void list_maxItemsHeaderStartAndVolserArePassedOn() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(true, null, "USER.A"));

        mockMvc.perform(get(BASE).param("dslevel", "USER").param("start", "USER.A").param("volser", "VOL001")
                        .header("X-IBM-Max-Items", "1"))
                .andExpect(jsonPath("$.moreRows").value(true));

        ListQuery q = capturedQuery("USER", "VOL001");
        assertEquals(1, q.maxItems());
        assertEquals("USER.A", q.start());
    }

    @Test
    void list_maxItemsZeroMeansUnlimitedAndInvalidFallsBackToDefault() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(false, null));

        mockMvc.perform(get(BASE).param("dslevel", "A").header("X-IBM-Max-Items", "0")).andExpect(status().isOk());
        mockMvc.perform(get(BASE).param("dslevel", "B").header("X-IBM-Max-Items", "abc")).andExpect(status().isOk());

        assertEquals(0, capturedQuery("A", null).maxItems());
        assertEquals(1000, capturedQuery("B", null).maxItems());
    }

    @Test
    void list_totalRowsOnlyWhenRequestedAndMoreRowsExist() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenReturn(page(true, 7, "USER.A"));

        mockMvc.perform(get(BASE).param("dslevel", "USER").header("X-IBM-Attributes", "dsname,total")
                        .header("X-IBM-Max-Items", "1"))
                .andExpect(jsonPath("$.totalRows").value(7));
        mockMvc.perform(get(BASE).param("dslevel", "USER").header("X-IBM-Max-Items", "1"))
                .andExpect(jsonPath("$.totalRows").doesNotExist());
    }

    @Test
    void list_missingDslevelIs400() throws Exception {
        mockMvc.perform(get(BASE)).andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.message").value("dslevel parameter is required"));
        verifyNoInteractions(service);
    }

    @Test
    void list_invalidInputIs400() throws Exception {
        when(service.listDataSets(any(), any(), any())).thenThrow(new IllegalArgumentException("Invalid dslevel"));
        mockMvc.perform(get(BASE).param("dslevel", "BAD DS")).andExpect(status().isBadRequest());
    }

    // -------------------------------------------------------------- members

    @Test
    void members_defaultReturnsMemberNamesOnly() throws Exception {
        when(service.listMembers(any(), any(), any())).thenReturn(new ListPage<>(
                List.of(new MemberInfo("MEM1", "USER", 1, 2, null, null, null, null, null, null, "N")), false, null));

        mockMvc.perform(get(BASE + "/USER.PDS/member").param("pattern", "ME%"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].member").value("MEM1"))
                .andExpect(jsonPath("$.items[0].vers").doesNotExist())
                .andExpect(jsonPath("$.returnedRows").value(1))
                .andExpect(jsonPath("$.JSONversion").value(1));
        verify(service).listMembers(eq("USER.PDS"), eq("ME%"), any());
    }

    @Test
    void members_baseReturnsStatisticsWithNumericFields() throws Exception {
        when(service.listMembers(any(), any(), any())).thenReturn(new ListPage<>(
                List.of(new MemberInfo("MEM1", "IBMUSER", 1, 2, "2020/01/01", "2020/02/02", "12:30:00", 10, 11, 12,
                        "N")), true, null));

        mockMvc.perform(get(BASE + "/USER.PDS/member").header("X-IBM-Attributes", "base")
                        .header("X-IBM-Max-Items", "1").param("start", "MEM1"))
                .andExpect(jsonPath("$.items[0].vers").value(1))
                .andExpect(jsonPath("$.items[0].mod").value(2))
                .andExpect(jsonPath("$.items[0].c4date").value("2020/01/01"))
                .andExpect(jsonPath("$.items[0].m4date").value("2020/02/02"))
                .andExpect(jsonPath("$.items[0].mtime").value("12:30:00"))
                .andExpect(jsonPath("$.items[0].cnorc").value(10))
                .andExpect(jsonPath("$.items[0].inorc").value(11))
                .andExpect(jsonPath("$.items[0].mnorc").value(12))
                .andExpect(jsonPath("$.items[0].user").value("IBMUSER"))
                .andExpect(jsonPath("$.items[0].sclm").value("N"))
                .andExpect(jsonPath("$.moreRows").value(true));
        ArgumentCaptor<ListQuery> captor = ArgumentCaptor.forClass(ListQuery.class);
        verify(service).listMembers(eq("USER.PDS"), any(), captor.capture());
        assertEquals("MEM1", captor.getValue().start());
        assertEquals(1, captor.getValue().maxItems());
        assertEquals(true, captor.getValue().attributes());
    }

    @Test
    void members_missingDataSetIs404() throws Exception {
        when(service.listMembers(any(), any(), any()))
                .thenThrow(new ZoRpcException("listDsMembers", ZoRpcException.NOT_FOUND, "Data set not found", null));
        mockMvc.perform(get(BASE + "/NO.SUCH/member")).andExpect(status().isNotFound())
                .andExpect(jsonPath("$.rc").value(ZoRpcException.NOT_FOUND));
    }

    // -------------------------------------------------------------- read

    private ZoContentIo.Content body(String text, String etag) {
        return new ZoContentIo.Content(text.getBytes(StandardCharsets.UTF_8), etag);
    }

    @Test
    void read_textReturnsPlainUtf8WithEtag() throws Exception {
        when(service.read(any(), any())).thenReturn(body("hello", "abc123"));

        mockMvc.perform(get(BASE + "/USER.DATA"))
                .andExpect(status().isOk())
                .andExpect(header().string("ETag", "\"abc123\""))
                .andExpect(content().contentType("text/plain;charset=UTF-8"))
                .andExpect(content().string("hello"));

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).read(target.capture(), options.capture());
        assertEquals("USER.DATA", target.getValue().dsname());
        assertNull(target.getValue().member());
        assertNull(options.getValue().encoding());
    }

    @Test
    void read_memberViaEncodedParenthesesAndFileEncoding() throws Exception {
        when(service.read(any(), any())).thenReturn(body("x", "e1"));

        mockMvc.perform(get(java.net.URI.create(BASE + "/USER.PDS%28MEM1%29")).header("X-IBM-Data-Type", "text;fileEncoding=IBM-037"))
                .andExpect(status().isOk());

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).read(target.capture(), options.capture());
        assertEquals("USER.PDS", target.getValue().dsname());
        assertEquals("MEM1", target.getValue().member());
        assertEquals("IBM-037", options.getValue().encoding());
    }

    @Test
    void read_memberWithLiteralParentheses() throws Exception {
        when(service.read(any(), any())).thenReturn(body("x", "e1"));

        mockMvc.perform(get(BASE + "/USER.PDS(MEM1)")).andExpect(status().isOk());

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        verify(service).read(target.capture(), any());
        assertEquals("MEM1", target.getValue().member());
    }

    @Test
    void read_withVolume() throws Exception {
        when(service.read(any(), any())).thenReturn(body("x", "e1"));

        mockMvc.perform(get(BASE + "/-(VOL001)/USER.PDS(MEM1)")).andExpect(status().isOk());
        mockMvc.perform(get(BASE + "/-(VOL001)/USER.SEQ")).andExpect(status().isOk());

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        verify(service, org.mockito.Mockito.times(2)).read(target.capture(), any());
        assertEquals("VOL001", target.getAllValues().get(0).volser());
        assertEquals("MEM1", target.getAllValues().get(0).member());
        assertEquals("USER.SEQ", target.getAllValues().get(1).dsname());
        assertEquals("VOL001", target.getAllValues().get(1).volser());
    }

    @Test
    void read_binaryReturnsOctetStreamAndRawEncoding() throws Exception {
        when(service.read(any(), any())).thenReturn(new ZoContentIo.Content(new byte[]{0, 1, (byte) 0xff}, "b1"));

        mockMvc.perform(get(BASE + "/USER.LOAD").header("X-IBM-Data-Type", "binary"))
                .andExpect(status().isOk())
                .andExpect(content().contentType(MediaType.APPLICATION_OCTET_STREAM))
                .andExpect(content().bytes(new byte[]{0, 1, (byte) 0xff}));

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).read(any(), options.capture());
        assertEquals(ZoContentIo.BINARY, options.getValue().encoding());
    }

    @Test
    void read_ifNoneMatchWithCurrentEtagIs304() throws Exception {
        when(service.read(any(), any())).thenReturn(body("hello", "abc123"));

        mockMvc.perform(get(BASE + "/USER.DATA").header("If-None-Match", "\"abc123\""))
                .andExpect(status().isNotModified())
                .andExpect(header().string("ETag", "\"abc123\""))
                .andExpect(content().string(""));
        mockMvc.perform(get(BASE + "/USER.DATA").header("If-None-Match", "other, abc123"))
                .andExpect(status().isNotModified());
    }

    @Test
    void read_ifNoneMatchWithStaleEtagReturnsContent() throws Exception {
        when(service.read(any(), any())).thenReturn(body("hello", "abc123"));

        mockMvc.perform(get(BASE + "/USER.DATA").header("If-None-Match", "old"))
                .andExpect(status().isOk()).andExpect(content().string("hello"));
    }

    @Test
    void read_recordDataTypeIs501() throws Exception {
        mockMvc.perform(get(BASE + "/USER.DATA").header("X-IBM-Data-Type", "record"))
                .andExpect(status().isNotImplemented());
        verifyNoInteractions(service);
    }

    @Test
    void read_contentSearchParameterIs501() throws Exception {
        mockMvc.perform(get(BASE + "/USER.DATA").param("search", "x")).andExpect(status().isNotImplemented());
    }

    @Test
    void read_invalidDataTypeOrEncodingIs400() throws Exception {
        mockMvc.perform(get(BASE + "/USER.DATA").header("X-IBM-Data-Type", "weird")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/USER.DATA").header("X-IBM-Data-Type", "text;fileEncoding=A B"))
                .andExpect(status().isBadRequest());
        verifyNoInteractions(service);
    }

    @Test
    void read_missingTargetIs404() throws Exception {
        when(service.read(any(), any()))
                .thenThrow(new ZoRpcException("readDataset", ZoRpcException.NOT_FOUND, "not found", null));
        mockMvc.perform(get(BASE + "/NO.SUCH")).andExpect(status().isNotFound());
    }

    @Test
    void read_malformedNamesAreRejectedBeforeReachingTheService() throws Exception {
        mockMvc.perform(get(BASE + "/USER.PDS(MEM(1)")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE + "/-(TOOLONGVOL)/USER.SEQ")).andExpect(status().isBadRequest());
    }

    // -------------------------------------------------------------- write

    @Test
    void write_createdReturns201WithEtag() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("new1", null, true));

        mockMvc.perform(put(BASE + "/USER.PDS(NEW)").contentType("text/plain;charset=UTF-8")
                        .header("X-IBM-Data-Type", "text").content("data"))
                .andExpect(status().isCreated())
                .andExpect(header().string("ETag", "\"new1\""))
                .andExpect(header().doesNotExist(ZosmfDataSetController.TRUNCATION_HEADER));

        ArgumentCaptor<byte[]> data = ArgumentCaptor.forClass(byte[].class);
        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).write(any(), data.capture(), options.capture(), eq(null));
        assertEquals("data", new String(data.getValue(), StandardCharsets.UTF_8));
        assertEquals("UTF-8", options.getValue().localEncoding());
    }

    @Test
    void write_overwriteReturns204AndHonorsIfMatchAndFileEncoding() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("new2", null, false));

        mockMvc.perform(put(BASE + "/USER.SEQ").header("If-Match", "\"old1\"").contentType("text/plain")
                        .header("X-IBM-Data-Type", "text;fileEncoding=IBM-1047").content("x"))
                .andExpect(status().isNoContent())
                .andExpect(header().string("ETag", "\"new2\""));

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).write(any(), any(), options.capture(), eq("old1"));
        assertEquals("IBM-1047", options.getValue().encoding());
        // no charset declared: z/OSMF's ISO8859-1 default
        assertEquals("ISO8859-1", options.getValue().localEncoding());
    }

    @Test
    void write_bareCharsetContentTypeIsAcceptedLikeZoweClientsSendIt() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("e", null, false));

        mockMvc.perform(put(BASE + "/USER.SEQ").contentType("ISO8859-1").content("x"))
                .andExpect(status().isNoContent());

        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).write(any(), any(), options.capture(), any());
        assertEquals("ISO8859-1", options.getValue().localEncoding());
    }

    @Test
    void write_binaryIgnoresCharsetAndUsesRawEncoding() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("e", null, false));

        mockMvc.perform(put(BASE + "/-(VOL001)/USER.LOAD").contentType(MediaType.APPLICATION_OCTET_STREAM)
                        .header("X-IBM-Data-Type", "binary").content(new byte[]{1, 2, 3}))
                .andExpect(status().isNoContent());

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        ArgumentCaptor<ZoContentIo.Options> options = ArgumentCaptor.forClass(ZoContentIo.Options.class);
        verify(service).write(target.capture(), eq(new byte[]{1, 2, 3}), options.capture(), any());
        assertEquals(ZoContentIo.BINARY, options.getValue().encoding());
        assertEquals("VOL001", target.getValue().volser());
    }

    @Test
    void write_emptyBodyWritesEmptyContent() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("e", null, true));

        mockMvc.perform(put(BASE + "/USER.PDS(EMPTY)")).andExpect(status().isCreated());
        verify(service).write(any(), eq(new byte[0]), any(), any());
    }

    @Test
    void write_truncationWarningSurfacesAsSingleLineHeader() throws Exception {
        when(service.write(any(), any(), any(), any()))
                .thenReturn(new ZoContentIo.WriteResult("e", "Line 3 truncated\nLine 9 truncated", false));

        mockMvc.perform(put(BASE + "/USER.SEQ").content("x"))
                .andExpect(status().isNoContent())
                .andExpect(header().string(ZosmfDataSetController.TRUNCATION_HEADER,
                        "Line 3 truncated Line 9 truncated"));
    }

    @Test
    void write_etagMismatchIs412() throws Exception {
        when(service.write(any(), any(), any(), any()))
                .thenThrow(new ZoRpcException("writeDataset", ZoRpcException.ETAG_MISMATCH, "Etag mismatch", null));

        mockMvc.perform(put(BASE + "/USER.SEQ").header("If-Match", "stale").content("x"))
                .andExpect(status().isPreconditionFailed());
    }

    @Test
    void write_ifMatchStarMeansUnconditional() throws Exception {
        when(service.write(any(), any(), any(), any())).thenReturn(new ZoContentIo.WriteResult("e", null, false));

        mockMvc.perform(put(BASE + "/USER.SEQ").header("If-Match", "*").content("x"))
                .andExpect(status().isNoContent());
        verify(service).write(any(), any(), any(), eq(null));
    }

    @Test
    void write_recordDataTypeIs501() throws Exception {
        mockMvc.perform(put(BASE + "/USER.SEQ").header("X-IBM-Data-Type", "record").content("x"))
                .andExpect(status().isNotImplemented());
        verify(service, never()).write(any(), any(), any(), any());
    }

    // ------------------------------------------------------------ utilities

    private static final String JSON = MediaType.APPLICATION_JSON_VALUE;

    @Test
    void rename_dataSet() throws Exception {
        mockMvc.perform(put(BASE + "/USER.NEW").contentType(JSON)
                        .content("{\"request\":\"rename\",\"from-dataset\":{\"dsn\":\"USER.OLD\"}}"))
                .andExpect(status().isOk());
        verify(service).renameDataSet("USER.OLD", "USER.NEW");
    }

    @Test
    void rename_member() throws Exception {
        mockMvc.perform(put(BASE + "/USER.PDS(NEWM)").contentType(JSON)
                        .content("{\"request\":\"rename\",\"from-dataset\":{\"dsn\":\"user.pds\",\"member\":\"OLDM\"}}"))
                .andExpect(status().isOk());
        verify(service).renameMember("USER.PDS", "OLDM", "NEWM");
    }

    @Test
    void rename_memberAcrossDataSetsIs400() throws Exception {
        mockMvc.perform(put(BASE + "/USER.PDS(NEWM)").contentType(JSON)
                        .content("{\"request\":\"rename\",\"from-dataset\":{\"dsn\":\"USER.OTHER\",\"member\":\"OLDM\"}}"))
                .andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/USER.PDS(NEWM)").contentType(JSON)
                        .content("{\"request\":\"rename\",\"from-dataset\":{\"dsn\":\"USER.PDS\"}}"))
                .andExpect(status().isBadRequest());
        verify(service, never()).renameMember(any(), any(), any());
    }

    @Test
    void rename_missingSourceIs404() throws Exception {
        org.mockito.Mockito.doThrow(new ZoRpcException("renameDataset", ZoRpcException.NOT_FOUND, "missing", null))
                .when(service).renameDataSet(any(), any());
        mockMvc.perform(put(BASE + "/USER.NEW").contentType(JSON)
                        .content("{\"request\":\"rename\",\"from-dataset\":{\"dsn\":\"USER.OLD\"}}"))
                .andExpect(status().isNotFound());
    }

    @Test
    void copy_memberWithReplace() throws Exception {
        mockMvc.perform(put(BASE + "/USER.TO(MEMB)").contentType(JSON + ";charset=UTF-8")
                        .content("{\"request\":\"copy\",\"from-dataset\":{\"dsn\":\"USER.FROM\",\"member\":\"MEMA\"},"
                                + "\"replace\":true,\"unknown\":1}"))
                .andExpect(status().isOk());

        ArgumentCaptor<Target> to = ArgumentCaptor.forClass(Target.class);
        verify(service).copy(eq(new CopySource("USER.FROM", "MEMA")), to.capture(), eq(true));
        assertEquals("USER.TO(MEMB)", to.getValue().fullName());
    }

    @Test
    void copy_dataSetDefaultsToNoReplace() throws Exception {
        mockMvc.perform(put(BASE + "/USER.TO").contentType(JSON)
                        .content("{\"request\":\"copy\",\"from-dataset\":{\"dsn\":\"USER.FROM\"}}"))
                .andExpect(status().isOk());
        verify(service).copy(eq(new CopySource("USER.FROM", null)), any(), eq(false));
    }

    @Test
    void copy_unsupportedSourcesAre501() throws Exception {
        mockMvc.perform(put(BASE + "/USER.TO").contentType(JSON)
                        .content("{\"request\":\"copy\",\"from-file\":{\"filename\":\"/u/x\"}}"))
                .andExpect(status().isNotImplemented());
        mockMvc.perform(put(BASE + "/USER.TO").contentType(JSON)
                        .content("{\"request\":\"copy\",\"from-dataset\":{\"dsn\":\"USER.FROM\",\"member\":\"*\"}}"))
                .andExpect(status().isNotImplemented());
        verify(service, never()).copy(any(), any(), org.mockito.ArgumentMatchers.anyBoolean());
    }

    @Test
    void hrecall_restoresDataSet() throws Exception {
        mockMvc.perform(put(BASE + "/USER.MIGR").contentType(JSON).content("{\"request\":\"hrecall\",\"wait\":true}"))
                .andExpect(status().isOk());
        verify(service).recall("USER.MIGR");
    }

    @Test
    void hrecall_onMemberIs400() throws Exception {
        mockMvc.perform(put(BASE + "/USER.PDS(M)").contentType(JSON).content("{\"request\":\"hrecall\"}"))
                .andExpect(status().isBadRequest());
    }

    @Test
    void hsmMigrateAndDeleteAre501() throws Exception {
        mockMvc.perform(put(BASE + "/USER.DS").contentType(JSON).content("{\"request\":\"hmigrate\"}"))
                .andExpect(status().isNotImplemented())
                .andExpect(jsonPath("$.message").value(org.hamcrest.Matchers.containsString("not supported")));
        mockMvc.perform(put(BASE + "/USER.DS").contentType(JSON).content("{\"request\":\"hdelete\"}"))
                .andExpect(status().isNotImplemented());
    }

    @Test
    void utility_invalidBodyOrUnknownRequestIs400() throws Exception {
        mockMvc.perform(put(BASE + "/USER.DS").contentType(JSON).content("not json")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/USER.DS").contentType(JSON).content("{\"x\":1}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/USER.DS").contentType(JSON).content("{\"request\":\"explode\"}"))
                .andExpect(status().isBadRequest());
        verify(service, never()).write(any(), any(), any(), any());
    }

    @Test
    void ams_is501() throws Exception {
        mockMvc.perform(put("/zosmf/restfiles/ams").contentType(JSON).content("{\"input\":[\"LISTCAT\"]}"))
                .andExpect(status().isNotImplemented());
    }

    // --------------------------------------------------------------- create

    @Test
    void create_mapsZosmfBodyToCreateSpecAndReturns201() throws Exception {
        mockMvc.perform(post(BASE + "/user.new.ds").contentType(JSON)
                        .content("{\"volser\":\"VOL001\",\"unit\":\"3390\",\"dsorg\":\"PO\",\"alcunit\":\"CYL\","
                                + "\"primary\":10,\"secondary\":\"5\",\"dirblk\":20,\"avgblk\":500,\"recfm\":\"FB\","
                                + "\"blksize\":27920,\"lrecl\":80,\"storclass\":\"SC1\",\"mgntclass\":\"MC1\","
                                + "\"dataclass\":\"DC1\",\"dsntype\":\"PDS\",\"like\":\"USER.MODEL\"}"))
                .andExpect(status().isCreated())
                .andExpect(content().string(""));

        ArgumentCaptor<CreateSpec> spec = ArgumentCaptor.forClass(CreateSpec.class);
        verify(service).createDataSet(eq("USER.NEW.DS"), spec.capture());
        assertEquals(new CreateSpec("VOL001", "3390", "PO", "CYL", 10, 5, 20, 500, "FB", 27920, 80, "SC1", "MC1",
                "DC1", "PDS", "USER.MODEL"), spec.getValue());
    }

    @Test
    void create_inheritDsntypeIsDroppedAndEmptyBodyAllowed() throws Exception {
        mockMvc.perform(post(BASE + "/USER.NEW").contentType(JSON).content("{\"dsntype\":\"INHERIT\"}"))
                .andExpect(status().isCreated());
        ArgumentCaptor<CreateSpec> spec = ArgumentCaptor.forClass(CreateSpec.class);
        verify(service).createDataSet(eq("USER.NEW"), spec.capture());
        assertNull(spec.getValue().dsntype());
    }

    @Test
    void create_rejectsMemberNameAndBadJsonAndBadNumbers() throws Exception {
        mockMvc.perform(post(BASE + "/USER.PDS(M)").contentType(JSON).content("{}")).andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/USER.NEW").contentType(JSON).content("[1]")).andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/USER.NEW").contentType(JSON).content("{broken")).andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/USER.NEW").contentType(JSON).content("{\"primary\":\"ten\"}"))
                .andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/USER.NEW").contentType(JSON).content("{\"recfm\":5}"))
                .andExpect(status().isBadRequest());
        verify(service, never()).createDataSet(any(), any());
    }

    // --------------------------------------------------------------- delete

    @Test
    void delete_dataSetAndMemberAndVolumeReturn204() throws Exception {
        mockMvc.perform(delete(BASE + "/USER.DATA")).andExpect(status().isNoContent());
        mockMvc.perform(delete(BASE + "/USER.PDS(MEM)")).andExpect(status().isNoContent());
        mockMvc.perform(delete(BASE + "/-(VOL001)/USER.DATA")).andExpect(status().isNoContent());

        ArgumentCaptor<Target> target = ArgumentCaptor.forClass(Target.class);
        verify(service, org.mockito.Mockito.times(3)).delete(target.capture());
        assertEquals("USER.DATA", target.getAllValues().get(0).fullName());
        assertEquals("USER.PDS(MEM)", target.getAllValues().get(1).fullName());
        assertEquals("VOL001", target.getAllValues().get(2).volser());
    }

    @Test
    void delete_missingIs404() throws Exception {
        org.mockito.Mockito.doThrow(new ZoRpcException("deleteDataset", ZoRpcException.NOT_FOUND, "missing", null))
                .when(service).delete(any());
        mockMvc.perform(delete(BASE + "/USER.NOPE")).andExpect(status().isNotFound())
                .andExpect(jsonPath("$.details").isArray());
    }

    @Test
    void delete_invalidNameIs400() throws Exception {
        mockMvc.perform(delete(BASE + "/1BAD.NAME")).andExpect(status().isBadRequest());
        verify(service, never()).delete(any());
    }
}
