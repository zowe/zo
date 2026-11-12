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

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoContentIo;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Data set operations on top of the zo RPC contract (packages/sdk/src/doc/rpc/ds.ts).
 * All input is validated here, so callers may pass raw user input.
 */
@Service
public class DataSetService {

    /** Upper bound for a single native listing when the caller wants everything. */
    public static final int MAX_FETCH = 100_000;

    private static final Set<String> ALLOC_UNITS = Set.of("CYL", "TRK", "CYLINDERS", "TRACKS");
    private static final Set<String> DSORGS = Set.of("PS", "PO", "PO-E", "POE");
    private static final Set<String> DSNTYPES = Set.of("BASIC", "EXTPREF", "EXTREQ", "LARGE", "PDS", "LIBRARY");
    private static final int MAX_SPACE = 16_777_215;

    private final ZoRpcClient rpc;
    private final ZoContentIo contentIo;

    public DataSetService(ZoRpcClient rpc, ZoContentIo contentIo) {
        this.rpc = rpc;
        this.contentIo = contentIo;
    }

    @JsonInclude(JsonInclude.Include.NON_NULL)
    public record DataSetInfo(String name, Long alloc, Integer allocx, Integer blksize, String cdate, String dataclass,
                              String devtype, String dsntype, String dsorg, String edate, Boolean encrypted,
                              Integer lrecl, String mgmtclass, Boolean migrated, Boolean multivolume, Long primary,
                              String rdate, String recfm, Long secondary, String spacu, String storclass,
                              Integer usedp, Integer usedx, String volser, List<String> volsers) {
    }

    @JsonInclude(JsonInclude.Include.NON_NULL)
    public record MemberInfo(String name, String user, Integer vers, Integer mod, String c4date, String m4date,
                             String mtime, Integer cnorc, Integer inorc, Integer mnorc, String sclm) {
    }

    /** moreRows is true when matches beyond the page limit exist; totalRows is only known when counted. */
    public record ListPage<T>(List<T> items, boolean moreRows, Integer totalRows) {
    }

    /** A data set, or one of its members, optionally on an explicit volume. */
    public record Target(String dsname, String member, String volser) {
        public static Target of(String dsname, String member, String volser) {
            return new Target(DataSetNames.dsn(dsname),
                    member == null ? null : DataSetNames.member(member),
                    volser == null ? null : DataSetNames.volser(volser));
        }

        public String fullName() {
            return member == null ? dsname : dsname + "(" + member + ")";
        }
    }

    /** Unvalidated create attributes as supplied by a client. */
    public record CreateSpec(String volser, String unit, String dsorg, String alcunit, Integer primary,
                             Integer secondary, Integer dirblk, Integer avgblk, String recfm, Integer blksize,
                             Integer lrecl, String storclass, String mgntclass, String dataclass, String dsntype,
                             String like) {
    }

    /** Source of a copy; member is null for a whole data set. */
    public record CopySource(String dsname, String member) {
    }

    public record ListQuery(String pattern, String start, int maxItems, boolean attributes, boolean total) {
    }

    @FunctionalInterface
    private interface Call<T> {
        T run() throws ZoRpcException;
    }

    /** ThreadSecurityAspect wraps ffm exceptions in RuntimeException; unwrap them again. */
    private static <T> T guarded(Call<T> call) throws ZoRpcException {
        try {
            return call.run();
        } catch (ZoRpcException e) {
            throw e;
        } catch (RuntimeException e) {
            for (Throwable t = e; t != null; t = t.getCause() == t ? null : t.getCause()) {
                if (t instanceof ZoRpcException zre) {
                    throw zre;
                }
            }
            throw e;
        }
    }

    private JsonNode call(String method, Map<String, Object> params) throws ZoRpcException {
        return guarded(() -> rpc.call(method, params));
    }

