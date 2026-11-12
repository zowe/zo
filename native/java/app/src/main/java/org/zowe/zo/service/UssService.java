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
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoContentIo.Content;
import org.zowe.zo.ffm.ZoContentIo.Options;
import org.zowe.zo.ffm.ZoContentIo.WriteResult;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;

@Service
@RequiredArgsConstructor
public class UssService {

    public static final int DEFAULT_MAX_ITEMS = 1000;

    private final ZoRpcClient rpc;
    private final ZoContentIo contentIo;

    public record UssEntry(String name, String mode, Long links, String user, String group, Long size,
                           String filetag, String mtime) {
    }

    public record UssListing(List<UssEntry> items, int totalRows) {
    }

    /** Cheap, exact list filters; null fields do not filter. */
    public record UssFilter(String name, String type, String user, String group, String perm) {
        public static final UssFilter NONE = new UssFilter(null, null, null, null, null);
    }

    public UssListing list(String path, int depth, int maxItems, UssFilter filter) throws ZoRpcException {
        if (depth < 0) throw new IllegalArgumentException("depth must not be negative");
        if (maxItems < 1) throw new IllegalArgumentException("X-IBM-Max-Items must be a positive integer");
        Matcher matcher = Matcher.of(filter);
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        params.put("long", true);
        params.put("all", true);
        params.put("depth", depth);
        JsonNode result = callOrNotFound("listFiles", params);
        List<UssEntry> matched = new ArrayList<>();
        for (JsonNode n : result.path("items")) {
            UssEntry entry = new UssEntry(JobService.text(n, "name"), JobService.text(n, "mode"),
                    n.has("links") ? n.get("links").asLong() : null, JobService.text(n, "user"),
                    JobService.text(n, "group"), n.has("size") ? n.get("size").asLong() : null,
                    JobService.text(n, "filetag"), JobService.text(n, "mtime"));
            if (matcher.accepts(entry)) matched.add(entry);
        }
        int total = matched.size();
        return new UssListing(total > maxItems ? List.copyOf(matched.subList(0, maxItems)) : List.copyOf(matched), total);
    }

    public Content read(String path, Options options) throws ZoRpcException {
        return contentIo.readFile(InputValidator.path(path), options);
    }

    public WriteResult write(String path, byte[] data, Options options, String etag) throws ZoRpcException {
        return contentIo.writeFile(InputValidator.path(path), data, options, etag);
    }

