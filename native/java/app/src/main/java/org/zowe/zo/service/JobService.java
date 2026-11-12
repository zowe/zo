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
import org.springframework.security.core.Authentication;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

@Service
@RequiredArgsConstructor
public class JobService {

    public static final int DEFAULT_MAX_JOBS = 1000;
    private static final String LOCAL_ENCODING = "UTF-8";
    private static final String BINARY = "binary";

    private final ZoRpcClient rpc;

    public enum JobAction {
        HOLD("holdJob"),
        RELEASE("releaseJob"),
        CANCEL("cancelJob"),
        DELETE("deleteJob");

        private final String method;

        JobAction(String method) {
            this.method = method;
        }
    }

    public record JobQuery(String owner, String prefix, String jobId, String status, Integer maxJobs) {
    }

    public record JobInfo(String id, String name, String subsystem, String owner, String status, String type,
                          String jobClass, String retcode, String correlator, int phase, String phaseName,
                          String execSystem, String execMember, String execSubmitted, String execStarted,
                          String execEnded) {
    }

    public record SpoolInfo(int id, String ddname, String stepname, String procstep, String dsname) {
    }

    public List<JobInfo> listJobs(JobQuery query) throws ZoRpcException {
        if (query.jobId() != null && !query.jobId().isEmpty()) {
            String ref = InputValidator.jobRef(query.jobId());
            try {
                return List.of(getJobByRef(ref));
            } catch (ZoRpcException e) {
                if (e.getCode() == ZoRpcException.NOT_FOUND) return List.of();
                throw e;
            }
        }
        String owner = query.owner() == null || query.owner().isEmpty() ? currentUser() : InputValidator.jobNamePattern(query.owner());
        String prefix = query.prefix() == null || query.prefix().isEmpty() ? "*" : InputValidator.jobNamePattern(query.prefix());
        int max = query.maxJobs() == null ? DEFAULT_MAX_JOBS : query.maxJobs();
        if (max < 1) throw new IllegalArgumentException("max-jobs must be a positive integer");
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("owner", owner);
        params.put("prefix", prefix);
        if (query.status() != null && !query.status().isEmpty()) params.put("status", InputValidator.jobStatus(query.status()));
        params.put("maxItems", max);
        JsonNode items = rpc.call("listJobs", params).path("items");
        List<JobInfo> jobs = new ArrayList<>();
        for (JsonNode item : items) jobs.add(toJob(item));
        return jobs;
    }

    /** Looks a job up by its ID or correlator. */
    public JobInfo getJobByRef(String jobRef) throws ZoRpcException {
        String ref = InputValidator.jobRef(jobRef);
        return toJob(callOrNotFound("getJobStatus", Map.of("jobId", ref)));
    }

    /** Looks a job up and verifies that zo reports the same job name for the ID. */
    public JobInfo getJob(String jobName, String jobId) throws ZoRpcException {
        String name = InputValidator.jobName(jobName);
        JobInfo job = getJobByRef(InputValidator.jobId(jobId));
        if (!name.equalsIgnoreCase(job.name())) {
            throw InputValidator.notFound("getJobStatus", "Job " + name + "(" + jobId + ") was not found");
        }
        return job;
    }

    /** Runs hold, release, cancel or delete and returns the job as it was just before the action. */
    public JobInfo modify(String jobName, String jobId, JobAction action) throws ZoRpcException {
        JobInfo job = getJob(jobName, jobId);
        rpc.call(action.method, Map.of("jobId", job.id()));
        return job;
    }

    public JobInfo modifyByRef(String jobRef, JobAction action) throws ZoRpcException {
        JobInfo job = getJobByRef(jobRef);
        rpc.call(action.method, Map.of("jobId", job.id()));
        return job;
    }

    public List<SpoolInfo> listSpools(String jobRef) throws ZoRpcException {
        String ref = InputValidator.jobRef(jobRef);
        JsonNode items = callOrNotFound("listSpools", Map.of("jobId", ref)).path("items");
        List<SpoolInfo> spools = new ArrayList<>();
        for (JsonNode item : items) {
            spools.add(new SpoolInfo(item.path("id").asInt(), text(item, "ddname"), text(item, "stepname"),
                    text(item, "procstep"), text(item, "dsname")));
        }
        return spools;
    }

    /**
     * @param encoding codepage of the spool data, "binary" for no conversion, or null for the zo default
     */
    public byte[] readSpool(String jobRef, int spoolId, String encoding) throws ZoRpcException {
        if (spoolId < 1) throw new IllegalArgumentException("Invalid spool file ID");
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("jobId", InputValidator.jobRef(jobRef));
        params.put("spoolId", spoolId);
        if (encoding != null && encoding.equalsIgnoreCase(BINARY)) {
            params.put("encoding", BINARY);
        } else {
            if (encoding != null) params.put("encoding", InputValidator.codeset(encoding));
            params.put("localEncoding", LOCAL_ENCODING);
        }
        JsonNode result = callOrNotFound("readSpool", params);
        return Base64.getDecoder().decode(result.path("data").asText(""));
    }

    public String getJcl(String jobRef) throws ZoRpcException {
        return callOrNotFound("getJcl", Map.of("jobId", InputValidator.jobRef(jobRef))).path("data").asText("");
    }

    /**
     * @param localEncoding codepage of the JCL bytes (null = UTF-8)
     */
    public JobInfo submitJcl(byte[] jcl, String localEncoding) throws ZoRpcException {
        if (jcl == null || jcl.length == 0) throw new IllegalArgumentException("JCL is required");
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("jcl", Base64.getEncoder().encodeToString(jcl));
        params.put("localEncoding", localEncoding == null ? LOCAL_ENCODING : InputValidator.codeset(localEncoding));
        return jobFromSubmit(rpc.call("submitJcl", params));
    }

    public JobInfo submitDataset(String dsn) throws ZoRpcException {
        return jobFromSubmit(rpc.call("submitJob", Map.of("dsname", InputValidator.dsn(dsn))));
    }

    private JobInfo jobFromSubmit(JsonNode submitted) throws ZoRpcException {
        return getJobByRef(submitted.path("jobId").asText(""));
    }

    private JsonNode callOrNotFound(String method, Object params) throws ZoRpcException {
        try {
            return rpc.call(method, params);
        } catch (ZoRpcException e) {
            String details = e.getDetails();
            String text = (e.getMessage() == null ? "" : e.getMessage()) + (details == null ? "" : details);
            if (e.getCode() != ZoRpcException.NOT_FOUND && text.toLowerCase(Locale.ROOT).contains("could not locate job")) {
                throw InputValidator.notFound(method, "Job was not found");
            }
            throw e;
        }
    }

    private static JobInfo toJob(JsonNode n) {
        return new JobInfo(text(n, "id"), text(n, "name"), text(n, "subsystem"), text(n, "owner"), text(n, "status"),
                text(n, "type"), text(n, "class"), text(n, "retcode"), text(n, "correlator"), n.path("phase").asInt(),
                text(n, "phaseName"), text(n, "execSystem"), text(n, "execMember"), text(n, "execSubmitted"),
                text(n, "execStarted"), text(n, "execEnded"));
    }

    static String text(JsonNode node, String field) {
        JsonNode value = node.get(field);
        return value == null || value.isNull() ? null : value.asText();
    }

    private static String currentUser() {
        Authentication auth = SecurityContextHolder.getContext().getAuthentication();
        if (auth == null || auth.getName() == null || auth.getName().isBlank()) return "*";
        return InputValidator.jobNamePattern(auth.getName());
    }
}
