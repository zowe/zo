package org.zowe.zo.ffm;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

public class FfmUtils {
    // Bound for short, fixed-format fields (error messages, dataset/member names, dsorg/recfm/volser).
    // error_message is backed by a native char[256] buffer (ZDIAG.e_msg) and dataset names are capped
    // at 44 chars by z/OS convention, so 1 KB is generous headroom while still failing fast (instead of
    // scanning into unmapped memory) if a buffer is ever corrupted or not null-terminated.
    public static final long MAX_METADATA_STRING_LENGTH = 1024L;

    // Bound for full data set content (readDataSet), which the native reader loads into memory with no
    // size limit of its own. 100 MB is a starting operational ceiling, not a value derived from the native
    // code - adjust to match the largest data set this service is expected to serve.
    public static final long MAX_DATA_STRING_LENGTH = 100L * 1024 * 1024;

    public static String readString(MemorySegment segment, long maxLen) {
        if (segment == null || segment.address() == 0) {
            return null;
        }
        // Bound the reinterpret so getString's null-terminator scan can't run past maxLen.
        return segment.reinterpret(maxLen).getString(0);
    }

    public static MemorySegment allocateString(Arena arena, String str) {
        if (str == null) return MemorySegment.NULL;
        return arena.allocateFrom(str);
    }
}
