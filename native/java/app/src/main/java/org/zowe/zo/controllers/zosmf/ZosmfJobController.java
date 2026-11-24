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

import jakarta.servlet.http.HttpServletRequest;
import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.servlet.support.ServletUriComponentsBuilder;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.InputValidator;
import org.zowe.zo.service.JobService;
import org.zowe.zo.service.JobService.JobAction;
import org.zowe.zo.service.JobService.JobInfo;
import org.zowe.zo.service.JobService.JobQuery;
import org.zowe.zo.service.JobService.SpoolInfo;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** z/OSMF jobs REST interface ({@code /zosmf/restjobs/jobs}). */
@RestController
@RequestMapping("/zosmf/restjobs/jobs")
@RequiredArgsConstructor
public class ZosmfJobController {

    private static final String BASE = "/zosmf/restjobs/jobs";
    private static final MediaType TEXT_UTF8 = MediaType.parseMediaType("text/plain;charset=UTF-8");
    private static final Pattern DS_FILE = Pattern.compile("^//'?([^']+)'?$");
    private static final Pattern RANGE = Pattern.compile("(\\d+)(?:-(\\d+)|,(\\d+))?");

    private final JobService jobService;

    @GetMapping
    public List<Map<String, Object>> list(@RequestParam(value = "owner", required = false) String owner,
                                          @RequestParam(value = "prefix", required = false) String prefix,
                                          @RequestParam(value = "jobid", required = false) String jobid,
                                          @RequestParam(value = "status", required = false) String status,
                                          @RequestParam(value = "max-jobs", required = false) String maxJobs,
                                          @RequestParam(value = "exec-data", required = false) String execData,
                                          HttpServletRequest request) throws ZoRpcException {
        boolean exec = isYes(execData);
        List<Map<String, Object>> out = new ArrayList<>();
        for (JobInfo job : jobService.listJobs(new JobQuery(owner, prefix, jobid, status, InputValidator.integer("max-jobs", maxJobs)))) {
            out.add(toZosmf(job, request, exec));
        }
        return out;
    }

    @GetMapping("/{jobname}/{jobid}")
    public Map<String, Object> get(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                   @RequestParam(value = "exec-data", required = false) String execData,
                                   HttpServletRequest request) throws ZoRpcException {
        return toZosmf(jobService.getJob(jobname, jobid), request, isYes(execData));
    }

    @GetMapping("/-{correlator}")
    public Map<String, Object> getByCorrelator(@PathVariable("correlator") String correlator,
                                               @RequestParam(value = "exec-data", required = false) String execData,
                                               HttpServletRequest request) throws ZoRpcException {
        return toZosmf(jobService.getJobByRef(InputValidator.correlator(correlator)), request, isYes(execData));
    }

    @GetMapping("/-{correlator}/files")
    public List<Map<String, Object>> filesByCorrelator(@PathVariable("correlator") String correlator,
                                                       HttpServletRequest request) throws ZoRpcException {
        return spoolFiles(jobService.getJobByRef(InputValidator.correlator(correlator)), request);
    }

    @GetMapping("/{jobname}/{jobid}/files")
    public List<Map<String, Object>> files(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                           HttpServletRequest request) throws ZoRpcException {
        return spoolFiles(jobService.getJob(jobname, jobid), request);
    }

    @GetMapping("/{jobname}/{jobid}/files/JCL/records")
    public ResponseEntity<byte[]> jcl(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                      @RequestHeader(value = "X-IBM-Record-Range", required = false) String range,
                                      @RequestParam(value = "mode", required = false) String mode) throws ZoRpcException {
        requireTextMode(mode);
        JobInfo job = jobService.getJob(jobname, jobid);
        byte[] data = jobService.getJcl(job.id()).getBytes(StandardCharsets.UTF_8);
        return ResponseEntity.ok().contentType(TEXT_UTF8).body(applyRange(data, range));
    }

