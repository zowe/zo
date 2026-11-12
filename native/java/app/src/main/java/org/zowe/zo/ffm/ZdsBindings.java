package org.zowe.zo.ffm;

import java.lang.foreign.*;
import java.util.ArrayList;
import java.util.List;

import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.generated.DS_ATTRIBUTES_C;
import org.zowe.zo.ffm.generated.ZDSBasicResponse_C;
import org.zowe.zo.ffm.generated.ZDSEntry_C;
import org.zowe.zo.ffm.generated.ZDSListResponse_C;
import org.zowe.zo.ffm.generated.ZDSMemListResponse_C;
import org.zowe.zo.ffm.generated.ZDSStringResponse_C;
import org.zowe.zo.ffm.generated.ZdsCApi;

@Service
public class ZdsBindings implements ZdsService {

    static {
        // Force loading of the native library before any FFM calls are made
        try {
            Class.forName("org.zowe.zo.ffm.NativeLoader");
        } catch (ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    public static class ZDSEntry {
        public String name;
        public String dsorg;
        public String volser;
        public String recfm;
        public boolean migrated;
    }

    public List<ZDSEntry> listDataSets(String dsn) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
          
            var dsnSeg = FfmUtils.allocateString(arena, dsn);
            
            var responsePtr = ZdsCApi.zds_c_list_data_sets(dsnSeg);
            // MemorySegment responsePtr = (MemorySegment) zds_c_list_data_sets.invokeExact(dsnSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZDSListResponse_C.reinterpret(responsePtr, arena, null);

            try {
                MemorySegment errorMsgSeg = ZDSListResponse_C.error_message(responsePtr);
                String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
                if (errorMsg != null) {
                    throw new RuntimeException(errorMsg);
                }

                MemorySegment entriesPtr = ZDSListResponse_C.entries(responsePtr);
                long count = ZDSListResponse_C.count(responsePtr);

                List<ZDSEntry> results = new ArrayList<>();
                if (count > 0 && entriesPtr.address() != 0) {
                    // reinterpret entries array to read structs
                    MemorySegment entriesArray = ZDSEntry_C.reinterpret(entriesPtr, count,arena, null);
                    for (long i = 0; i < count; i++) {
                        MemorySegment entryStruct = ZDSEntry_C.asSlice(entriesArray, i);

                        ZDSEntry entry = new ZDSEntry();
                        entry.name = FfmUtils.readString(ZDSEntry_C.name(entryStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                        entry.dsorg = FfmUtils.readString(ZDSEntry_C.dsorg(entryStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                        entry.volser = FfmUtils.readString(ZDSEntry_C.volser(entryStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                        entry.recfm = FfmUtils.readString(ZDSEntry_C.recfm(entryStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                        entry.migrated = ZDSEntry_C.migrated(entryStruct);
                        results.add(entry);
                    }
                }
                return results;
            } finally {
                ZdsCApi.zds_c_free_list_response(responsePtr);
            }
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String readDataSet(String dsn, String codepage) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment cpSeg = FfmUtils.allocateString(arena, codepage);

            MemorySegment responsePtr = ZdsCApi.zds_c_read_data_set(dsnSeg, cpSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZDSStringResponse_C.reinterpret(responsePtr, arena, null);

            try {
                MemorySegment errorMsgSeg = ZDSStringResponse_C.error_message(responsePtr);
                String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
                if (errorMsg != null) {
                    throw new RuntimeException(errorMsg);
                }

                MemorySegment dataSeg = ZDSStringResponse_C.data(responsePtr);
                return FfmUtils.readString(dataSeg, FfmUtils.MAX_DATA_STRING_LENGTH);
            } finally {
                ZdsCApi.zds_c_free_string_response(responsePtr);
            }
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String writeDataSet(String dsn, String data, String codepage, String etag) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment dataSeg = FfmUtils.allocateString(arena, data);
            MemorySegment cpSeg = FfmUtils.allocateString(arena, codepage);
            MemorySegment etagSeg = FfmUtils.allocateString(arena, etag);

            MemorySegment responsePtr = ZdsCApi.zds_c_write_data_set(dsnSeg, dataSeg, cpSeg, etagSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZDSStringResponse_C.reinterpret(responsePtr, arena, null);

            try {
                MemorySegment errorMsgSeg = ZDSStringResponse_C.error_message(responsePtr);
                String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
                if (errorMsg != null) {
                    throw new RuntimeException(errorMsg);
                }

                MemorySegment outEtagSeg = ZDSStringResponse_C.data(responsePtr);
                return FfmUtils.readString(outEtagSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            } finally {
                ZdsCApi.zds_c_free_string_response(responsePtr);
            }
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public static class ZDSMem {
        public String name;
    }

    public static class DSAttributes {
        public String alcunit;
        public int blksize;
        public int dirblk;
        public String dsorg;
        public int primary;
        public String recfm;
        public int lrecl;
        public String dataclass;
        public String unit;
        public String dsntype;
        public String mgntclass;
        public String dsname;
        public int avgblk;
        public int secondary;
        public int size;
        public String storclass;
        public String vol;
    }

    private void handleBasicResponse(MemorySegment responsePtr, Arena arena) throws Exception {
        if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
        responsePtr = ZDSBasicResponse_C.reinterpret(responsePtr, arena, null);
        try {
            MemorySegment errorMsgSeg = ZDSBasicResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                throw new RuntimeException(errorMsg);
            }
        } finally {
            ZdsCApi.zds_c_free_basic_response(responsePtr);
        }
    }

    public void createDataSet(String dsn, DSAttributes attrs) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment attrsSeg = MemorySegment.NULL;
            
            if (attrs != null) {
                attrsSeg = DS_ATTRIBUTES_C.allocate(arena);
                DS_ATTRIBUTES_C.alcunit(attrsSeg, FfmUtils.allocateString(arena, attrs.alcunit));
                DS_ATTRIBUTES_C.blksize(attrsSeg, attrs.blksize);
                DS_ATTRIBUTES_C.dirblk(attrsSeg, attrs.dirblk);
                DS_ATTRIBUTES_C.dsorg(attrsSeg, FfmUtils.allocateString(arena, attrs.dsorg));
                DS_ATTRIBUTES_C.primary(attrsSeg, attrs.primary);
                DS_ATTRIBUTES_C.recfm(attrsSeg, FfmUtils.allocateString(arena, attrs.recfm));
                DS_ATTRIBUTES_C.lrecl(attrsSeg, attrs.lrecl);
                DS_ATTRIBUTES_C.dataclass(attrsSeg, FfmUtils.allocateString(arena, attrs.dataclass));
                DS_ATTRIBUTES_C.unit(attrsSeg, FfmUtils.allocateString(arena, attrs.unit));
                DS_ATTRIBUTES_C.dsntype(attrsSeg, FfmUtils.allocateString(arena, attrs.dsntype));
                DS_ATTRIBUTES_C.mgntclass(attrsSeg, FfmUtils.allocateString(arena, attrs.mgntclass));
                DS_ATTRIBUTES_C.dsname(attrsSeg, FfmUtils.allocateString(arena, attrs.dsname));
                DS_ATTRIBUTES_C.avgblk(attrsSeg, attrs.avgblk);
                DS_ATTRIBUTES_C.secondary(attrsSeg, attrs.secondary);
                DS_ATTRIBUTES_C.size(attrsSeg, attrs.size);
                DS_ATTRIBUTES_C.storclass(attrsSeg, FfmUtils.allocateString(arena, attrs.storclass));
                DS_ATTRIBUTES_C.vol(attrsSeg, FfmUtils.allocateString(arena, attrs.vol));
            }

            MemorySegment responsePtr = ZdsCApi.zds_c_create_data_set(dsnSeg, attrsSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void createMember(String dsn) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment responsePtr = ZdsCApi.zds_c_create_member(dsnSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public List<ZDSMem> listMembers(String dsn) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment responsePtr = ZdsCApi.zds_c_list_members(dsnSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZDSMemListResponse_C.reinterpret(responsePtr, arena, null);

            try {
                MemorySegment errorMsgSeg = ZDSMemListResponse_C.error_message(responsePtr);
                String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
                if (errorMsg != null) {
                    throw new RuntimeException(errorMsg);
                }

                MemorySegment membersPtr = ZDSMemListResponse_C.members(responsePtr);
                long count = ZDSMemListResponse_C.count(responsePtr);

                List<ZDSMem> results = new ArrayList<>();
                if (count > 0 && membersPtr.address() != 0) {
                    MemorySegment membersArray = org.zowe.zo.ffm.generated.ZDSMem_C.reinterpret(membersPtr, count, arena, null);
                    for (long i = 0; i < count; i++) {
                        MemorySegment memStruct = org.zowe.zo.ffm.generated.ZDSMem_C.asSlice(membersArray, i);
                        ZDSMem mem = new ZDSMem();
                        mem.name = FfmUtils.readString(org.zowe.zo.ffm.generated.ZDSMem_C.name(memStruct), FfmUtils.MAX_METADATA_STRING_LENGTH);
                        results.add(mem);
                    }
                }
                return results;
            } finally {
                ZdsCApi.zds_c_free_mem_list_response(responsePtr);
            }
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void deleteDataSet(String dsn) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment dsnSeg = FfmUtils.allocateString(arena, dsn);
            MemorySegment responsePtr = ZdsCApi.zds_c_delete_data_set(dsnSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }
}
