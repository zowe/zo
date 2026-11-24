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

package org.zowe.zo.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoContentIo.Options;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.UssService.CopyOptions;
import org.zowe.zo.service.UssService.UssFilter;
import org.zowe.zo.service.UssService.UssListing;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class UssServiceTest {

    private static final ObjectMapper JSON = new ObjectMapper();

    @Mock
    ZoRpcClient rpc;

    @Mock
    ZoContentIo contentIo;

    @InjectMocks
    UssService service;

    private static final String LISTING = "{\"items\":["
            + "{\"name\":\"a.txt\",\"mode\":\"-rw-r--r--\",\"links\":1,\"user\":\"IBMUSER\",\"group\":\"SYS1\",\"size\":10,\"filetag\":\"IBM-1047\",\"mtime\":\"2026-01-01T10:00:00\"},"
            + "{\"name\":\"b.log\",\"mode\":\"drwxr-xr-x\",\"links\":2,\"user\":\"OTHER\",\"group\":\"SYS1\",\"size\":0,\"filetag\":\"untagged\",\"mtime\":\"2026-01-02T10:00:00\"},"
            + "{\"name\":\"c.txt\",\"mode\":\"-rwxr-xr-x\",\"links\":1,\"user\":\"IBMUSER\",\"group\":\"SYS1\",\"size\":5,\"filetag\":\"binary\",\"mtime\":\"2026-01-03T10:00:00\"}],\"returnedRows\":3}";

    private JsonNode json(String text) throws Exception {
        return JSON.readTree(text);
    }

    private Object capture(String method) throws Exception {
        ArgumentCaptor<Object> params = ArgumentCaptor.forClass(Object.class);
        verify(rpc).call(eq(method), params.capture());
        return params.getValue();
    }

    @Test
    void listSendsLongAllDepthAndNormalizedPath() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        UssListing listing = service.list("/u/ibmuser/./x/", 2, 1000, UssFilter.NONE);

        assertEquals(Map.of("fspath", "/u/ibmuser/x", "long", true, "all", true, "depth", 2), capture("listFiles"));
        assertEquals(3, listing.items().size());
        assertEquals("drwxr-xr-x", listing.items().get(1).mode());
        assertEquals("binary", listing.items().get(2).filetag());
    }

    @Test
    void listTruncatesToMaxItemsButReportsTotal() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        UssListing listing = service.list("/u", 1, 2, UssFilter.NONE);

        assertEquals(2, listing.items().size());
        assertEquals(3, listing.totalRows());
    }

    @Test
    void listFiltersByNameTypeUserGroupAndPerm() throws Exception {
        when(rpc.call(eq("listFiles"), any())).thenReturn(json(LISTING));

        assertEquals(2, service.list("/u", 1, 10, new UssFilter("*.txt", null, null, null, null)).totalRows());
        assertEquals(1, service.list("/u", 1, 10, new UssFilter(null, "d", null, null, null)).totalRows());
        assertEquals(2, service.list("/u", 1, 10, new UssFilter(null, "f", null, null, null)).totalRows());
        assertEquals(2, service.list("/u", 1, 10, new UssFilter(null, null, "IBMUSER", "SYS1", null)).totalRows());
        assertEquals(2, service.list("/u", 1, 10, new UssFilter(null, null, null, null, "755")).totalRows());
        assertEquals(1, service.list("/u", 1, 10, new UssFilter(null, null, null, null, "644")).totalRows());
        assertEquals(1, service.list("/u", 1, 10, new UssFilter("?.log", null, null, null, null)).totalRows());
    }

    @Test
    void listRejectsBadInput() {
        assertThrows(IllegalArgumentException.class, () -> service.list("relative", 1, 10, UssFilter.NONE));
        assertThrows(IllegalArgumentException.class, () -> service.list("/u", -1, 10, UssFilter.NONE));
        assertThrows(IllegalArgumentException.class, () -> service.list("/u", 1, 10, new UssFilter(null, "x", null, null, null)));
        verifyNoInteractions(rpc);
    }

    @Test
    void readAndWriteDelegateToContentIoWithNormalizedPath() throws Exception {
        var content = new ZoContentIo.Content(new byte[]{1}, "e1");
        when(contentIo.readFile("/u/a", Options.DEFAULT)).thenReturn(content);
        assertSame(content, service.read("/u//a", Options.DEFAULT));

        var result = new ZoContentIo.WriteResult("e2", null, true);
        when(contentIo.writeFile("/u/a", new byte[]{2}, Options.DEFAULT, "e1")).thenReturn(result);
        assertSame(result, service.write("/u/a", new byte[]{2}, Options.DEFAULT, "e1"));
    }

    @Test
    void createSendsFileOrDirectoryWithMode() throws Exception {
        service.create("/u/new", true, "rwxr-x---");

        assertEquals(Map.of("fspath", "/u/new", "isDir", true, "permissions", "750"), capture("createFile"));
    }

    @Test
    void deleteSendsRecursiveOnlyWhenRequested() throws Exception {
        service.delete("/u/d", true);
        assertEquals(Map.of("fspath", "/u/d", "recursive", true), capture("deleteFile"));
    }

    @Test
    void chmodSendsOctalMode() throws Exception {
        service.chmod("/u/f", "rwxr--r--", true);

        assertEquals(Map.of("fspath", "/u/f", "mode", "744", "recursive", true), capture("chmodFile"));
        assertThrows(IllegalArgumentException.class, () -> service.chmod("/u/f", "999", false));
    }

    @Test
    void chownCombinesOwnerAndGroup() throws Exception {
        service.chown("/u/f", "IBMUSER", "SYS1", false);

        assertEquals(Map.of("fspath", "/u/f", "owner", "IBMUSER:SYS1"), capture("chownFile"));
        assertThrows(IllegalArgumentException.class, () -> service.chown("/u/f", "bad:name", null, false));
        ZoRpcException ex = assertThrows(ZoRpcException.class, () -> service.chown("/u/f", null, "SYS1", false));
        assertEquals(ZoRpcException.METHOD_NOT_FOUND, ex.getCode());
    }

    @Test
    void chtagMapsTypesToNativeTags() throws Exception {
        service.chtag("/u/f", "set", "binary", null, false);
        service.chtag("/u/f", "set", "text", "ISO8859-1", true);
        service.chtag("/u/f", "remove", null, null, false);

        verify(rpc).call("chtagFile", Map.of("fspath", "/u/f", "tag", "binary"));
        verify(rpc).call("chtagFile", Map.of("fspath", "/u/f", "tag", "ISO8859-1", "recursive", true));
        verify(rpc).call("chtagFile", Map.of("fspath", "/u/f", "tag", "untagged"));
    }

    @Test
    void chtagUnsupportedOptionsAre501() {
        assertEquals(ZoRpcException.METHOD_NOT_FOUND, assertThrows(ZoRpcException.class, () -> service.chtag("/u/f", "set", "mixed", "IBM-1047", false)).getCode());
        assertThrows(IllegalArgumentException.class, () -> service.chtag("/u/f", "set", "text", "a b", false));
        verifyNoInteractions(rpc);
    }

    @Test
    void copySendsAllOptions() throws Exception {
        service.copy("/u/a", "/u/b", new CopyOptions(true, true, true, true));

        assertEquals(Map.of("srcFsPath", "/u/a", "dstFsPath", "/u/b", "recursive", true, "followSymlinks", true,
                "preserveAttributes", true, "force", true), capture("copyUss"));
    }

    @Test
    void moveSendsForceFromOverwrite() throws Exception {
        service.move("/u/a", "/u/b", false);

        assertEquals(Map.of("source", "/u/a", "target", "/u/b", "force", false), capture("moveFile"));
    }

    @Test
    void missingPathErrorBecomesNotFound() throws Exception {
        when(rpc.call(eq("deleteFile"), any())).thenThrow(new ZoRpcException("deleteFile", ZoRpcException.INTERNAL_ERROR,
                "Failed to delete USS item: No such file or directory", null));

        assertEquals(ZoRpcException.NOT_FOUND, assertThrows(ZoRpcException.class, () -> service.delete("/u/x", false)).getCode());
    }

    @Test
    void parseDataType() throws Exception {
        assertEquals(Options.DEFAULT, UssService.parseDataType(null));
        assertEquals(Options.DEFAULT, UssService.parseDataType("text"));
        assertTrue(UssService.parseDataType("binary").isBinary());
        assertEquals("IBM-037", UssService.parseDataType("text;fileEncoding=IBM-037").encoding());
        assertEquals(ZoRpcException.METHOD_NOT_FOUND, assertThrows(ZoRpcException.class, () -> UssService.parseDataType("record")).getCode());
        assertThrows(IllegalArgumentException.class, () -> UssService.parseDataType("text;fileEncoding=a b"));
    }
}
