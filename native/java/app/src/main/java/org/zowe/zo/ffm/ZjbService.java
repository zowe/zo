package org.zowe.zo.ffm;

import java.util.List;

public interface ZjbService {
    List<ZjbBindings.ZJob> listJobsByOwner(String ownerName, String prefix, String status) throws Exception;
    ZjbBindings.ZJob getJobStatus(String jobid) throws Exception;
    String submitJob(String jclContent) throws Exception;
    List<ZjbBindings.ZJobDD> listSpoolFiles(String jobid) throws Exception;
    String readSpoolFile(String jobid, int key) throws Exception;
    String getJobJcl(String jobid) throws Exception;
    void deleteJob(String jobid) throws Exception;
}