    public void create(String path, boolean directory, String mode) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        params.put("isDir", directory);
        if (mode != null) params.put("permissions", InputValidator.mode(mode));
        rpc.call("createFile", params);
    }

    public void delete(String path, boolean recursive) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        if (recursive) params.put("recursive", true);
        callOrNotFound("deleteFile", params);
    }

    public void chmod(String path, String mode, boolean recursive) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        params.put("mode", InputValidator.mode(mode));
        if (recursive) params.put("recursive", true);
        callOrNotFound("chmodFile", params);
    }

    public void chown(String path, String owner, String group, boolean recursive) throws ZoRpcException {
        if (owner == null || owner.isEmpty()) {
            if (group == null || group.isEmpty()) throw new IllegalArgumentException("owner is required");
            throw InputValidator.notImplemented("Changing only the group is not supported; supply owner as well");
        }
        String target = InputValidator.owner(owner) + (group == null || group.isEmpty() ? "" : ":" + InputValidator.group(group));
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        params.put("owner", target);
        if (recursive) params.put("recursive", true);
        callOrNotFound("chownFile", params);
    }

    /** z/OSMF-style chtag: set (text or binary) or remove. */
    public void chtag(String path, String action, String type, String codeset, boolean recursive) throws ZoRpcException {
        String act = action == null ? "set" : action.toLowerCase(Locale.ROOT);
        String tag;
        switch (act) {
            case "remove" -> tag = "untagged";
            case "list" -> throw new IllegalArgumentException("chtag action 'list' returns output; use chtagList");
            case "set" -> {
                String kind = type == null ? "text" : type.toLowerCase(Locale.ROOT);
                tag = switch (kind) {
                    case "binary" -> "binary";
                    case "text" -> codeset == null || codeset.isEmpty() ? "IBM-1047" : InputValidator.codeset(codeset);
                    case "mixed" -> throw InputValidator.notImplemented("chtag type 'mixed' is not supported");
                    default -> throw new IllegalArgumentException("Invalid chtag type: " + kind);
                };
            }
            default -> throw new IllegalArgumentException("Invalid chtag action: " + act);
        }
        chtagRaw(path, tag, recursive);
    }

    /** z/OSMF-style {@code chtag -p} output lines ({@code t IBM-1047 T=on /path}), built from the listing's file tags. */
    public List<String> chtagList(String path) throws ZoRpcException {
        String base = InputValidator.path(path);
        String baseName = base.substring(base.lastIndexOf('/') + 1);
        UssListing listing = list(base, 1, Integer.MAX_VALUE, UssFilter.NONE);
        List<String> lines = new ArrayList<>();
        for (UssEntry e : listing.items()) {
            String tag = e.filetag() == null ? "untagged" : e.filetag();
            String kind = tag.equals("binary") ? "b" : tag.equals("untagged") ? "-" : "t";
            String name = e.name() == null ? "" : e.name();
            String full = name.startsWith("/") ? name : listing.items().size() == 1 && name.equals(baseName) ? base : base + "/" + name;
            lines.add(String.format("%s %-12s T=%s  %s", kind, tag, kind.equals("t") ? "on" : "off", full));
        }
        return lines;
    }

    /** Native chtag: tag is a CCSID number or a display name such as IBM-1047, binary or untagged. */
    public void chtagRaw(String path, String tag, boolean recursive) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        params.put("tag", InputValidator.codeset(tag));
        if (recursive) params.put("recursive", true);
        callOrNotFound("chtagFile", params);
    }

    public record CopyOptions(boolean recursive, boolean followSymlinks, boolean preserveAttributes, boolean force) {
    }

    public void copy(String from, String to, CopyOptions options) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("srcFsPath", InputValidator.path(from));
        params.put("dstFsPath", InputValidator.path(to));
        if (options.recursive()) params.put("recursive", true);
        if (options.followSymlinks()) params.put("followSymlinks", true);
        if (options.preserveAttributes()) params.put("preserveAttributes", true);
        if (options.force()) params.put("force", true);
        callOrNotFound("copyUss", params);
    }

    /**
     * z/OSMF copy semantics: with overwrite=false an existing target must be left untouched.
     * zo's copy (cp) replaces writable targets even without force, so check first.
     */
    public void copyZosmf(String from, String to, CopyOptions options) throws ZoRpcException {
        if (!options.force() && exists(to)) {
            throw new ZoRpcException("copyUss", ZoRpcException.CONFLICT, "Target path already exists", null);
        }
        copy(from, to, options);
    }

    private boolean exists(String path) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("fspath", InputValidator.path(path));
        try {
            callOrNotFound("listFiles", params);
            return true;
        } catch (ZoRpcException e) {
            if (e.getCode() == ZoRpcException.NOT_FOUND) return false;
            throw e;
        }
    }

    public void move(String from, String to, boolean overwrite) throws ZoRpcException {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("source", InputValidator.path(from));
        params.put("target", InputValidator.path(to));
        params.put("force", overwrite);
        callOrNotFound("moveFile", params);
    }

    /** Parses z/OSMF's {@code X-IBM-Data-Type: text[;fileEncoding=CP] | binary}. */
    public static Options parseDataType(String header) throws ZoRpcException {
        if (header == null || header.isBlank()) return Options.DEFAULT;
        String[] parts = header.split(";");
        String type = parts[0].trim().toLowerCase(Locale.ROOT);
        switch (type) {
            case "binary" -> {
                return new Options(ZoContentIo.BINARY, null, null);
            }
            case "text" -> {
                for (int i = 1; i < parts.length; i++) {
                    String[] kv = parts[i].trim().split("=", 2);
                    if (kv.length == 2 && kv[0].trim().equalsIgnoreCase("fileEncoding")) {
                        return new Options(InputValidator.codeset(kv[1].trim()), null, null);
                    }
                }
                return Options.DEFAULT;
            }
            default -> throw InputValidator.notImplemented("X-IBM-Data-Type '" + type + "' is not supported");
        }
    }

    private JsonNode callOrNotFound(String method, Object params) throws ZoRpcException {
        try {
            return rpc.call(method, params);
        } catch (ZoRpcException e) {
            String details = e.getDetails();
            String text = ((e.getMessage() == null ? "" : e.getMessage()) + (details == null ? "" : details)).toLowerCase(Locale.ROOT);
            if (e.getCode() != ZoRpcException.NOT_FOUND
                    && (text.contains("no such file or directory") || text.contains("does not exist"))) {
                throw InputValidator.notFound(method, "Path was not found");
            }
            throw e;
        }
    }

    private record Matcher(Pattern name, String type, String user, String group, String perm) {

        static Matcher of(UssFilter f) {
            Pattern name = null;
            if (f.name() != null && !f.name().isEmpty()) {
                if (f.name().length() > 255) throw new IllegalArgumentException("name filter is too long");
                StringBuilder regex = new StringBuilder();
                for (char c : f.name().toCharArray()) {
                    switch (c) {
                        case '*' -> regex.append(".*");
                        case '?' -> regex.append('.');
                        default -> regex.append(Pattern.quote(String.valueOf(c)));
                    }
                }
                name = Pattern.compile(regex.toString());
            }
            String type = null;
            if (f.type() != null && !f.type().isEmpty()) {
                if (!f.type().matches("[cdflps]")) throw new IllegalArgumentException("Invalid type filter: use one of c, d, f, l, p, s");
                type = f.type().equals("f") ? "-" : f.type();
            }
            String perm = null;
            if (f.perm() != null && !f.perm().isEmpty()) perm = octalToSymbolic(InputValidator.mode(f.perm()));
            return new Matcher(name, type, f.user(), f.group(), perm);
        }

        boolean accepts(UssEntry e) {
            if (name != null && (e.name() == null || !name.matcher(baseName(e.name())).matches())) return false;
            String mode = e.mode();
            if (type != null && (mode == null || mode.isEmpty() || !String.valueOf(mode.charAt(0)).equals(type))) return false;
            if (perm != null && (mode == null || mode.length() < 10 || !mode.substring(1, 10).equals(perm))) return false;
            if (user != null && !user.equals(e.user())) return false;
            return group == null || group.equals(e.group());
        }

        private static String baseName(String name) {
            int slash = name.lastIndexOf('/');
            return slash >= 0 ? name.substring(slash + 1) : name;
        }

        private static String octalToSymbolic(String octal) {
            String digits = octal.substring(octal.length() - 3);
            StringBuilder out = new StringBuilder();
            for (char d : digits.toCharArray()) {
                int v = d - '0';
                out.append((v & 4) != 0 ? 'r' : '-').append((v & 2) != 0 ? 'w' : '-').append((v & 1) != 0 ? 'x' : '-');
            }
            return out.toString();
        }
    }
}
