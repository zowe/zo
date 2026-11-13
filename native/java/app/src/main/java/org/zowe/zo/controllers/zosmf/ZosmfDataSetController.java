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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.HttpServletRequest;
import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.DataSetNames;
import org.zowe.zo.service.ContentCharsets;
import org.zowe.zo.service.DataSetService;
import org.zowe.zo.service.DataSetService.CopySource;
import org.zowe.zo.service.DataSetService.CreateSpec;
import org.zowe.zo.service.DataSetService.DataSetInfo;
import org.zowe.zo.service.DataSetService.ListPage;
import org.zowe.zo.service.DataSetService.ListQuery;
import org.zowe.zo.service.DataSetService.MemberInfo;
import org.zowe.zo.service.DataSetService.Target;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

/** z/OSMF-compatible "z/OS data set and file REST interface" for data sets. */
@RestController
@RequestMapping("/zosmf/restfiles")
@RequiredArgsConstructor
public class ZosmfDataSetController {

    public static final String TRUNCATION_HEADER = "X-Zo-Truncation-Warning";

    private static final String VOLUME_PATH = "{vol:-\\(.+\\)}";
    private static final int DEFAULT_MAX_ITEMS = 1000;
    private static final String NO_EXPIRATION = "***None***";
    private static final Pattern NAME = Pattern.compile("^([^()]+)(?:\\(([^()]*)\\))?$");
    private static final Pattern VOLUME = Pattern.compile("^-\\((.+)\\)$");
    private static final MediaType TEXT_UTF8 = new MediaType("text", "plain", StandardCharsets.UTF_8);

    private final DataSetService dataSetService;
    private final ObjectMapper objectMapper;

    private record DataType(boolean binary, String encoding) {
        ZoContentIo.Options toOptions(String localEncoding) {
            return new ZoContentIo.Options(binary ? ZoContentIo.BINARY : encoding, binary ? null : localEncoding, null);
        }
    }

    private static ZoRpcException unsupported(String what) {
        return new ZoRpcException("zosmf", ZoRpcException.METHOD_NOT_FOUND,
                what + " is not supported by this server", null);
    }

    // ---------------------------------------------------------------- list

    @GetMapping("/ds")
    public Map<String, Object> listDataSets(@RequestParam(value = "dslevel", required = false) String dslevel,
                                            @RequestParam(value = "volser", required = false) String volser,
                                            @RequestParam(value = "start", required = false) String start,
                                            HttpServletRequest request) throws ZoRpcException {
        if (dslevel == null || dslevel.isEmpty()) {
            throw new IllegalArgumentException("dslevel parameter is required");
        }
        Set<String> attributes = attributeTokens(request);
        boolean base = attributes.contains("base");
        boolean vol = attributes.contains("vol");
        ListQuery query = new ListQuery(null, blankToNull(start), maxItems(request), base || vol,
                attributes.contains("total"));
        ListPage<DataSetInfo> page = dataSetService.listDataSets(dslevel, blankToNull(volser), query);
        List<Map<String, Object>> items = page.items().stream()
                .map(info -> base ? baseItem(info) : vol ? volItem(info) : nameItem(info))
                .toList();
        return pageBody(items, page.moreRows(), page.totalRows(), query.total());
    }

    @GetMapping("/ds/{dsn}/member")
    public Map<String, Object> listMembers(@PathVariable("dsn") String dsn,
                                           @RequestParam(value = "pattern", required = false) String pattern,
                                           @RequestParam(value = "start", required = false) String start,
                                           HttpServletRequest request) throws ZoRpcException {
        Set<String> attributes = attributeTokens(request);
        boolean base = attributes.contains("base");
        ListQuery query = new ListQuery(null, blankToNull(start), maxItems(request), base,
                attributes.contains("total"));
        ListPage<MemberInfo> page = dataSetService.listMembers(dsn, blankToNull(pattern), query);
        List<Map<String, Object>> items = page.items().stream().map(m -> memberItem(m, base)).toList();
        return pageBody(items, page.moreRows(), page.totalRows(), query.total());
    }

