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

package org.zowe.zo.config;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.core.MethodParameter;
import org.springframework.http.MediaType;
import org.springframework.http.converter.HttpMessageConverter;
import org.springframework.http.server.ServerHttpRequest;
import org.springframework.http.server.ServerHttpResponse;
import org.springframework.stereotype.Component;
import org.springframework.web.bind.annotation.ControllerAdvice;
import org.springframework.web.filter.OncePerRequestFilter;
import org.springframework.web.servlet.mvc.method.annotation.ResponseBodyAdvice;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Opt-in (zo.diagnostics.server-timing) W3C Server-Timing header with per-request phase
 * durations, e.g. "native;dur=412.3, copy;dur=8.1, app;dur=530.0". Native call sites record
 * phases on the request thread; the header is added just before the body is written.
 */
public final class ServerTiming {

    private static final ThreadLocal<Map<String, Double>> PHASES = new ThreadLocal<>();
    private static final ThreadLocal<Long> START = new ThreadLocal<>();

    private ServerTiming() {
    }

    /** Adds the elapsed time since startNanos to a phase (no-op unless enabled for this request). */
    public static void record(String phase, long startNanos) {
        Map<String, Double> phases = PHASES.get();
        if (phases != null) {
            phases.merge(phase, (System.nanoTime() - startNanos) / 1e6, Double::sum);
        }
    }

    private static final java.lang.management.ThreadMXBean THREADS = java.lang.management.ManagementFactory.getThreadMXBean();

    /** Current thread CPU time in nanoseconds (includes native code on this thread), or -1. */
    public static long cpuNanos() {
        return PHASES.get() != null && THREADS.isCurrentThreadCpuTimeSupported() ? THREADS.getCurrentThreadCpuTime() : -1;
    }

    /** Records CPU consumed on this thread since cpuStart (from {@link #cpuNanos()}). */
    public static void recordCpu(String phase, long cpuStart) {
        Map<String, Double> phases = PHASES.get();
        if (phases != null && cpuStart >= 0) {
            phases.merge(phase, (THREADS.getCurrentThreadCpuTime() - cpuStart) / 1e6, Double::sum);
        }
    }

    static String header() {
        Map<String, Double> phases = PHASES.get();
        Long start = START.get();
        if (phases == null || start == null) return null;
        StringBuilder sb = new StringBuilder();
        // Locale.ROOT: the header grammar needs '.' decimals regardless of the JVM's default locale
        phases.forEach((k, v) -> sb.append(k).append(";dur=").append(String.format(java.util.Locale.ROOT, "%.1f", v)).append(", "));
        sb.append("app;dur=").append(String.format(java.util.Locale.ROOT, "%.1f", (System.nanoTime() - start) / 1e6));
        return sb.toString();
    }

    @Component
    public static class Filter extends OncePerRequestFilter {
        private final boolean enabled;

        public Filter(@Value("${zo.diagnostics.server-timing:false}") boolean enabled) {
            this.enabled = enabled;
        }

        @Override
        protected boolean shouldNotFilter(HttpServletRequest request) {
            return !enabled;
        }

        @Override
        protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
                throws ServletException, IOException {
            PHASES.set(new LinkedHashMap<>());
            START.set(System.nanoTime());
            try {
                chain.doFilter(request, response);
            } finally {
                PHASES.remove();
                START.remove();
            }
        }
    }

    @ControllerAdvice
    public static class Advice implements ResponseBodyAdvice<Object> {
        @Override
        public boolean supports(MethodParameter returnType, Class<? extends HttpMessageConverter<?>> converterType) {
            return true;
        }

        @Override
        public Object beforeBodyWrite(Object body, MethodParameter returnType, MediaType contentType,
                                      Class<? extends HttpMessageConverter<?>> converterType,
                                      ServerHttpRequest request, ServerHttpResponse response) {
            String value = header();
            if (value != null) response.getHeaders().add("Server-Timing", value);
            return body;
        }
    }
}
