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

import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZoContentIo.Content;
import org.zowe.zo.ffm.ZoContentIo.Options;
import org.zowe.zo.ffm.ZoContentIo.WriteResult;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.InputValidator;
import org.zowe.zo.service.UssService;
import org.zowe.zo.service.UssService.CopyOptions;
import org.zowe.zo.service.UssService.UssEntry;
import org.zowe.zo.service.UssService.UssFilter;
import org.zowe.zo.service.UssService.UssListing;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** z/OSMF z/OS UNIX files REST interface ({@code /zosmf/restfiles/fs}). */
@RestController
@RequestMapping("/zosmf/restfiles/fs")
@RequiredArgsConstructor
public class ZosmfUssController {

    private static final MediaType TEXT_UTF8 = MediaType.parseMediaType("text/plain;charset=UTF-8");

    private final UssService ussService;

    @GetMapping
    public ResponseEntity<Map<String, Object>> list(@RequestParam(value = "path", required = false) String path,
                                                    @RequestParam(value = "depth", required = false) String depth,
                                                    @RequestParam(value = "filesys", required = false) String filesys,
                                                    @RequestParam(value = "symlinks", required = false) String symlinks,
                                                    @ModelAttribute ListFilters filters,
                                                    @RequestHeader(value = "X-IBM-Max-Items", required = false) String maxItemsHeader) throws ZoRpcException {
        Integer maxItems = InputValidator.integer("X-IBM-Max-Items", maxItemsHeader);
        int depthValue = depth == null || depth.isBlank() ? 1 : InputValidator.integer("depth", depth);
        if (path == null || path.isEmpty()) throw new IllegalArgumentException("path query parameter is required");
        if (filesys != null && !filesys.equalsIgnoreCase("all")) throw InputValidator.notImplemented("filesys=" + filesys + " is not supported");
        if (symlinks != null && !symlinks.equalsIgnoreCase("report")) throw InputValidator.notImplemented("symlinks=" + symlinks + " is not supported");
        if (filters.getMtime() != null) throw InputValidator.notImplemented("The mtime filter is not supported");
        if (filters.getSize() != null) throw InputValidator.notImplemented("The size filter is not supported");
        if (maxItems != null && maxItems < 0) throw new IllegalArgumentException("X-IBM-Max-Items must not be negative");
        int max = maxItems == null ? UssService.DEFAULT_MAX_ITEMS : maxItems == 0 ? Integer.MAX_VALUE : maxItems;
        UssFilter filter = new UssFilter(filters.getName(), filters.getType(), filters.getUser(), filters.getGroup(), filters.getPerm());
        UssListing listing = ussService.list(path, depthValue, max, filter);
        List<Map<String, Object>> items = new ArrayList<>();
        for (UssEntry entry : listing.items()) items.add(toZosmf(entry));
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("items", items);
        body.put("returnedRows", items.size());
        body.put("totalRows", listing.totalRows());
        body.put("JSONversion", 1);
        return ResponseEntity.ok(body);
    }

    @GetMapping("/{*path}")
    public ResponseEntity<?> read(@PathVariable("path") String path,
                                  @RequestParam(value = "path", required = false) String pathParam,
                                  @RequestHeader(value = "X-IBM-Data-Type", required = false) String dataType,
                                  @RequestHeader(value = "If-None-Match", required = false) String ifNoneMatch,
                                  @RequestHeader(value = "X-IBM-Record-Range", required = false) String range,
                                  @RequestParam Map<String, String> query,
                                  @RequestHeader Map<String, String> headers) throws ZoRpcException {
        if ((path.isEmpty() || path.equals("/")) && pathParam != null) {
            ListFilters filters = new ListFilters();
            filters.setName(query.get("name"));
            filters.setType(query.get("type"));
            filters.setUser(query.get("user"));
            filters.setGroup(query.get("group"));
            filters.setPerm(query.get("perm"));
            filters.setMtime(query.get("mtime"));
            filters.setSize(query.get("size"));
            return list(pathParam, query.get("depth"), query.get("filesys"), query.get("symlinks"), filters,
                    headerIgnoreCase(headers, "X-IBM-Max-Items"));
        }
        Options options = UssService.parseDataType(dataType);
        if (range != null && options.isBinary()) throw InputValidator.notImplemented("X-IBM-Record-Range is not supported with binary data");
        Content content = ussService.read(path, options);
        HttpHeaders out = new HttpHeaders();
        out.setContentType(options.isBinary() ? MediaType.APPLICATION_OCTET_STREAM : TEXT_UTF8);
        if (content.etag() != null) {
            out.set(HttpHeaders.ETAG, quote(content.etag()));
            if (etagMatches(ifNoneMatch, content.etag())) return ResponseEntity.status(HttpStatus.NOT_MODIFIED).headers(out).build();
        }
        return ResponseEntity.ok().headers(out).body(ZosmfJobController.applyRange(content.data(), range));
    }

