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

package org.zowe.zo.controllers.zosmf;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.zowe.zo.config.GlobalExceptionHandler;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoContentIo.Content;
import org.zowe.zo.ffm.ZoContentIo.Options;
import org.zowe.zo.ffm.ZoContentIo.WriteResult;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.UssService;

import java.util.Map;

import static org.hamcrest.Matchers.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
import static org.zowe.zo.testutil.TestJson.json;

@ExtendWith(MockitoExtension.class)
class ZosmfUssControllerTest {

    private static final String BASE = "/zosmf/restfiles/fs";

    private static final String LISTING = "{\"items\":["
            + "{\"name\":\"a.txt\",\"mode\":\"-rw-r--r--\",\"links\":1,\"user\":\"IBMUSER\",\"group\":\"SYS1\",\"size\":10,\"filetag\":\"IBM-1047\",\"mtime\":\"2026-01-01T10:00:00\"},"
            + "{\"name\":\"b\",\"mode\":\"drwxr-xr-x\",\"links\":2,\"user\":\"IBMUSER\",\"group\":\"SYS1\",\"size\":0,\"filetag\":\"untagged\",\"mtime\":\"2026-01-02T10:00:00\"}],\"returnedRows\":2}";

    @Mock
    ZoRpcClient rpc;

    @Mock
    ZoContentIo contentIo;

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ZosmfUssController(new UssService(rpc, contentIo)))
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    private Object capture(String method) throws Exception {
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq(method), params.capture());
        return params.getValue();
    }

    @Test
    void listReturnsZosmfShape() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        mockMvc.perform(get(BASE).param("path", "/u/ibmuser"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].name").value("a.txt"))
                .andExpect(jsonPath("$.items[0].mode").value("-rw-r--r--"))
                .andExpect(jsonPath("$.items[0].size").value(10))
                .andExpect(jsonPath("$.items[0].user").value("IBMUSER"))
                .andExpect(jsonPath("$.items[0].group").value("SYS1"))
                .andExpect(jsonPath("$.items[0].mtime").value("2026-01-01T10:00:00"))
                .andExpect(jsonPath("$.items[0].filetag").doesNotExist())
                .andExpect(jsonPath("$.items[1].mode").value("drwxr-xr-x"))
                .andExpect(jsonPath("$.returnedRows").value(2))
                .andExpect(jsonPath("$.totalRows").value(2))
                .andExpect(jsonPath("$.JSONversion").value(1));

        assertEquals(Map.of("fspath", "/u/ibmuser", "long", true, "all", true, "depth", 1), capture("listFiles"));
    }

    @Test
    void listHonorsMaxItemsHeaderAndFilters() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        mockMvc.perform(get(BASE).param("path", "/u/ibmuser").header("X-IBM-Max-Items", "1"))
                .andExpect(jsonPath("$.returnedRows").value(1))
                .andExpect(jsonPath("$.totalRows").value(2));
        mockMvc.perform(get(BASE).param("path", "/u/ibmuser").param("type", "d").param("name", "b").param("user", "IBMUSER").param("depth", "3"))
                .andExpect(jsonPath("$.items", hasSize(1)))
                .andExpect(jsonPath("$.items[0].name").value("b"));
        mockMvc.perform(get(BASE).param("path", "/u/ibmuser").header("X-IBM-Max-Items", "0"))
                .andExpect(jsonPath("$.returnedRows").value(2));
    }

    @Test
    void nonNumericDepthAndMaxItemsAre400() throws Exception {
        mockMvc.perform(get(BASE).param("path", "/u").param("depth", "abc")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("path", "/u").header("X-IBM-Max-Items", "many")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void encodedSegmentsAreDecodedIntoThePath() throws Exception {
        when(contentIo.readFile(eq("/u/my dir/f.txt"), any())).thenReturn(new Content("x".getBytes(), "E"));

        mockMvc.perform(get(java.net.URI.create(BASE + "/u/my%20dir/f.txt"))).andExpect(status().isOk());
    }

    @Test
    void readHonorsRecordRangeForText() throws Exception {
        when(contentIo.readFile(eq("/u/a"), any())).thenReturn(new Content("l0\nl1\nl2\n".getBytes(), "E"));

        mockMvc.perform(get(BASE + "/u/a").header("X-IBM-Record-Range", "1,1")).andExpect(content().string("l1\n"));
        mockMvc.perform(get(BASE + "/u/a").header("X-IBM-Record-Range", "0-0").header("X-IBM-Data-Type", "binary"))
                .andExpect(status().isNotImplemented());
    }

    @Test
    void listViaTrailingSlashFormUsesPathParameter() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        mockMvc.perform(get(BASE + "/").param("path", "/u/ibmuser"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.JSONversion").value(1));
    }

    @Test
    void listRejectsMissingPathBadInputAndUnsupportedFilters() throws Exception {
        mockMvc.perform(get(BASE)).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("path", "relative")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("path", "/u").header("X-IBM-Max-Items", "-1")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("path", "/u").param("type", "z")).andExpect(status().isBadRequest());
        mockMvc.perform(get(BASE).param("path", "/u").param("mtime", "+1")).andExpect(status().isNotImplemented());
        mockMvc.perform(get(BASE).param("path", "/u").param("size", "+1")).andExpect(status().isNotImplemented());
        mockMvc.perform(get(BASE).param("path", "/u").param("filesys", "same")).andExpect(status().isNotImplemented());
        mockMvc.perform(get(BASE).param("path", "/u").param("symlinks", "follow")).andExpect(status().isNotImplemented());
        verifyNoInteractions(rpc);
    }

    @Test
    void readReturnsContentAndEtag() throws Exception {
        when(contentIo.readFile("/u/ibmuser/a.txt", Options.DEFAULT)).thenReturn(new Content("hello".getBytes(), "ABC123"));

        mockMvc.perform(get(BASE + "/u/ibmuser/a.txt"))
                .andExpect(status().isOk())
                .andExpect(header().string(HttpHeaders.ETAG, "\"ABC123\""))
                .andExpect(content().contentTypeCompatibleWith(MediaType.TEXT_PLAIN))
                .andExpect(content().string("hello"));
    }

    @Test
    void readPassesDataTypeToContentIo() throws Exception {
        when(contentIo.readFile(eq("/u/a"), any())).thenReturn(new Content(new byte[]{1, 2}, "E"));

        mockMvc.perform(get(BASE + "/u/a").header("X-IBM-Data-Type", "binary"))
                .andExpect(status().isOk())
                .andExpect(content().contentType(MediaType.APPLICATION_OCTET_STREAM))
                .andExpect(content().bytes(new byte[]{1, 2}));
        mockMvc.perform(get(BASE + "/u/a").header("X-IBM-Data-Type", "text;fileEncoding=IBM-037")).andExpect(status().isOk());

        verify(contentIo).readFile("/u/a", new Options("binary", null, null));
        verify(contentIo).readFile("/u/a", new Options("IBM-037", null, null));
    }

    @Test
    void readWithMatchingIfNoneMatchIs304() throws Exception {
        when(contentIo.readFile(eq("/u/a"), any())).thenReturn(new Content("hello".getBytes(), "ABC123"));

        mockMvc.perform(get(BASE + "/u/a").header("If-None-Match", "\"ABC123\""))
                .andExpect(status().isNotModified())
                .andExpect(header().string(HttpHeaders.ETAG, "\"ABC123\""))
                .andExpect(content().string(""));
        mockMvc.perform(get(BASE + "/u/a").header("If-None-Match", "OTHER")).andExpect(status().isOk());
    }

    @Test
    void readMissingFileIs404WithZosmfStyleBody() throws Exception {
        when(contentIo.readFile(eq("/u/missing"), any())).thenThrow(new ZoRpcException("readFile", ZoRpcException.NOT_FOUND, "File not found", null));

        mockMvc.perform(get(BASE + "/u/missing"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.rc").value(ZoRpcException.NOT_FOUND))
                .andExpect(jsonPath("$.details").isArray());
    }

    @Test
    void readRejectsUnsupportedDataTypeAndTraversalIsNormalized() throws Exception {
        mockMvc.perform(get(BASE + "/u/a").header("X-IBM-Data-Type", "record")).andExpect(status().isNotImplemented());

        when(contentIo.readFile(eq("/etc/passwd"), any())).thenReturn(new Content(new byte[0], "E"));
        mockMvc.perform(get(BASE + "/u/../etc/passwd")).andExpect(status().isOk());
        verify(contentIo).readFile(eq("/etc/passwd"), any());
    }

    @Test
    void writeCreatedReturns201WithEtag() throws Exception {
        when(contentIo.writeFile(eq("/u/new.txt"), any(), eq(Options.DEFAULT), eq(null))).thenReturn(new WriteResult("E1", null, true));

        mockMvc.perform(put(BASE + "/u/new.txt").contentType(MediaType.TEXT_PLAIN).content("hi"))
                .andExpect(status().isCreated())
                .andExpect(header().string(HttpHeaders.ETAG, "\"E1\""));

        verify(contentIo).writeFile("/u/new.txt", "hi".getBytes(), Options.DEFAULT, null);
    }

    @Test
    void writeOverwriteReturns204AndPassesIfMatch() throws Exception {
        when(contentIo.writeFile(eq("/u/f"), any(), any(), eq("E1"))).thenReturn(new WriteResult("E2", null, false));

        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_OCTET_STREAM).content(new byte[]{9, 8})
                        .header("If-Match", "\"E1\"").header("X-IBM-Data-Type", "binary"))
                .andExpect(status().isNoContent())
                .andExpect(header().string(HttpHeaders.ETAG, "\"E2\""));

        verify(contentIo).writeFile("/u/f", new byte[]{9, 8}, new Options("binary", null, null), "E1");
    }

    @Test
    void writeWithStaleIfMatchIs412() throws Exception {
        when(contentIo.writeFile(eq("/u/f"), any(), any(), eq("OLD"))).thenThrow(new ZoRpcException("writeFile", ZoRpcException.ETAG_MISMATCH, "ETag mismatch", null));

        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.TEXT_PLAIN).content("x").header("If-Match", "OLD"))
                .andExpect(status().isPreconditionFailed())
                .andExpect(jsonPath("$.rc").value(ZoRpcException.ETAG_MISMATCH));
    }

    @Test
    void chmodMapsToChmodFile() throws Exception {
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chmod\",\"mode\":\"755\",\"recursive\":true}"))
                .andExpect(status().isOk());

        assertEquals(Map.of("fspath", "/u/f", "mode", "755", "recursive", true), capture("chmodFile"));
    }

    @Test
    void chownMapsOwnerAndGroup() throws Exception {
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chown\",\"owner\":\"IBMUSER\",\"group\":\"SYS1\"}"))
                .andExpect(status().isOk());

        assertEquals(Map.of("fspath", "/u/f", "owner", "IBMUSER:SYS1"), capture("chownFile"));
    }

    @Test
    void chtagMapsToTags() throws Exception {
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"request\":\"chtag\",\"action\":\"set\",\"type\":\"text\",\"codeset\":\"IBM-1047\",\"recursive\":true}"))
                .andExpect(status().isOk());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chtag\",\"action\":\"remove\"}"))
                .andExpect(status().isOk());

        verify(rpc).call("chtagFile", Map.of("fspath", "/u/f", "tag", "IBM-1047", "recursive", true));
        verify(rpc).call("chtagFile", Map.of("fspath", "/u/f", "tag", "untagged"));
    }

    @Test
    void chtagListReturnsStdoutLinesFromFileTags() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json("{\"items\":[{\"name\":\"f\",\"mode\":\"-rw-r--r--\",\"filetag\":\"IBM-1047\"}]}"));

        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chtag\",\"action\":\"list\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.stdout[0]", org.hamcrest.Matchers.startsWith("t IBM-1047")))
                .andExpect(jsonPath("$.stdout[0]", org.hamcrest.Matchers.endsWith("T=on  /u/f")));
    }

    @Test
    void chtagListReportsBinaryAndUntagged() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json("{\"items\":[{\"name\":\"f\",\"filetag\":\"binary\"}]}"));

        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chtag\",\"action\":\"list\"}"))
                .andExpect(jsonPath("$.stdout[0]", org.hamcrest.Matchers.startsWith("b binary")));
    }

    @Test
    void chtagMixedIs501() throws Exception {
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chtag\",\"action\":\"set\",\"type\":\"mixed\"}"))
                .andExpect(status().isNotImplemented());
    }

    @Test
    void copyReturns201AndMapsOverwriteToForce() throws Exception {
        mockMvc.perform(put(BASE + "/u/dst").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"request\":\"copy\",\"from\":\"/u/src\",\"recursive\":true,\"overwrite\":true}"))
                .andExpect(status().isCreated());

        assertEquals(Map.of("srcFsPath", "/u/src", "dstFsPath", "/u/dst", "recursive", true, "force", true), capture("copyUss"));
    }

    @Test
    void copyFromDatasetIs501() throws Exception {
        mockMvc.perform(put(BASE + "/u/dst").contentType(MediaType.APPLICATION_JSON)
                        .content("{\"request\":\"copy\",\"from\":{\"dsn\":\"A.B\"}}"))
                .andExpect(status().isNotImplemented());
    }

    @Test
    void moveMapsOverwriteToForce() throws Exception {
        mockMvc.perform(put(BASE + "/u/dst").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"move\",\"from\":\"/u/src\"}"))
                .andExpect(status().isOk());

        assertEquals(Map.of("source", "/u/src", "target", "/u/dst", "force", false), capture("moveFile"));
    }

    @Test
    void utilityValidationAndUnsupportedRequests() throws Exception {
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chmod\",\"mode\":\"9z9\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chown\",\"owner\":\"a;b\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"move\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{}")).andExpect(status().isBadRequest());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"chown\",\"group\":\"SYS1\"}")).andExpect(status().isNotImplemented());
        mockMvc.perform(put(BASE + "/u/f").contentType(MediaType.APPLICATION_JSON).content("{\"request\":\"setfacl\"}")).andExpect(status().isNotImplemented());
        verifyNoInteractions(rpc);
    }

    @Test
    void createFileAndDirectory() throws Exception {
        mockMvc.perform(post(BASE + "/u/new.txt").contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"file\",\"mode\":\"rw-r--r--\"}"))
                .andExpect(status().isCreated());
        mockMvc.perform(post(BASE + "/u/newdir").contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"directory\"}"))
                .andExpect(status().isCreated());

        verify(rpc).call("createFile", Map.of("fspath", "/u/new.txt", "isDir", false, "permissions", "644"));
        verify(rpc).call("createFile", Map.of("fspath", "/u/newdir", "isDir", true));
    }

    @Test
    void createRejectsBadTypeAndMode() throws Exception {
        mockMvc.perform(post(BASE + "/u/x").contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"symlink\"}")).andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/u/x").contentType(MediaType.APPLICATION_JSON).content("{}")).andExpect(status().isBadRequest());
        mockMvc.perform(post(BASE + "/u/x").contentType(MediaType.APPLICATION_JSON).content("{\"type\":\"file\",\"mode\":\"rwx\"}")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }

    @Test
    void deleteReturns204AndRecursiveOption() throws Exception {
        mockMvc.perform(delete(BASE + "/u/f")).andExpect(status().isNoContent());
        mockMvc.perform(delete(BASE + "/u/d").header("X-IBM-Option", "recursive")).andExpect(status().isNoContent());

        verify(rpc).call("deleteFile", Map.of("fspath", "/u/f"));
        verify(rpc).call("deleteFile", Map.of("fspath", "/u/d", "recursive", true));
    }

    @Test
    void deleteRejectsUnknownOption() throws Exception {
        mockMvc.perform(delete(BASE + "/u/f").header("X-IBM-Option", "force")).andExpect(status().isBadRequest());
        verifyNoInteractions(rpc);
    }
}