    @GetMapping("/{jobname}/{jobid}/files/{id}/records")
    public ResponseEntity<byte[]> records(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                          @PathVariable("id") String id,
                                          @RequestHeader(value = "X-IBM-Record-Range", required = false) String range,
                                          @RequestParam(value = "mode", required = false) String mode,
                                          @RequestParam(value = "fileEncoding", required = false) String fileEncoding) throws ZoRpcException {
        String kind = mode == null ? "text" : mode.toLowerCase(Locale.ROOT);
        if (kind.equals("record")) throw InputValidator.notImplemented("mode=record is not supported");
        if (!kind.equals("text") && !kind.equals("binary")) throw new IllegalArgumentException("Invalid mode: " + kind);
        int spoolId = parseSpoolId(id);
        JobInfo job = jobService.getJob(jobname, jobid);
        if (kind.equals("binary")) {
            if (range != null) throw InputValidator.notImplemented("X-IBM-Record-Range is not supported with mode=binary");
            byte[] data = jobService.readSpool(job.id(), spoolId, "binary");
            return ResponseEntity.ok().contentType(MediaType.APPLICATION_OCTET_STREAM).body(data);
        }
        byte[] data = jobService.readSpool(job.id(), spoolId, fileEncoding);
        return ResponseEntity.ok().contentType(TEXT_UTF8).body(applyRange(data, range));
    }

