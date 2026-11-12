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

package org.zowe.zo.ffm;

import org.springframework.stereotype.Service;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.StructLayout;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;

import static java.lang.foreign.MemoryLayout.PathElement.groupElement;

/** FFM implementation of {@link ZoContentIo} over libzo_java.so's zo_content_c_api. */
@Service
public class ZoContentBindings implements ZoContentIo {

    private static final int OK = 0;
    private static final int NOT_FOUND = 2;
    private static final int ETAG_MISMATCH = 3;
    private static final int INVALID = 4;

    @Override
    public Content readDataset(String dsname, Options options) throws ZoRpcException {
        return read(Native.READ_DATASET, "readDataset", dsname, options);
    }

    @Override
    public WriteResult writeDataset(String dsname, byte[] data, Options options, String etag) throws ZoRpcException {
        return write(Native.WRITE_DATASET, "writeDataset", dsname, data, options, etag);
    }

    @Override
    public Content readFile(String path, Options options) throws ZoRpcException {
        return read(Native.READ_FILE, "readFile", path, options);
    }

    @Override
    public WriteResult writeFile(String path, byte[] data, Options options, String etag) throws ZoRpcException {
        return write(Native.WRITE_FILE, "writeFile", path, data, options, etag);
    }

    private Content read(MethodHandle fn, String op, String target, Options options) throws ZoRpcException {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment result = (MemorySegment) fn.invokeExact(
                    ascii(arena, target), Native.options(arena, options, null));
            return withResult(op, result, r -> {
                long length = (long) Native.LENGTH.get(r, 0L);
                MemorySegment data = (MemorySegment) Native.DATA.get(r, 0L);
                byte[] bytes = length == 0 ? new byte[0] : data.reinterpret(length).toArray(ValueLayout.JAVA_BYTE);
                return new Content(bytes, string((MemorySegment) Native.ETAG.get(r, 0L)));
            });
        } catch (ZoRpcException e) {
            throw e;
        } catch (Throwable t) {
            throw new ZoRpcException(op, ZoRpcException.INTERNAL_ERROR, "Native call failed", null);
        }
    }

    private WriteResult write(MethodHandle fn, String op, String target, byte[] data, Options options, String etag)
            throws ZoRpcException {
        if (data.length > FfmUtils.MAX_DATA_STRING_LENGTH) {
            throw new ZoRpcException(op, ZoRpcException.INVALID_PARAMS, "Content exceeds the maximum size", null);
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buffer = arena.allocate(Math.max(data.length, 1));
            MemorySegment.copy(data, 0, buffer, ValueLayout.JAVA_BYTE, 0, data.length);
            MemorySegment result = (MemorySegment) fn.invokeExact(
                    ascii(arena, target), buffer, (long) data.length, Native.options(arena, options, etag));
            return withResult(op, result, r -> new WriteResult(
                    string((MemorySegment) Native.ETAG.get(r, 0L)),
                    string((MemorySegment) Native.WARNING.get(r, 0L)),
                    (int) Native.CREATED.get(r, 0L) != 0));
        } catch (ZoRpcException e) {
            throw e;
        } catch (Throwable t) {
            throw new ZoRpcException(op, ZoRpcException.INTERNAL_ERROR, "Native call failed", null);
        }
    }

    private interface ResultReader<T> {
        T read(MemorySegment result);
    }

    private <T> T withResult(String op, MemorySegment raw, ResultReader<T> reader) throws Throwable {
        if (raw.address() == 0) {
            throw new ZoRpcException(op, ZoRpcException.INTERNAL_ERROR, "Out of memory in native layer", null);
        }
        MemorySegment r = raw.reinterpret(Native.RESULT.byteSize());
        try {
            int status = (int) Native.STATUS.get(r, 0L);
            if (status != OK) {
                String message = string((MemorySegment) Native.ERROR.get(r, 0L));
                int code = switch (status) {
                    case NOT_FOUND -> ZoRpcException.NOT_FOUND;
                    case ETAG_MISMATCH -> ZoRpcException.ETAG_MISMATCH;
                    case INVALID -> ZoRpcException.INVALID_PARAMS;
                    default -> ZoRpcException.INTERNAL_ERROR;
                };
                throw new ZoRpcException(op, code, message != null ? message : op + " failed", null);
            }
            return reader.read(r);
        } finally {
            Native.FREE.invokeExact(raw);
        }
    }

    private static MemorySegment ascii(Arena arena, String value) {
        return value == null ? MemorySegment.NULL : arena.allocateFrom(value, StandardCharsets.ISO_8859_1);
    }

    private static String string(MemorySegment segment) {
        return segment.address() == 0 ? null
                : segment.reinterpret(FfmUtils.MAX_METADATA_STRING_LENGTH).getString(0, StandardCharsets.ISO_8859_1);
    }

    private static final class Native {
        static final StructLayout OPTIONS = MemoryLayout.structLayout(
                ValueLayout.ADDRESS.withName("encoding"),
                ValueLayout.ADDRESS.withName("local_encoding"),
                ValueLayout.ADDRESS.withName("volser"),
                ValueLayout.ADDRESS.withName("etag"));
        static final StructLayout RESULT = MemoryLayout.structLayout(
                ValueLayout.JAVA_INT.withName("status"),
                ValueLayout.JAVA_INT.withName("created"),
                ValueLayout.ADDRESS.withName("data"),
                ValueLayout.JAVA_LONG.withName("length"),
                ValueLayout.ADDRESS.withName("etag"),
                ValueLayout.ADDRESS.withName("warning"),
                ValueLayout.ADDRESS.withName("error_message"));

        static final VarHandle STATUS = RESULT.varHandle(groupElement("status"));
        static final VarHandle CREATED = RESULT.varHandle(groupElement("created"));
        static final VarHandle DATA = RESULT.varHandle(groupElement("data"));
        static final VarHandle LENGTH = RESULT.varHandle(groupElement("length"));
        static final VarHandle ETAG = RESULT.varHandle(groupElement("etag"));
        static final VarHandle WARNING = RESULT.varHandle(groupElement("warning"));
        static final VarHandle ERROR = RESULT.varHandle(groupElement("error_message"));

        static final MethodHandle READ_DATASET;
        static final MethodHandle WRITE_DATASET;
        static final MethodHandle READ_FILE;
        static final MethodHandle WRITE_FILE;
        static final MethodHandle FREE;

        static {
            try {
                Class.forName("org.zowe.zo.ffm.NativeLoader");
            } catch (ClassNotFoundException e) {
                throw new ExceptionInInitializerError(e);
            }
            var linker = NativeLoader.LINKER;
            var lookup = NativeLoader.getLookup();
            FunctionDescriptor readDesc = FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS);
            FunctionDescriptor writeDesc = FunctionDescriptor.of(ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
            READ_DATASET = linker.downcallHandle(lookup.findOrThrow("zo_c_read_dataset"), readDesc);
            WRITE_DATASET = linker.downcallHandle(lookup.findOrThrow("zo_c_write_dataset"), writeDesc);
            READ_FILE = linker.downcallHandle(lookup.findOrThrow("zo_c_read_file"), readDesc);
            WRITE_FILE = linker.downcallHandle(lookup.findOrThrow("zo_c_write_file"), writeDesc);
            FREE = linker.downcallHandle(lookup.findOrThrow("zo_c_free_content_result"),
                    FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
        }

        static MemorySegment options(Arena arena, Options options, String etag) {
            Options o = options != null ? options : Options.DEFAULT;
            MemorySegment seg = arena.allocate(OPTIONS);
            seg.set(ValueLayout.ADDRESS, 0, ascii(arena, o.encoding()));
            seg.set(ValueLayout.ADDRESS, 8, ascii(arena, o.localEncoding()));
            seg.set(ValueLayout.ADDRESS, 16, ascii(arena, o.volume()));
            seg.set(ValueLayout.ADDRESS, 24, ascii(arena, etag));
            return seg;
        }
    }
}
