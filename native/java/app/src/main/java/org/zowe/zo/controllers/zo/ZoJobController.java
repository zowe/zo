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
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.InputValidator;
import org.zowe.zo.service.JobService;
import org.zowe.zo.service.JobService.JobAction;
import org.zowe.zo.service.JobService.JobInfo;
import org.zowe.zo.service.JobService.JobQuery;
import org.zowe.zo.service.JobService.SpoolInfo;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;

/** zo-native job API. Jobs are addressed by job ID or correlator alone, and fields keep zo's names. */
@RestController
@RequestMapping("/zo/api/v1/jobs")
@RequiredArgsConstructor
public class ZoJobController {

    private static final MediaType TEXT_UTF8 = MediaType.parseMediaType("text/plain;charset=UTF-8");

    private final JobService jobService;

    @GetMapping
    public List<Map<String, Object>> list(@RequestParam(value = "owner", required = false) String owner,
                                          @RequestParam(value = "prefix", required = false) String prefix,
                                          @RequestParam(value = "status", required = false) String status,
                                          @RequestParam(value = "maxItems", required = false) String maxItems) throws ZoRpcException {
        List<Map<String, Object>> out = new ArrayList<>();
        for (JobInfo job : jobService.listJobs(new JobQuery(owner, prefix, null, status, InputValidator.integer("maxItems", maxItems)))) out.add(toMap(job));
        return out;
    }

    @GetMapping("/{jobId}")
    public Map<String, Object> get(@PathVariable("jobId") String jobId) throws ZoRpcException {
        return toMap(jobService.getJobByRef(jobId));
    }

    @GetMapping("/{jobId}/spool")
    public List<Map<String, Object>> spool(@PathVariable("jobId") String jobId) throws ZoRpcException {
        List<Map<String, Object>> out = new ArrayList<>();
        for (SpoolInfo s : jobService.listSpools(jobId)) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("id", s.id());
            m.put("ddname", s.ddname());
            m.put("stepname", s.stepname());
            m.put("procstep", s.procstep());
            m.put("dsname", s.dsname());
            out.add(m);
        }
        return out;
    }

    @GetMapping("/{jobId}/spool/{spoolId}")
    public ResponseEntity<byte[]> readSpool(@PathVariable("jobId") String jobId, @PathVariable("spoolId") String spoolId,
                                            @RequestParam(value = "encoding", required = false) String encoding) throws ZoRpcException {
        boolean binary = "binary".equalsIgnoreCase(encoding);
        return ResponseEntity.ok().contentType(binary ? MediaType.APPLICATION_OCTET_STREAM : TEXT_UTF8)
                .body(jobService.readSpool(jobId, Objects.requireNonNull(InputValidator.integer("spoolId", spoolId)), encoding));
    }

    @GetMapping(value = "/{jobId}/jcl", produces = "text/plain;charset=UTF-8")
    public String jcl(@PathVariable("jobId") String jobId) throws ZoRpcException {
        return jobService.getJcl(jobId);
    }

    @PostMapping(consumes = "!" + MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Map<String, Object>> submit(@RequestBody byte[] jcl,
                                                      @RequestParam(value = "encoding", required = false) String encoding) throws ZoRpcException {
        return ResponseEntity.status(HttpStatus.CREATED).body(toMap(jobService.submitJcl(jcl, encoding)));
    }

    @PostMapping(value = "/dataset", consumes = MediaType.APPLICATION_JSON_VALUE)
    public ResponseEntity<Map<String, Object>> submitDataset(@RequestBody Map<String, String> body) throws ZoRpcException {
        return ResponseEntity.status(HttpStatus.CREATED).body(toMap(jobService.submitDataset(body.get("dsname"))));
    }

    @PostMapping("/{jobId}/{action:hold|release|cancel}")
    public Map<String, Object> modify(@PathVariable("jobId") String jobId, @PathVariable("action") String action) throws ZoRpcException {
        return toMap(jobService.modifyByRef(jobId, JobAction.valueOf(action.toUpperCase(Locale.ROOT))));
    }

    @DeleteMapping("/{jobId}")
    public ResponseEntity<Void> delete(@PathVariable("jobId") String jobId) throws ZoRpcException {
        jobService.modifyByRef(jobId, JobAction.DELETE);
        return ResponseEntity.noContent().build();
    }

    private static Map<String, Object> toMap(JobInfo job) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("id", job.id());
        m.put("name", job.name());
        put(m, "subsystem", job.subsystem());
        m.put("owner", job.owner());
        m.put("status", job.status());
        m.put("type", job.type());
        m.put("class", job.jobClass());
        put(m, "retcode", job.retcode());
        put(m, "correlator", job.correlator());
        m.put("phase", job.phase());
        m.put("phaseName", job.phaseName());
        put(m, "execSystem", job.execSystem());
        put(m, "execMember", job.execMember());
        put(m, "execSubmitted", job.execSubmitted());
        put(m, "execStarted", job.execStarted());
        put(m, "execEnded", job.execEnded());
        return m;
    }

    private static void put(Map<String, Object> m, String key, String value) {
        if (value != null && !value.isEmpty()) m.put(key, value);
    }
}