    @PutMapping(value = "/{*path}", consumes = "!" + MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Void> write(@PathVariable("path") String path,
                                      @RequestBody(required = false) byte[] data,
                                      @RequestHeader(value = "X-IBM-Data-Type", required = false) String dataType,
                                      @RequestHeader(value = "If-Match", required = false) String ifMatch) throws ZoRpcException {
        Options options = UssService.parseDataType(dataType);
        WriteResult result = ussService.write(path, data == null ? new byte[0] : data, options, stripEtag(ifMatch));
        ResponseEntity.BodyBuilder response = ResponseEntity.status(result.created() ? HttpStatus.CREATED : HttpStatus.NO_CONTENT);
        if (result.etag() != null) response.header(HttpHeaders.ETAG, quote(result.etag()));
        return response.build();
    }

    @PutMapping(value = "/{*path}", consumes = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<?> utility(@PathVariable("path") String path,
                                        @RequestBody Map<String, Object> body) throws ZoRpcException {
        Object req = body.get("request");
        if (!(req instanceof String name)) throw new IllegalArgumentException("'request' is required in the request body");
        boolean recursive = bool(body.get("recursive"));
        switch (name.toLowerCase(Locale.ROOT)) {
            case "chmod" -> ussService.chmod(path, str(body, "mode"), recursive);
            case "chown" -> ussService.chown(path, str(body, "owner"), str(body, "group"), recursive);
            case "chtag" -> {
                if ("list".equalsIgnoreCase(str(body, "action"))) return ResponseEntity.ok(Map.of("stdout", ussService.chtagList(path)));
                ussService.chtag(path, str(body, "action"), str(body, "type"), str(body, "codeset"), recursive);
            }
            case "move" -> ussService.move(source(body), path, bool(body.get("overwrite")));
            case "copy" -> {
                ussService.copy(source(body), path, new CopyOptions(recursive, false, preserve(body), bool(body.get("overwrite"))));
                return ResponseEntity.status(HttpStatus.CREATED).build();
            }
            default -> throw InputValidator.notImplemented("Utility request '" + name + "' is not supported");
        }
        return ResponseEntity.ok().build();
    }

    @PostMapping("/{*path}")
    public ResponseEntity<Void> create(@PathVariable("path") String path,
                                       @RequestBody Map<String, Object> body) throws ZoRpcException {
        String type = str(body, "type");
        if (type == null) throw new IllegalArgumentException("'type' is required in the request body");
        boolean directory = switch (type.toLowerCase(Locale.ROOT)) {
            case "file" -> false;
            case "dir", "directory" -> true;
            default -> throw new IllegalArgumentException("Invalid type: " + type);
        };
        ussService.create(path, directory, str(body, "mode"));
        return ResponseEntity.status(HttpStatus.CREATED).build();
    }

    @DeleteMapping("/{*path}")
    public ResponseEntity<Void> delete(@PathVariable("path") String path,
                                       @RequestHeader(value = "X-IBM-Option", required = false) String option) throws ZoRpcException {
        boolean recursive = false;
        if (option != null && !option.isBlank()) {
            if (!option.trim().equalsIgnoreCase("recursive")) throw new IllegalArgumentException("Invalid X-IBM-Option: " + option);
            recursive = true;
        }
        ussService.delete(path, recursive);
        return ResponseEntity.noContent().build();
    }

    /** Query parameters of the z/OSMF list request that filter entries. */
    @lombok.Data
    public static class ListFilters {
        private String name;
        private String type;
        private String user;
        private String group;
        private String perm;
        private String mtime;
        private String size;
    }

    private static Map<String, Object> toZosmf(UssEntry e) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("name", e.name());
        if (e.mode() != null) m.put("mode", e.mode());
        if (e.size() != null) m.put("size", e.size());
        if (e.user() != null) m.put("user", e.user());
        if (e.group() != null) m.put("group", e.group());
        if (e.mtime() != null) m.put("mtime", e.mtime());
        return m;
    }

    private static String source(Map<String, Object> body) throws ZoRpcException {
        Object from = body.get("from");
        if (from instanceof String s && s.startsWith("/")) return s;
        if (from == null) throw new IllegalArgumentException("'from' is required in the request body");
        throw InputValidator.notImplemented("Only USS paths are supported as 'from'");
    }

    private static boolean preserve(Map<String, Object> body) throws ZoRpcException {
        String preserve = str(body, "preserve");
        if (preserve == null || preserve.equalsIgnoreCase("none")) return false;
        if (preserve.equalsIgnoreCase("all")) return true;
        throw InputValidator.notImplemented("preserve=" + preserve + " is not supported");
    }

    private static String str(Map<String, Object> body, String key) {
        Object v = body.get(key);
        return v == null ? null : v.toString();
    }

    private static boolean bool(Object v) {
        return v instanceof Boolean b ? b : v != null && Boolean.parseBoolean(v.toString());
    }

    private static String headerIgnoreCase(Map<String, String> headers, String name) {
        for (Map.Entry<String, String> e : headers.entrySet()) if (e.getKey().equalsIgnoreCase(name)) return e.getValue();
        return null;
    }

    private static String quote(String etag) {
        return "\"" + stripEtag(etag) + "\"";
    }

    static String stripEtag(String raw) {
        if (raw == null || raw.isBlank()) return null;
        String v = raw.trim();
        if (v.startsWith("W/")) v = v.substring(2);
        if (v.length() >= 2 && v.startsWith("\"") && v.endsWith("\"")) v = v.substring(1, v.length() - 1);
        return v;
    }

    static boolean etagMatches(String header, String etag) {
        if (header == null || header.isBlank()) return false;
        if (header.trim().equals("*")) return true;
        String current = stripEtag(etag);
        for (String candidate : header.split(",")) {
            if (current != null && current.equals(stripEtag(candidate))) return true;
        }
        return false;
    }
}