    private static Map<String, Object> pageBody(List<Map<String, Object>> items, boolean more, Integer total,
                                                boolean wantTotal) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("items", items);
        body.put("returnedRows", items.size());
        body.put("moreRows", more);
        if (wantTotal && more && total != null) {
            body.put("totalRows", total);
        }
        body.put("JSONversion", 1);
        return body;
    }

    private static Set<String> attributeTokens(HttpServletRequest request) {
        String header = request.getHeader("X-IBM-Attributes");
        if (header == null) {
            return Set.of();
        }
        return Arrays.stream(header.split(",")).map(s -> s.trim().toLowerCase(Locale.ROOT))
                .filter(s -> !s.isEmpty()).collect(Collectors.toSet());
    }

    private static int maxItems(HttpServletRequest request) {
        String header = request.getHeader("X-IBM-Max-Items");
        if (header == null) {
            return DEFAULT_MAX_ITEMS;
        }
        try {
            int value = Integer.parseInt(header.trim());
            return value < 0 ? DEFAULT_MAX_ITEMS : value;
        } catch (NumberFormatException e) {
            return DEFAULT_MAX_ITEMS;
        }
    }

    private static String blankToNull(String s) {
        return s == null || s.isEmpty() ? null : s;
    }

    private static void put(Map<String, Object> map, String key, Object value) {
        if (value != null) {
            map.put(key, String.valueOf(value));
        }
    }

    private static Map<String, Object> nameItem(DataSetInfo info) {
        Map<String, Object> item = new LinkedHashMap<>();
        item.put("dsname", info.name());
        return item;
    }

    private static Map<String, Object> volItem(DataSetInfo info) {
        Map<String, Object> item = nameItem(info);
        put(item, "vol", info.volser());
        return item;
    }

    private static Map<String, Object> baseItem(DataSetInfo info) {
        Map<String, Object> item = nameItem(info);
        put(item, "blksz", info.blksize());
        put(item, "cdate", info.cdate());
        put(item, "dev", info.devtype() == null ? null : info.devtype().replaceFirst("^0[xX]", ""));
        put(item, "dsntp", info.dsntype());
        put(item, "dsorg", info.dsorg());
        put(item, "edate", info.edate() == null || info.edate().isEmpty() ? (info.cdate() == null ? null : NO_EXPIRATION)
                : info.edate());
        put(item, "extx", info.allocx());
        put(item, "lrecl", info.lrecl());
        put(item, "migr", info.migrated() == null ? null : info.migrated() ? "YES" : "NO");
        put(item, "mvol", info.multivolume() == null ? null : info.multivolume() ? "Y" : "N");
        put(item, "rdate", info.rdate());
        put(item, "recfm", info.recfm());
        put(item, "sizex", info.alloc());
        put(item, "spacu", info.spacu());
        put(item, "used", info.usedp());
        put(item, "vol", info.volser());
        put(item, "vols", info.volsers() == null ? null : String.join(",", info.volsers()));
        return item;
    }

    private static Map<String, Object> memberItem(MemberInfo m, boolean base) {
        Map<String, Object> item = new LinkedHashMap<>();
        item.put("member", m.name());
        if (!base) {
            return item;
        }
        putRaw(item, "vers", m.vers());
        putRaw(item, "mod", m.mod());
        putRaw(item, "c4date", m.c4date());
        putRaw(item, "m4date", m.m4date());
        putRaw(item, "mtime", m.mtime());
        putRaw(item, "cnorc", m.cnorc());
        putRaw(item, "inorc", m.inorc());
        putRaw(item, "mnorc", m.mnorc());
        putRaw(item, "user", m.user());
        putRaw(item, "sclm", m.sclm());
        return item;
    }

    private static void putRaw(Map<String, Object> map, String key, Object value) {
        if (value != null) {
            map.put(key, value);
        }
    }

    // ------------------------------------------------------------- content

    private static Target target(String name, String vol) {
        Matcher m = NAME.matcher(name);
        if (!m.matches()) {
            throw new IllegalArgumentException("Invalid data set name '" + name + "'");
        }
        String volser = null;
        if (vol != null) {
            Matcher v = VOLUME.matcher(vol);
            if (!v.matches()) {
                throw new IllegalArgumentException("Invalid volume specification '" + vol + "'");
            }
            volser = v.group(1);
        }
        return Target.of(m.group(1), m.group(2), volser);
    }

    private static DataType dataType(HttpServletRequest request) throws ZoRpcException {
        String header = request.getHeader("X-IBM-Data-Type");
        if (header == null || header.isBlank()) {
            return new DataType(false, null);
        }
        String[] parts = header.split(";");
        String kind = parts[0].trim().toLowerCase(Locale.ROOT);
        String encoding = null;
        for (int i = 1; i < parts.length; i++) {
            String[] kv = parts[i].split("=", 2);
            if (kv.length == 2 && kv[0].trim().equalsIgnoreCase("fileEncoding")) {
                encoding = DataSetNames.codepage(kv[1].trim());
            }
        }
        return switch (kind) {
            case "text" -> new DataType(false, encoding);
            case "binary" -> new DataType(true, null);
            case "record" -> throw unsupported("Record data type");
            default -> throw new IllegalArgumentException("Invalid X-IBM-Data-Type '" + header + "'");
        };
    }

    private static String etagValue(String header) {
        String value = header.trim();
        if (value.startsWith("W/")) {
            value = value.substring(2);
        }
        if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
            value = value.substring(1, value.length() - 1);
        }
        return value;
    }

    private static String quoted(String etag) {
        return etag.startsWith("\"") ? etag : "\"" + etag + "\"";
    }

    private static boolean matchesEtag(String ifNoneMatch, String etag) {
        return Arrays.stream(ifNoneMatch.split(",")).map(ZosmfDataSetController::etagValue)
                .anyMatch(v -> v.equals("*") || v.equals(etag));
    }

    private static String charsetOrDefault(String declared, byte[] body) {
        return declared != null ? declared : ContentCharsets.defaultFor(body);
    }

    /** Accepts "text/plain;charset=X" as well as the bare codepage name some Zowe clients send. */
    private static String requestCharset(HttpServletRequest request) {
        String header = request.getContentType();
        if (header == null || header.isBlank()) {
            return null;
        }
        for (String part : header.split(";")) {
            String p = part.trim();
            if (p.toLowerCase(Locale.ROOT).startsWith("charset=")) {
                return DataSetNames.codepage(p.substring(8).replace("\"", ""));
            }
        }
        return header.contains("/") || header.contains(";") ? null : DataSetNames.codepage(header.trim());
    }

    @GetMapping("/ds/{name}")
    public ResponseEntity<byte[]> readDataSet(@PathVariable("name") String name, HttpServletRequest request)
            throws ZoRpcException {
        return read(name, null, request);
    }

    @GetMapping("/ds/" + VOLUME_PATH + "/{name}")
    public ResponseEntity<byte[]> readDataSetOnVolume(@PathVariable("vol") String vol,
                                                      @PathVariable("name") String name,
                                                      HttpServletRequest request) throws ZoRpcException {
        return read(name, vol, request);
    }

    private ResponseEntity<byte[]> read(String name, String vol, HttpServletRequest request) throws ZoRpcException {
        if (request.getParameter("search") != null || request.getParameter("research") != null) {
            throw unsupported("Searching data set content through GET");
        }
        DataType type = dataType(request);
        ZoContentIo.Content content = dataSetService.read(target(name, vol), type.toOptions(null));
        String ifNoneMatch = request.getHeader("If-None-Match");
        ResponseEntity.BodyBuilder response = ResponseEntity.status(HttpStatus.OK);
        if (content.etag() != null) {
            response.header("ETag", quoted(content.etag()));
            if (ifNoneMatch != null && matchesEtag(ifNoneMatch, content.etag())) {
                return ResponseEntity.status(HttpStatus.NOT_MODIFIED).header("ETag", quoted(content.etag())).build();
            }
        }
        return response.contentType(type.binary() ? MediaType.APPLICATION_OCTET_STREAM : TEXT_UTF8)
                .body(content.data());
    }

    @PutMapping("/ds/{name}")
    public ResponseEntity<Void> putDataSet(@PathVariable("name") String name, HttpServletRequest request)
            throws ZoRpcException, IOException {
        return put(name, null, request);
    }

    @PutMapping("/ds/" + VOLUME_PATH + "/{name}")
    public ResponseEntity<Void> putDataSetOnVolume(@PathVariable("vol") String vol,
                                                   @PathVariable("name") String name,
                                                   HttpServletRequest request) throws ZoRpcException, IOException {
        return put(name, vol, request);
    }

    private ResponseEntity<Void> put(String name, String vol, HttpServletRequest request)
            throws ZoRpcException, IOException {
        String contentType = request.getContentType();
        if (contentType != null && contentType.toLowerCase(Locale.ROOT).startsWith(MediaType.APPLICATION_JSON_VALUE)) {
            utility(target(name, null), request.getInputStream().readAllBytes());
            return ResponseEntity.ok().build();
        }
        DataType type = dataType(request);
        String ifMatch = request.getHeader("If-Match");
        String etag = ifMatch == null || etagValue(ifMatch).equals("*") ? null : etagValue(ifMatch);
        byte[] body = request.getInputStream().readAllBytes();
        ZoContentIo.WriteResult result = dataSetService.write(target(name, vol), body,
                type.toOptions(type.binary() ? null : charsetOrDefault(requestCharset(request), body)), etag);
        ResponseEntity.BodyBuilder response = ResponseEntity.status(result.created() ? HttpStatus.CREATED
                : HttpStatus.NO_CONTENT);
        if (result.etag() != null) {
            response.header("ETag", quoted(result.etag()));
        }
        if (result.truncationWarning() != null && !result.truncationWarning().isBlank()) {
            response.header(TRUNCATION_HEADER, result.truncationWarning().replaceAll("[\\r\\n]+", " ").trim());
        }
        return response.build();
    }

    // ----------------------------------------------------------- utilities

    private void utility(Target target, byte[] body) throws ZoRpcException {
        JsonNode json;
        try {
            json = objectMapper.readTree(body);
        } catch (IOException e) {
            throw new IllegalArgumentException("Request body is not valid JSON");
        }
        if (json == null || !json.isObject() || !json.path("request").isTextual()) {
            throw new IllegalArgumentException("Request body must be a JSON object with a 'request' property");
        }
        switch (json.get("request").asText().toLowerCase(Locale.ROOT)) {
            case "rename" -> rename(target, json.path("from-dataset"));
            case "copy" -> copy(target, json);
            case "hrecall" -> {
                if (target.member() != null) {
                    throw new IllegalArgumentException("hrecall applies to a data set, not a member");
                }
                dataSetService.recall(target.dsname());
            }
            case "hmigrate", "hdelete" -> throw unsupported("HSM migrate/delete requests");
            default -> throw new IllegalArgumentException("Unsupported request '" + json.get("request").asText() + "'");
        }
    }

    private static String textField(JsonNode node, String field) {
        JsonNode value = node.get(field);
        if (value == null || value.isNull()) {
            return null;
        }
        if (!value.isTextual()) {
            throw new IllegalArgumentException(field + " must be a string");
        }
        return value.asText();
    }

    private void rename(Target to, JsonNode from) throws ZoRpcException {
        String fromDsn = textField(from, "dsn");
        String fromMember = textField(from, "member");
        if (fromDsn == null) {
            throw new IllegalArgumentException("from-dataset.dsn is required");
        }
        if (to.member() == null && fromMember == null) {
            dataSetService.renameDataSet(fromDsn, to.dsname());
        } else if (to.member() != null && fromMember != null) {
            if (!DataSetNames.dsn(fromDsn).equals(to.dsname())) {
                throw new IllegalArgumentException("Member rename must stay within the same data set");
            }
            dataSetService.renameMember(to.dsname(), fromMember, to.member());
        } else {
            throw new IllegalArgumentException("Cannot rename between a data set and a member");
        }
    }

    private void copy(Target to, JsonNode json) throws ZoRpcException {
        if (json.hasNonNull("from-file")) {
            throw unsupported("Copying from a z/OS UNIX file");
        }
        JsonNode from = json.path("from-dataset");
        String fromDsn = textField(from, "dsn");
        String fromMember = textField(from, "member");
        if (fromDsn == null) {
            throw new IllegalArgumentException("from-dataset.dsn is required");
        }
        if ("*".equals(fromMember)) {
            throw unsupported("Copying all members with '*'");
        }
        dataSetService.copy(new CopySource(fromDsn, fromMember), to, json.path("replace").asBoolean(false));
    }

    @PutMapping("/ams")
    public void accessMethodServices() throws ZoRpcException {
        throw unsupported("Access Method Services (AMS)");
    }

    // --------------------------------------------------------- create, delete

    @PostMapping("/ds/{dsn}")
    public ResponseEntity<Void> createDataSet(@PathVariable("dsn") String dsn, HttpServletRequest request)
            throws ZoRpcException, IOException {
        Target target = target(dsn, null);
        if (target.member() != null) {
            throw new IllegalArgumentException("A data set is created without a member name");
        }
        byte[] body = request.getInputStream().readAllBytes();
        JsonNode json;
        try {
            json = body.length == 0 ? objectMapper.createObjectNode() : objectMapper.readTree(body);
        } catch (IOException e) {
            throw new IllegalArgumentException("Request body is not valid JSON");
        }
        if (json == null || !json.isObject()) {
            throw new IllegalArgumentException("Request body must be a JSON object");
        }
        dataSetService.createDataSet(target.dsname(), createSpec(json));
        return ResponseEntity.status(HttpStatus.CREATED).build();
    }

    private static Integer intField(JsonNode node, String field) {
        JsonNode value = node.get(field);
        if (value == null || value.isNull()) {
            return null;
        }
        if (value.isIntegralNumber() && value.canConvertToInt()) {
            return value.intValue();
        }
        if (value.isTextual() && value.asText().matches("\\d{1,9}")) {
            return Integer.valueOf(value.asText());
        }
        throw new IllegalArgumentException(field + " must be a whole number");
    }

    private static CreateSpec createSpec(JsonNode json) {
        String dsntype = textField(json, "dsntype");
        return new CreateSpec(textField(json, "volser"), textField(json, "unit"), textField(json, "dsorg"),
                textField(json, "alcunit"), intField(json, "primary"), intField(json, "secondary"),
                intField(json, "dirblk"), intField(json, "avgblk"), textField(json, "recfm"),
                intField(json, "blksize"), intField(json, "lrecl"), textField(json, "storclass"),
                textField(json, "mgntclass"), textField(json, "dataclass"),
                "INHERIT".equalsIgnoreCase(dsntype) ? null : dsntype, textField(json, "like"));
    }

    @DeleteMapping("/ds/{name}")
    public ResponseEntity<Void> deleteDataSet(@PathVariable("name") String name) throws ZoRpcException {
        dataSetService.delete(target(name, null));
        return ResponseEntity.noContent().build();
    }

    @DeleteMapping("/ds/" + VOLUME_PATH + "/{name}")
    public ResponseEntity<Void> deleteDataSetOnVolume(@PathVariable("vol") String vol,
                                                      @PathVariable("name") String name) throws ZoRpcException {
        dataSetService.delete(target(name, vol));
        return ResponseEntity.noContent().build();
    }
}