    private static Map<String, Object> params(Object... keysAndValues) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < keysAndValues.length; i += 2) {
            if (keysAndValues[i + 1] != null) {
                map.put((String) keysAndValues[i], keysAndValues[i + 1]);
            }
        }
        return map;
    }

    private static boolean isStructural(ZoRpcException e) {
        return e.getCode() < 0;
    }

    private ZoRpcException notFound(String method, String what) {
        return new ZoRpcException(method, ZoRpcException.NOT_FOUND, what + " not found", null);
    }

    /** Converts a handler failure into NOT_FOUND when the target is in fact missing. */
    private ZoRpcException resolveFailure(ZoRpcException e, String dsname, String member) throws ZoRpcException {
        if (isStructural(e)) {
            return e;
        }
        if (!dataSetExists(dsname)) {
            return notFound(e.getMethod(), "Data set '" + dsname + "'");
        }
        if (member != null && !memberExists(dsname, member)) {
            return notFound(e.getMethod(), "Member '" + dsname + "(" + member + ")'");
        }
        return e;
    }

    public boolean dataSetExists(String dsname) throws ZoRpcException {
        JsonNode result = call("listDatasets", params("pattern", dsname, "exactMatch", true, "maxItems", 1));
        for (JsonNode item : result.path("items")) {
            if (dsname.equalsIgnoreCase(item.path("name").asText())) {
                return true;
            }
        }
        return false;
    }

    public boolean memberExists(String dsname, String member) throws ZoRpcException {
        JsonNode result = call("listDsMembers", params("dsname", dsname, "pattern", member, "maxItems", 1));
        for (JsonNode item : result.path("items")) {
            if (member.equalsIgnoreCase(item.path("name").asText())) {
                return true;
            }
        }
        return false;
    }

    private static String text(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || v.isNull() ? null : v.asText();
    }

    private static Integer integer(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || !v.isNumber() ? null : v.asInt();
    }

    private static Long longValue(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || !v.isNumber() ? null : v.asLong();
    }

    private static Boolean bool(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || !v.isBoolean() ? null : v.asBoolean();
    }

    private static DataSetInfo toDataSet(JsonNode n) {
        List<String> volsers = null;
        if (n.has("volsers") && n.get("volsers").isArray()) {
            volsers = new ArrayList<>();
            for (JsonNode v : n.get("volsers")) {
                volsers.add(v.asText());
            }
        }
        return new DataSetInfo(text(n, "name"), longValue(n, "alloc"), integer(n, "allocx"), integer(n, "blksize"),
                text(n, "cdate"), text(n, "dataclass"), text(n, "devtype"), text(n, "dsntype"), text(n, "dsorg"),
                text(n, "edate"), bool(n, "encrypted"), integer(n, "lrecl"), text(n, "mgmtclass"),
                bool(n, "migrated"), bool(n, "multivolume"), longValue(n, "primary"), text(n, "rdate"),
                text(n, "recfm"), longValue(n, "secondary"), text(n, "spacu"), text(n, "storclass"),
                integer(n, "usedp"), integer(n, "usedx"), text(n, "volser"), volsers);
    }

    private static MemberInfo toMember(JsonNode n) {
        return new MemberInfo(text(n, "name"), text(n, "user"), integer(n, "vers"), integer(n, "mod"),
                text(n, "c4date"), text(n, "m4date"), text(n, "mtime"), integer(n, "cnorc"), integer(n, "inorc"),
                integer(n, "mnorc"), text(n, "sclm"));
    }

    private static int fetchSize(ListQuery q) {
        if (q.start() != null || q.total() || q.maxItems() <= 0) {
            return MAX_FETCH;
        }
        return q.maxItems() + 1;
    }

    private static <T> ListPage<T> page(List<T> all, ListQuery q, boolean totalKnown) {
        boolean more = q.maxItems() > 0 && all.size() > q.maxItems();
        List<T> items = more ? new ArrayList<>(all.subList(0, q.maxItems())) : all;
        return new ListPage<>(items, more, more && totalKnown ? all.size() : null);
    }

    /**
     * Lists cataloged data sets. The pattern is passed to zo unmodified; zo's catalog search appends
     * ".**" exactly like z/OSMF does. A volser restricts the result to data sets residing on it.
     */
    public ListPage<DataSetInfo> listDataSets(String dslevel, String volser, ListQuery query) throws ZoRpcException {
        String pattern = DataSetNames.dslevel(dslevel);
        String vol = volser == null ? null : DataSetNames.volser(volser);
        String start = query.start() == null ? null : DataSetNames.dsn(query.start());
        ListQuery q = new ListQuery(pattern, start, Math.max(query.maxItems(), 0), query.attributes() || vol != null,
                query.total());
        boolean filtering = start != null || vol != null;
        int fetch = filtering ? MAX_FETCH : fetchSize(q);
        JsonNode result = call("listDatasets", params("pattern", pattern, "attributes", q.attributes() ? true : null,
                "maxItems", fetch));
        List<DataSetInfo> all = new ArrayList<>();
        for (JsonNode item : result.path("items")) {
            DataSetInfo info = toDataSet(item);
            if (info.name() == null || (start != null && info.name().compareTo(start) < 0)) {
                continue;
            }
            if (vol != null && (info.volsers() == null ? !vol.equals(info.volser()) : !info.volsers().contains(vol))) {
                continue;
            }
            all.add(info);
        }
        all.sort(Comparator.comparing(DataSetInfo::name));
        return page(all, q, fetch == MAX_FETCH);
    }

    public ListPage<MemberInfo> listMembers(String dsname, String memberPattern, ListQuery query) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        String pattern = memberPattern == null ? null : DataSetNames.memberPattern(memberPattern);
        String start = query.start() == null ? null : DataSetNames.member(query.start());
        ListQuery q = new ListQuery(pattern, start, Math.max(query.maxItems(), 0), query.attributes(), query.total());
        int fetch = fetchSize(q);
        JsonNode result;
        try {
            result = call("listDsMembers", params("dsname", dsn, "pattern", pattern,
                    "attributes", q.attributes() ? true : null, "maxItems", fetch));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, null);
        }
        List<MemberInfo> all = new ArrayList<>();
        for (JsonNode item : result.path("items")) {
            MemberInfo info = toMember(item);
            if (info.name() != null && (start == null || info.name().compareTo(start) >= 0)) {
                all.add(info);
            }
        }
        all.sort(Comparator.comparing(MemberInfo::name));
        return page(all, q, fetch == MAX_FETCH);
    }

    public ZoContentIo.Content read(Target target, ZoContentIo.Options options) throws ZoRpcException {
        ZoContentIo.Options validated = validated(options, target.volser());
        try {
            return guarded(() -> contentIo.readDataset(target.fullName(), validated));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, target.dsname(), target.member());
        }
    }

    public ZoContentIo.WriteResult write(Target target, byte[] data, ZoContentIo.Options options, String etag)
            throws ZoRpcException {
        ZoContentIo.Options validated = validated(options, target.volser());
        try {
            return guarded(() -> contentIo.writeDataset(target.fullName(), data, validated, etag));
        } catch (ZoRpcException e) {
            if (!isStructural(e) && !dataSetExists(target.dsname())) {
                throw notFound(e.getMethod(), "Data set '" + target.dsname() + "'");
            }
            throw e;
        }
    }

    private static ZoContentIo.Options validated(ZoContentIo.Options o, String volser) {
        String encoding = o.encoding() == null || o.isBinary() ? o.encoding() : DataSetNames.codepage(o.encoding());
        String local = o.localEncoding() == null ? null : DataSetNames.codepage(o.localEncoding());
        return new ZoContentIo.Options(encoding, local, volser != null ? volser : o.volume());
    }

    public void createMember(String dsname, String member, boolean overwrite) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        String mem = DataSetNames.member(member);
        try {
            call("createMember", params("dsname", dsn + "(" + mem + ")", "overwrite", overwrite ? true : null));
        } catch (ZoRpcException e) {
            ZoRpcException failure = resolveFailure(e, dsn, null);
            if (failure == e && !overwrite && !isStructural(e) && memberExists(dsn, mem)) {
                throw new DataSetConflictException("Member '" + dsn + "(" + mem + ")' already exists");
            }
            throw failure;
        }
    }

    public void delete(Target target) throws ZoRpcException {
        try {
            call("deleteDataset", params("dsname", target.fullName()));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, target.dsname(), target.member());
        }
    }

    public void deleteVsam(String dsname, Boolean erase, Boolean purge) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        try {
            call("deleteVsam", params("dsname", dsn, "erase", erase, "purge", purge));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, null);
        }
    }

    public void renameDataSet(String from, String to) throws ZoRpcException {
        String before = DataSetNames.dsn(from);
        String after = DataSetNames.dsn(to);
        try {
            call("renameDataset", params("dsnameBefore", before, "dsnameAfter", after));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, before, null);
        }
    }

    public void renameMember(String dsname, String from, String to) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        String before = DataSetNames.member(from);
        String after = DataSetNames.member(to);
        try {
            call("renameMember", params("dsname", dsn, "memberBefore", before, "memberAfter", after));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, before);
        }
    }

    public void copy(CopySource source, Target target, boolean replace) throws ZoRpcException {
        Target from = Target.of(source.dsname(), source.member(), null);
        try {
            call("copyDatasetOrMember", params("source", from.fullName(), "target", target.fullName(),
                    "replace", replace ? true : null));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, from.dsname(), from.member());
        }
    }

    /** Recalls a migrated data set (zo restores synchronously, so the z/OSMF wait flag has no effect). */
    public void recall(String dsname) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        try {
            call("restoreDataset", params("dsname", dsn));
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, null);
        }
    }

    public String resolveAlias(String dsname) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        try {
            JsonNode result = call("resolveDsAlias", params("dsname", dsn));
            String target = text(result, "targetDsn");
            if (target == null || target.isEmpty()) {
                throw notFound("resolveDsAlias", "Alias '" + dsn + "'");
            }
            return target;
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, null);
        }
    }

    public String search(String dsname, String string, String parms) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        DataSetNames.noControl("string", string);
        if (string.isEmpty() || string.length() > 256) {
            throw new IllegalArgumentException("string must be 1-256 characters");
        }
        if (parms != null && !parms.matches("[A-Za-z0-9,()]{1,100}")) {
            throw new IllegalArgumentException("Invalid parms");
        }
        try {
            JsonNode result = call("toolSearch", params("dsname", dsn, "string", string, "parms", parms));
            return result.path("data").asText("");
        } catch (ZoRpcException e) {
            throw resolveFailure(e, dsn, null);
        }
    }

    private static Integer space(String label, Integer value) {
        if (value != null && (value < 0 || value > MAX_SPACE)) {
            throw new IllegalArgumentException(label + " is out of range");
        }
        return value;
    }

    private static String choice(String label, String value, Set<String> allowed) {
        if (value == null) {
            return null;
        }
        String upper = DataSetNames.noControl(label, value).toUpperCase(Locale.ROOT);
        if (!allowed.contains(upper)) {
            throw new IllegalArgumentException("Unsupported " + label + " '" + value + "'");
        }
        return upper;
    }

    /** Fills attributes the client omitted from a like data set's attributes (explicit values win). */
    private CreateSpec applyLike(CreateSpec s) throws ZoRpcException {
        String likeName = DataSetNames.dsn(s.like());
        JsonNode result = call("listDatasets", params("pattern", likeName, "exactMatch", true, "attributes", true,
                "maxItems", 1));
        DataSetInfo like = null;
        for (JsonNode item : result.path("items")) {
            like = toDataSet(item);
        }
        if (like == null) {
            throw notFound("listDatasets", "Data set '" + likeName + "'");
        }
        String spacu = like.spacu() == null ? null : like.spacu().toUpperCase(Locale.ROOT);
        String unit = "CYLINDERS".equals(spacu) ? "CYL" : "TRACKS".equals(spacu) ? "TRK" : null;
        return new CreateSpec(s.volser(), s.unit(),
                s.dsorg() != null ? s.dsorg() : like.dsorg(),
                s.alcunit() != null ? s.alcunit() : unit,
                s.primary() != null ? s.primary() : like.primary() == null ? null : like.primary().intValue(),
                s.secondary() != null ? s.secondary() : like.secondary() == null ? null : like.secondary().intValue(),
                s.dirblk() != null || like.dsorg() == null || !like.dsorg().startsWith("PO") ? s.dirblk() : 10,
                s.avgblk(),
                s.recfm() != null ? s.recfm() : like.recfm(),
                s.blksize() != null ? s.blksize() : like.blksize(),
                s.lrecl() != null ? s.lrecl() : like.lrecl(),
                s.storclass() != null ? s.storclass() : like.storclass(),
                s.mgntclass() != null ? s.mgntclass() : like.mgmtclass(),
                s.dataclass() != null ? s.dataclass() : like.dataclass(),
                s.dsntype() != null ? s.dsntype() : like.dsntype(),
                null);
    }

    public void createDataSet(String dsname, CreateSpec requested) throws ZoRpcException {
        String dsn = DataSetNames.dsn(dsname);
        CreateSpec spec = requested.like() != null ? applyLike(requested) : requested;
        String dsorg = choice("dsorg", spec.dsorg(), DSORGS);
        String dsntype = choice("dsntype", spec.dsntype(), DSNTYPES);
        if ("PO-E".equals(dsorg) || "POE".equals(dsorg)) {
            dsorg = "PO";
            dsntype = dsntype == null ? "LIBRARY" : dsntype;
        }
        String alcunit = choice("alcunit", spec.alcunit(), ALLOC_UNITS);
        String recfm = spec.recfm() == null ? null : spec.recfm().toUpperCase(Locale.ROOT);
        if (recfm != null && !recfm.matches("[FVUSBAM]{1,4}")) {
            throw new IllegalArgumentException("Invalid recfm '" + spec.recfm() + "'");
        }
        Integer primary = space("primary", spec.primary());
        if (primary == null && spec.dataclass() == null) {
            throw new IllegalArgumentException("primary is required");
        }
        Integer lrecl = space("lrecl", spec.lrecl());
        if (lrecl == null) {
            lrecl = recfm != null && recfm.startsWith("U") ? 0 : 80;
        }
        Map<String, Object> attributes = params(
                "alcunit", alcunit,
                "blksize", space("blksize", spec.blksize()),
                "dirblk", space("dirblk", spec.dirblk()),
                "dsorg", dsorg,
                "primary", primary == null ? 0 : primary,
                "recfm", recfm,
                "lrecl", lrecl,
                "dataclass", spec.dataclass() == null ? null : DataSetNames.className("dataclass", spec.dataclass()),
                "unit", spec.unit() == null ? null : DataSetNames.className("unit", spec.unit()),
                "dsntype", dsntype,
                "mgntclass", spec.mgntclass() == null ? null : DataSetNames.className("mgntclass", spec.mgntclass()),
                "avgblk", space("avgblk", spec.avgblk()),
                "secondary", space("secondary", spec.secondary()),
                "storclass", spec.storclass() == null ? null : DataSetNames.className("storclass", spec.storclass()),
                "vol", spec.volser() == null ? null : DataSetNames.volser(spec.volser()));
        call("createDataset", params("dsname", dsn, "attributes", attributes));
    }
}
