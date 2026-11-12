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

package org.zowe.zo.controllers.zo;

import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.UssService;
import org.zowe.zo.service.UssService.CopyOptions;
import org.zowe.zo.service.UssService.UssEntry;
import org.zowe.zo.service.UssService.UssFilter;
import org.zowe.zo.service.UssService.UssListing;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** zo-native USS API: everything zo returns that z/OSMF cannot express. */
@RestController
@RequestMapping("/zo/api/v1/fs")
@RequiredArgsConstructor
public class ZoUssController {

    private final UssService ussService;

    @GetMapping("/list")
    public Map<String, Object> list(@RequestParam("path") String path,
                                    @RequestParam(value = "depth", defaultValue = "1") int depth,
                                    @RequestParam(value = "maxItems", defaultValue = "1000") int maxItems) throws ZoRpcException {
        UssListing listing = ussService.list(path, depth, maxItems, UssFilter.NONE);
        List<Map<String, Object>> items = new ArrayList<>();
        for (UssEntry e : listing.items()) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("name", e.name());
            put(m, "mode", e.mode());
            put(m, "links", e.links());
            put(m, "user", e.user());
            put(m, "group", e.group());
            put(m, "size", e.size());
            put(m, "filetag", e.filetag());
            put(m, "mtime", e.mtime());
            items.add(m);
        }
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("items", items);
        body.put("returnedRows", items.size());
        body.put("totalRows", listing.totalRows());
        return body;
    }

    /** The {@code zo uss list --long} text listing, including the file tag column. */
    @GetMapping(value = "/listing", produces = "text/plain;charset=UTF-8")
    public String listing(@RequestParam("path") String path,
                          @RequestParam(value = "depth", defaultValue = "1") int depth,
                          @RequestParam(value = "maxItems", defaultValue = "1000") int maxItems) throws ZoRpcException {
        StringBuilder out = new StringBuilder();
        for (UssEntry e : ussService.list(path, depth, maxItems, UssFilter.NONE).items()) {
            out.append(String.join(" ", nvl(e.mode()), nvl(e.links()), nvl(e.user()), nvl(e.group()), nvl(e.size()),
                    nvl(e.filetag()), nvl(e.mtime()), nvl(e.name()))).append('\n');
        }
        return out.toString();
    }

    @PutMapping("/chtag")
    public ResponseEntity<Void> chtag(@RequestBody Map<String, Object> body) throws ZoRpcException {
        Object path = body.get("path");
        Object tag = body.get("tag");
        if (path == null || tag == null) throw new IllegalArgumentException("'path' and 'tag' are required");
        ussService.chtagRaw(path.toString(), tag.toString(), Boolean.TRUE.equals(body.get("recursive")));
        return ResponseEntity.ok().build();
    }

    @PostMapping("/copy")
    public ResponseEntity<Void> copy(@RequestBody Map<String, Object> body) throws ZoRpcException {
        Object from = body.get("from");
        Object to = body.get("to");
        if (from == null || to == null) throw new IllegalArgumentException("'from' and 'to' are required");
        ussService.copy(from.toString(), to.toString(), new CopyOptions(flag(body, "recursive"), flag(body, "followSymlinks"),
                flag(body, "preserveAttributes"), flag(body, "force")));
        return ResponseEntity.status(HttpStatus.CREATED).build();
    }

    private static boolean flag(Map<String, Object> body, String key) {
        return Boolean.TRUE.equals(body.get(key));
    }

    private static String nvl(Object value) {
        return value == null ? "-" : value.toString();
    }

    private static void put(Map<String, Object> m, String key, Object value) {
        if (value != null) m.put(key, value);
    }
}