    @PutMapping(consumes = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Map<String, Object>> submitFromSource(@RequestBody Map<String, Object> body,
                                                                HttpServletRequest request) throws ZoRpcException {
        rejectUnsupportedSubmitHeaders(request);
        Object file = body.get("file");
        if (!(file instanceof String source) || source.isEmpty()) {
            throw new IllegalArgumentException("'file' is required in the request body");
        }
        if (body.containsKey("volume")) throw InputValidator.notImplemented("'volume' is not supported");
        Matcher m = DS_FILE.matcher(source);
        if (!m.matches()) {
            throw InputValidator.notImplemented("Submitting a job from a USS file is not supported yet");
        }
        JobInfo job = jobService.submitDataset(m.group(1));
        return ResponseEntity.status(HttpStatus.CREATED).body(toZosmf(job, request, false));
    }

    @PutMapping(consumes = "!" + MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Map<String, Object>> submitJcl(@RequestBody(required = false) byte[] jcl,
                                                         @RequestHeader(value = "X-IBM-Intrdr-File-Encoding", required = false) String fileEncoding,
                                                         HttpServletRequest request) throws ZoRpcException {
        rejectUnsupportedSubmitHeaders(request);
        JobInfo job = jobService.submitJcl(jcl, fileEncoding);
        return ResponseEntity.status(HttpStatus.CREATED).body(toZosmf(job, request, false));
    }

    @PutMapping(value = "/{jobname}/{jobid}", consumes = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Map<String, Object>> modify(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                      @RequestBody Map<String, Object> body) throws ZoRpcException {
        Object req = body.get("request");
        if (!(req instanceof String name)) throw new IllegalArgumentException("'request' is required in the request body");
        JobAction action = switch (name.toLowerCase(Locale.ROOT)) {
            case "cancel" -> JobAction.CANCEL;
            case "hold" -> JobAction.HOLD;
            case "release" -> JobAction.RELEASE;
            case "class" -> throw InputValidator.notImplemented("Changing the job class is not supported");
            default -> throw new IllegalArgumentException("Invalid request: " + name);
        };
        Object version = body.get("version");
        boolean sync = isSync(version == null ? null : version.toString());
        return feedback(jobService.modify(jobname, jobid, action), sync);
    }

    @DeleteMapping("/{jobname}/{jobid}")
    public ResponseEntity<Map<String, Object>> delete(@PathVariable("jobname") String jobname, @PathVariable("jobid") String jobid,
                                      @RequestHeader(value = "X-IBM-Job-Modify-Version", required = false) String version) throws ZoRpcException {
        boolean sync = isSync(version);
        return feedback(jobService.modify(jobname, jobid, JobAction.DELETE), sync);
    }

    private static boolean isSync(String version) {
        if (version == null || version.equals("2.0")) return true;
        if (version.equals("1.0")) return false;
        throw new IllegalArgumentException("Invalid version: " + version);
    }

    private static ResponseEntity<Map<String, Object>> feedback(JobInfo job, boolean sync) {
        if (!sync) return ResponseEntity.accepted().build();
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("jobid", job.id());
        out.put("jobname", job.name());
        out.put("original-jobid", job.id());
        out.put("owner", job.owner());
        putIfPresent(out, "member", job.execMember());
        putIfPresent(out, "sysname", job.execSystem());
        putIfPresent(out, "job-correlator", job.correlator());
        out.put("status", 0);
        putIfPresent(out, "retcode", job.retcode());
        return ResponseEntity.ok(out);
    }

    private static void rejectUnsupportedSubmitHeaders(HttpServletRequest request) throws ZoRpcException {
        Enumeration<String> names = request.getHeaderNames();
        while (names != null && names.hasMoreElements()) {
            String name = names.nextElement().toLowerCase(Locale.ROOT);
            String value = request.getHeader(name);
            if (name.startsWith("x-ibm-jcl-symbol-")) {
                throw InputValidator.notImplemented("JCL symbol substitution is not supported");
            }
            boolean ok = switch (name) {
                case "x-ibm-intrdr-mode" -> "TEXT".equalsIgnoreCase(value);
                case "x-ibm-intrdr-recfm" -> "F".equalsIgnoreCase(value);
                case "x-ibm-intrdr-lrecl" -> "80".equals(value);
                case "x-ibm-intrdr-class" -> false;
                default -> true;
            };
            if (!ok) throw InputValidator.notImplemented("Header " + name + ": " + value + " is not supported");
        }
    }

    private static void requireTextMode(String mode) throws ZoRpcException {
        if (mode == null || mode.equalsIgnoreCase("text")) return;
        if (mode.equalsIgnoreCase("binary") || mode.equalsIgnoreCase("record")) {
            throw InputValidator.notImplemented("mode=" + mode + " is not supported for JCL");
        }
        throw new IllegalArgumentException("Invalid mode: " + mode);
    }

    private static int parseSpoolId(String id) {
        try {
            return Integer.parseInt(id);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Invalid spool file ID: " + id);
        }
    }

    private List<Map<String, Object>> spoolFiles(JobInfo job, HttpServletRequest request) throws ZoRpcException {
        List<Map<String, Object>> out = new ArrayList<>();
        for (SpoolInfo spool : jobService.listSpools(job.id())) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("id", spool.id());
            m.put("ddname", spool.ddname());
            putIfPresent(m, "stepname", spool.stepname());
            putIfPresent(m, "procstep", spool.procstep());
            m.put("jobid", job.id());
            m.put("jobname", job.name());
            putIfPresent(m, "subsystem", job.subsystem());
            m.put("records-url", jobUrl(request, job) + "/files/" + spool.id() + "/records");
            out.add(m);
        }
        return out;
    }

    static Map<String, Object> toZosmf(JobInfo job, HttpServletRequest request, boolean execData) {
        String url = jobUrl(request, job);
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("jobid", job.id());
        m.put("jobname", job.name());
        putIfPresent(m, "subsystem", job.subsystem());
        m.put("owner", job.owner());
        m.put("status", job.status());
        m.put("type", job.type());
        m.put("class", job.jobClass());
        m.put("retcode", job.retcode());
        m.put("url", url);
        m.put("files-url", url + "/files");
        putIfPresent(m, "job-correlator", job.correlator());
        m.put("phase", job.phase());
        m.put("phase-name", job.phaseName());
        if (execData) {
            putIfPresent(m, "exec-system", job.execSystem());
            putIfPresent(m, "exec-member", job.execMember());
            putIfPresent(m, "exec-submitted", job.execSubmitted());
            putIfPresent(m, "exec-started", job.execStarted());
            putIfPresent(m, "exec-ended", job.execEnded());
        }
        return m;
    }

    private static String jobUrl(HttpServletRequest request, JobInfo job) {
        return ServletUriComponentsBuilder.fromContextPath(request).path(BASE)
                .pathSegment(job.name(), job.id()).build().encode().toUriString();
    }

    private static void putIfPresent(Map<String, Object> map, String key, String value) {
        if (value != null && !value.isEmpty()) map.put(key, value);
    }

    private static boolean isYes(String value) {
        return value != null && value.equalsIgnoreCase("Y");
    }

    /** Applies z/OSMF's X-IBM-Record-Range ({@code SSS-EEE} or {@code SSS,NNN}, zero-based) to line-oriented text. */
    static byte[] applyRange(byte[] data, String range) {
        if (range == null || range.isBlank()) return data;
        Matcher m = RANGE.matcher(range.trim());
        if (!m.matches()) throw new IllegalArgumentException("Invalid X-IBM-Record-Range: " + range);
        long start = Long.parseLong(m.group(1));
        long end = Long.MAX_VALUE;
        if (m.group(2) != null) {
            end = Long.parseLong(m.group(2));
        } else if (m.group(3) != null) {
            long count = Long.parseLong(m.group(3));
            if (count == 0) return new byte[0];
            end = start + count - 1;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        long record = 0;
        int lineStart = 0;
        for (int i = 0; i < data.length && record <= end; i++) {
            if (data[i] == '\n' || i == data.length - 1) {
                if (record >= start) out.write(data, lineStart, i + 1 - lineStart);
                record++;
                lineStart = i + 1;
            }
        }
        return out.toByteArray();
    }
}
