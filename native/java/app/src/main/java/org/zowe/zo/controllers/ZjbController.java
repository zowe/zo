package org.zowe.zo.controllers;

import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;
import org.zowe.zo.ffm.ZjbBindings.ZJob;
import org.zowe.zo.ffm.ZjbBindings.ZJobDD;
import org.zowe.zo.ffm.ZjbService;

import java.util.List;
import java.util.Map;
import java.util.HashMap;

@RestController
@RequestMapping("/zosmf/restjobs/jobs")
@RequiredArgsConstructor
public class ZjbController {

    private final ZjbService zjbService;

    @GetMapping
    public List<ZJob> listJobs(@RequestParam(value = "owner", defaultValue = "*") String owner,
                               @RequestParam(value = "prefix", defaultValue = "*") String prefix,
                               @RequestParam(value = "status", required = false) String status) throws Exception {
        return zjbService.listJobsByOwner(owner, prefix, status != null ? status : "");
    }

    @GetMapping("/{jobid}")
    public ZJob getJobStatus(@PathVariable("jobid") String jobid) throws Exception {
        return zjbService.getJobStatus(jobid);
    }

    @PutMapping(produces = "text/plain;charset=UTF-8")
    public String submitJob(@RequestBody String jclContent) throws Exception {
        return zjbService.submitJob(jclContent);
    }

    @GetMapping("/{jobid}/spool")
    public List<ZJobDD> listSpoolFiles(@PathVariable("jobid") String jobid) throws Exception {
        return zjbService.listSpoolFiles(jobid);
    }

    @GetMapping(value = "/{jobid}/spool/{key}", produces = "text/plain;charset=UTF-8")
    public String readSpoolFile(@PathVariable("jobid") String jobid, @PathVariable("key") int key) throws Exception {
        return zjbService.readSpoolFile(jobid, key);
    }

    @GetMapping(value = "/{jobid}/jcl", produces = "text/plain;charset=UTF-8")
    public String getJobJcl(@PathVariable("jobid") String jobid) throws Exception {
        return zjbService.getJobJcl(jobid);
    }

    @DeleteMapping("/{jobid}")
    public Map<String, String> deleteJob(@PathVariable("jobid") String jobid) throws Exception {
        zjbService.deleteJob(jobid);
        return Map.of("message", "Job deleted successfully");
    }
}
