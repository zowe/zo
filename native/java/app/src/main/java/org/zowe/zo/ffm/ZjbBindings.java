package org.zowe.zo.ffm;

import org.springframework.stereotype.Service;

import java.lang.foreign.*;
import java.util.ArrayList;
import java.util.List;

import org.zowe.zo.ffm.generated.ZjbCApi;
import org.zowe.zo.ffm.generated.ZJBBasicResponse_C;
import org.zowe.zo.ffm.generated.ZJBStringResponse_C;
import org.zowe.zo.ffm.generated.ZJobListResponse_C;
import org.zowe.zo.ffm.generated.ZJobResponse_C;
import org.zowe.zo.ffm.generated.ZJobDDListResponse_C;
import org.zowe.zo.ffm.generated.ZJob_C;
import org.zowe.zo.ffm.generated.ZJobDD_C;

@Service
public class ZjbBindings implements ZjbService {

    static {
        // Force loading of the native library before any FFM calls are made
        try {
            Class.forName("org.zowe.zo.ffm.NativeLoader");
        } catch (ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    public static class ZJob {
        public String jobname;
        public String jobid;
        public String owner;
        public String status;
        public String fullStatus;
        public String retcode;
        public String correlator;
    }

    @Override
    public List<ZJob> listJobsByOwner(String ownerName, String prefix, String status) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment ownerSeg = FfmUtils.allocateString(arena, ownerName);
            MemorySegment prefixSeg = FfmUtils.allocateString(arena, prefix);
            MemorySegment statusSeg = FfmUtils.allocateString(arena, status);

            MemorySegment responsePtr = ZjbCApi.zjb_c_list_jobs_by_owner(ownerSeg, prefixSeg, statusSeg);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJobListResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJobListResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_job_list_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment jobsPtr = ZJobListResponse_C.jobs(responsePtr);
            long count = ZJobListResponse_C.count(responsePtr);

            List<ZJob> results = new ArrayList<>();
            if (count > 0 && jobsPtr.address() != 0) {
                MemorySegment jobsArray = ZJob_C.reinterpret(jobsPtr, count, arena, null);
                for (long i = 0; i < count; i++) {
                    MemorySegment jobStruct = ZJob_C.asSlice(jobsArray, i);
                    ZJob job = new ZJob();
                    job.jobname = FfmUtils.readString(ZJob_C.jobname(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.jobid = FfmUtils.readString(ZJob_C.jobid(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.owner = FfmUtils.readString(ZJob_C.owner(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.status = FfmUtils.readString(ZJob_C.status(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.fullStatus = FfmUtils.readString(ZJob_C.full_status(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.retcode = FfmUtils.readString(ZJob_C.retcode(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    job.correlator = FfmUtils.readString(ZJob_C.correlator(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    results.add(job);
                }
            }
            ZjbCApi.zjb_c_free_job_list_response(responsePtr);
            return results;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public ZJob getJobStatus(String jobid) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jobidSeg = FfmUtils.allocateString(arena, jobid);
            MemorySegment responsePtr = ZjbCApi.zjb_c_get_job_status(jobidSeg);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJobResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJobResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_job_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment jobPtr = ZJobResponse_C.job(responsePtr);
            ZJob job = null;
            if (jobPtr.address() != 0) {
                MemorySegment jobStruct = ZJob_C.reinterpret(jobPtr, arena, null);
                job = new ZJob();
                job.jobname = FfmUtils.readString(ZJob_C.jobname(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.jobid = FfmUtils.readString(ZJob_C.jobid(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.owner = FfmUtils.readString(ZJob_C.owner(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.status = FfmUtils.readString(ZJob_C.status(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.fullStatus = FfmUtils.readString(ZJob_C.full_status(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.retcode = FfmUtils.readString(ZJob_C.retcode(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                job.correlator = FfmUtils.readString(ZJob_C.correlator(jobStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
            }
            ZjbCApi.zjb_c_free_job_response(responsePtr);
            return job;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public static class ZJobDD {
        public String jobid;
        public String ddn;
        public String dsn;
        public String stepname;
        public String procstep;
        public int key;
    }

    private void handleBasicResponse(MemorySegment responsePtr, Arena arena) throws Exception {
        if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
        responsePtr = ZJBBasicResponse_C.reinterpret(responsePtr, arena, null);
        MemorySegment errorMsgSeg = ZJBBasicResponse_C.error_message(responsePtr);
        String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
        try {
            ZjbCApi.zjb_c_free_basic_response(responsePtr);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
        if (errorMsg != null) {
            throw new RuntimeException(errorMsg);
        }
    }

    public List<ZJobDD> listSpoolFiles(String jobid) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jobidSeg = FfmUtils.allocateString(arena, jobid);
            MemorySegment responsePtr = ZjbCApi.zjb_c_list_spool_files(jobidSeg);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJobDDListResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJobDDListResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_job_dd_list_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment ddsPtr = ZJobDDListResponse_C.dds(responsePtr);
            long count = ZJobDDListResponse_C.count(responsePtr);

            List<ZJobDD> results = new ArrayList<>();
            if (count > 0 && ddsPtr.address() != 0) {
                MemorySegment ddsArray = ZJobDD_C.reinterpret(ddsPtr, count, arena, null);
                for (long i = 0; i < count; i++) {
                    MemorySegment ddStruct = ZJobDD_C.asSlice(ddsArray, i);
                    ZJobDD dd = new ZJobDD();
                    dd.jobid = FfmUtils.readString(ZJobDD_C.jobid(ddStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    dd.ddn = FfmUtils.readString(ZJobDD_C.ddn(ddStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    dd.dsn = FfmUtils.readString(ZJobDD_C.dsn(ddStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    dd.stepname = FfmUtils.readString(ZJobDD_C.stepname(ddStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    dd.procstep = FfmUtils.readString(ZJobDD_C.procstep(ddStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                    dd.key = ZJobDD_C.key(ddStruct);
                    results.add(dd);
                }
            }
            ZjbCApi.zjb_c_free_job_dd_list_response(responsePtr);
            return results;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String readSpoolFile(String jobid, int key) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jobidSeg = FfmUtils.allocateString(arena, jobid);
            MemorySegment responsePtr = ZjbCApi.zjb_c_read_spool_file(jobidSeg, key);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJBStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJBStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment dataSeg = ZJBStringResponse_C.data(responsePtr);
            String data = FfmUtils.readString(dataSeg, FfmUtils.MAX_DATA_STRING_LENGTH);
            ZjbCApi.zjb_c_free_string_response(responsePtr);
            return data;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String getJobJcl(String jobid) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jobidSeg = FfmUtils.allocateString(arena, jobid);
            MemorySegment responsePtr = ZjbCApi.zjb_c_get_job_jcl(jobidSeg);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJBStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJBStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment dataSeg = ZJBStringResponse_C.data(responsePtr);
            String data = FfmUtils.readString(dataSeg, FfmUtils.MAX_DATA_STRING_LENGTH);
            ZjbCApi.zjb_c_free_string_response(responsePtr);
            return data;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void deleteJob(String jobid) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jobidSeg = FfmUtils.allocateString(arena, jobid);
            MemorySegment responsePtr = ZjbCApi.zjb_c_delete_job(jobidSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String submitJob(String jclContent) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment jclSeg = FfmUtils.allocateString(arena, jclContent);
            MemorySegment responsePtr = ZjbCApi.zjb_c_submit_job(jclSeg);

            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");

            responsePtr = ZJBStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZJBStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZjbCApi.zjb_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment jobidSeg = ZJBStringResponse_C.data(responsePtr);
            String jobid = FfmUtils.readString(jobidSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            ZjbCApi.zjb_c_free_string_response(responsePtr);
            return jobid;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }
}
