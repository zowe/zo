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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * FFM implementation of {@link ZoRpcClient} over libzo_java.so's zo_c_rpc_call.
 *
 * Wire format is ISO-8859-1 JSON. Non-ASCII Latin-1 characters are sent unescaped so the
 * native ISO8859-1 to IBM-1047 conversion maps them; zo's JSON parser would otherwise turn a
 * {@code \\u00XX} escape into the raw byte 0xXX, which is a different character in EBCDIC.
 * Characters outside Latin-1 cannot be represented and are rejected before dispatch. Native log records are forwarded to the
 * "org.zowe.zo.native" logger on the calling thread, so MDC (user, request id) applies.
 */
@Slf4j
@Service
public class ZoRpcBindings implements ZoRpcClient {

    private static final Logger NATIVE_LOG = LoggerFactory.getLogger("org.zowe.zo.native");
    // Bound for a single JSON-RPC response; content-heavy methods stay on the typed shims.
    private static final long MAX_RESPONSE_LENGTH = FfmUtils.MAX_DATA_STRING_LENGTH;

    private final ObjectMapper mapper = JsonMapper.builder().build();
    private final AtomicInteger nextId = new AtomicInteger(1);

    @Override
    public JsonNode call(String method, Object params) throws ZoRpcException {
        ObjectNode request = mapper.createObjectNode();
        request.put("jsonrpc", "2.0");
        request.put("method", method);
        if (params != null) {
            request.set("params", mapper.valueToTree(params));
        }
        request.put("id", nextId.getAndUpdate(i -> i == Integer.MAX_VALUE ? 1 : i + 1));

        String requestJson;
        try {
            requestJson = mapper.writeValueAsString(request);
        } catch (Exception e) {
            throw new IllegalArgumentException("Request parameters could not be serialized");
        }
        if (!requestJson.chars().allMatch(c -> c <= 0xFF)) {
            throw new IllegalArgumentException("Request contains characters that cannot be represented in IBM-1047");
        }

        JsonNode response;
        try {
            response = mapper.readTree(Native.call(requestJson));
        } catch (ZoRpcException e) {
            throw e;
        } catch (Exception e) {
            throw new ZoRpcException(method, ZoRpcException.INTERNAL_ERROR, "Invalid response from native layer", null);
        }

        JsonNode error = response.get("error");
        if (error != null && !error.isNull()) {
            throw new ZoRpcException(method, error.path("code").asInt(ZoRpcException.INTERNAL_ERROR),
                    error.path("message").asText("Command failed (" + method + ")"), error.get("data"));
        }
        JsonNode result = response.get("result");
        return result != null ? result : mapper.createObjectNode();
    }

    /** Native entry points, initialized on first use so the bean can be created off z/OS. */
    private static final class Native {
        private static final MethodHandle RPC_CALL;
        private static final MethodHandle FREE;
        private static final MethodHandle SET_LOG_LEVEL;

        static {
            try {
                Class.forName("org.zowe.zo.ffm.NativeLoader");
            } catch (ClassNotFoundException e) {
                throw new ExceptionInInitializerError(e);
            }
            Linker linker = NativeLoader.LINKER;
            var lookup = NativeLoader.getLookup();
            RPC_CALL = linker.downcallHandle(lookup.findOrThrow("zo_c_rpc_call"),
                    FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
            FREE = linker.downcallHandle(lookup.findOrThrow("zo_c_free"),
                    FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
            SET_LOG_LEVEL = linker.downcallHandle(lookup.findOrThrow("zo_c_set_log_level"),
                    FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT));
            MethodHandle init = linker.downcallHandle(lookup.findOrThrow("zo_c_init"),
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
            try {
                int rc = (int) init.invokeExact(createLogUpcall(linker), nativeLevel());
                if (rc != 0) throw new IllegalStateException("zo_c_init failed rc=" + rc);
            } catch (Throwable t) {
                throw new ExceptionInInitializerError(t);
            }
        }

        static String call(String requestJson) throws ZoRpcException {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment req = arena.allocateFrom(requestJson, StandardCharsets.ISO_8859_1);
                MemorySegment resp = (MemorySegment) RPC_CALL.invokeExact(req);
                if (resp.address() == 0) {
                    throw new ZoRpcException(null, ZoRpcException.INTERNAL_ERROR, "Out of memory in native layer", null);
                }
                try {
                    return resp.reinterpret(MAX_RESPONSE_LENGTH).getString(0, StandardCharsets.ISO_8859_1);
                } finally {
                    FREE.invokeExact(resp);
                }
            } catch (ZoRpcException e) {
                throw e;
            } catch (Throwable t) {
                log.error("zo_c_rpc_call failed", t);
                throw new ZoRpcException(null, ZoRpcException.INTERNAL_ERROR, "Native call failed", null);
            }
        }

        static void refreshLogLevel() {
            try {
                SET_LOG_LEVEL.invokeExact(nativeLevel());
            } catch (Throwable t) {
                log.warn("Unable to update native log level", t);
            }
        }

        private static MemorySegment createLogUpcall(Linker linker) {
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(Native.class, "onNativeLog",
                        MethodType.methodType(void.class, int.class, MemorySegment.class));
                return linker.upcallStub(target,
                        FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS), Arena.global());
            } catch (Throwable t) {
                log.warn("Native log forwarding unavailable; native log records will be discarded", t);
                return MemorySegment.NULL;
            }
        }

        // zlog_level_t: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=FATAL 6=OFF
        private static int nativeLevel() {
            if (NATIVE_LOG.isTraceEnabled()) return 0;
            if (NATIVE_LOG.isDebugEnabled()) return 1;
            if (NATIVE_LOG.isInfoEnabled()) return 2;
            if (NATIVE_LOG.isWarnEnabled()) return 3;
            if (NATIVE_LOG.isErrorEnabled()) return 4;
            return 6;
        }

        @SuppressWarnings("unused") // invoked from native code
        private static void onNativeLog(int level, MemorySegment message) {
            try {
                String text = message.reinterpret(FfmUtils.MAX_METADATA_STRING_LENGTH * 8)
                        .getString(0, StandardCharsets.ISO_8859_1);
                switch (level) {
                    case 0 -> NATIVE_LOG.trace(text);
                    case 1 -> NATIVE_LOG.debug(text);
                    case 2 -> NATIVE_LOG.info(text);
                    case 3 -> NATIVE_LOG.warn(text);
                    default -> NATIVE_LOG.error(text);
                }
            } catch (Throwable ignored) {
                // An exception escaping an upcall terminates the JVM
            }
        }
    }

    /**
     * Loads and initializes libzo_java.so if needed and reports whether it is usable. Static so
     * health checks can call it without a user security context (Spring AOP only advises beans).
     */
    public static boolean nativeReady() {
        try {
            Class.forName(Native.class.getName(), true, Native.class.getClassLoader());
            return true;
        } catch (Throwable t) {
            log.warn("zo native library is not available: {}", t.toString());
            return false;
        }
    }

    /** Re-read the "org.zowe.zo.native" logger level and push it to the native layer. */
    public void refreshNativeLogLevel() {
        Native.refreshLogLevel();
    }
}
