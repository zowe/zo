package org.zowe.zo.ffm;

import org.springframework.stereotype.Service;

import java.lang.foreign.*;

import org.zowe.zo.ffm.generated.ZusfCApi;
import org.zowe.zo.ffm.generated.ZUSFBasicResponse_C;
import org.zowe.zo.ffm.generated.ZUSFStringResponse_C;
import org.zowe.zo.ffm.generated.ZUSFListOptions_C;

@Service
public class ZusfBindings implements ZusfService {

    static {
        // Force loading of the native library before any FFM calls are made
        try {
            Class.forName("org.zowe.zo.ffm.NativeLoader");
        } catch (ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    public String listUssDir(String path, boolean allFiles, boolean longFormat, int maxDepth) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment pathSeg = FfmUtils.allocateString(arena, path);
            
            MemorySegment optionsSeg = ZUSFListOptions_C.allocate(arena);
            ZUSFListOptions_C.all_files(optionsSeg, allFiles);
            ZUSFListOptions_C.long_format(optionsSeg, longFormat);
            ZUSFListOptions_C.max_depth(optionsSeg, maxDepth);

            MemorySegment responsePtr = ZusfCApi.zusf_c_list_uss_dir(pathSeg, optionsSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZUSFStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZUSFStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZusfCApi.zusf_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment dataSeg = ZUSFStringResponse_C.data(responsePtr);
            String data = FfmUtils.readString(dataSeg, FfmUtils.MAX_DATA_STRING_LENGTH);
            ZusfCApi.zusf_c_free_string_response(responsePtr);
            return data;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String readUssFile(String file, String codepage) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment cpSeg = FfmUtils.allocateString(arena, codepage);
            MemorySegment responsePtr = ZusfCApi.zusf_c_read_uss_file(fileSeg, cpSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZUSFStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZUSFStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZusfCApi.zusf_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment dataSeg = ZUSFStringResponse_C.data(responsePtr);
            String data = FfmUtils.readString(dataSeg, FfmUtils.MAX_DATA_STRING_LENGTH);
            ZusfCApi.zusf_c_free_string_response(responsePtr);
            return data;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public String writeUssFile(String file, String data, String codepage, String etag) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment dataSeg = FfmUtils.allocateString(arena, data);
            MemorySegment cpSeg = FfmUtils.allocateString(arena, codepage);
            MemorySegment etagSeg = FfmUtils.allocateString(arena, etag);

            MemorySegment responsePtr = ZusfCApi.zusf_c_write_uss_file(fileSeg, dataSeg, cpSeg, etagSeg);
            
            if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
            
            responsePtr = ZUSFStringResponse_C.reinterpret(responsePtr, arena, null);

            MemorySegment errorMsgSeg = ZUSFStringResponse_C.error_message(responsePtr);
            String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            if (errorMsg != null) {
                ZusfCApi.zusf_c_free_string_response(responsePtr);
                throw new RuntimeException(errorMsg);
            }

            MemorySegment outDataSeg = ZUSFStringResponse_C.data(responsePtr);
            String outEtag = FfmUtils.readString(outDataSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
            ZusfCApi.zusf_c_free_string_response(responsePtr);
            return outEtag;
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    private void handleBasicResponse(MemorySegment responsePtr, Arena arena) throws Exception {
        if (responsePtr.address() == 0) throw new RuntimeException("Null response from native library");
        responsePtr = ZUSFBasicResponse_C.reinterpret(responsePtr, arena, null);
        MemorySegment errorMsgSeg = ZUSFBasicResponse_C.error_message(responsePtr);
        String errorMsg = FfmUtils.readString(errorMsgSeg, FfmUtils.MAX_METADATA_STRING_LENGTH);
        try {
            ZusfCApi.zusf_c_free_basic_response(responsePtr);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
        if (errorMsg != null) {
            throw new RuntimeException(errorMsg);
        }
    }

    public void createUssFile(String file, String mode) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment modeSeg = FfmUtils.allocateString(arena, mode);
            MemorySegment responsePtr = ZusfCApi.zusf_c_create_uss_file(fileSeg, modeSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void createUssDir(String file, String mode) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment modeSeg = FfmUtils.allocateString(arena, mode);
            MemorySegment responsePtr = ZusfCApi.zusf_c_create_uss_dir(fileSeg, modeSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void moveUssFileOrDir(String source, String destination) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment srcSeg = FfmUtils.allocateString(arena, source);
            MemorySegment destSeg = FfmUtils.allocateString(arena, destination);
            MemorySegment responsePtr = ZusfCApi.zusf_c_move_uss_file_or_dir(srcSeg, destSeg);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void chmodUssItem(String file, String mode, boolean recursive) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment modeSeg = FfmUtils.allocateString(arena, mode);
            MemorySegment responsePtr = ZusfCApi.zusf_c_chmod_uss_item(fileSeg, modeSeg, recursive);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void deleteUssItem(String file, boolean recursive) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment responsePtr = ZusfCApi.zusf_c_delete_uss_item(fileSeg, recursive);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void chownUssItem(String file, String owner, boolean recursive) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment ownerSeg = FfmUtils.allocateString(arena, owner);
            MemorySegment responsePtr = ZusfCApi.zusf_c_chown_uss_item(fileSeg, ownerSeg, recursive);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }

    public void chtagUssItem(String file, String tag, boolean recursive) throws Exception {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment fileSeg = FfmUtils.allocateString(arena, file);
            MemorySegment tagSeg = FfmUtils.allocateString(arena, tag);
            MemorySegment responsePtr = ZusfCApi.zusf_c_chtag_uss_item(fileSeg, tagSeg, recursive);
            handleBasicResponse(responsePtr, arena);
        } catch (Throwable e) {
            if (e instanceof Exception) throw (Exception) e;
            throw new RuntimeException(e);
        }
    }
}
