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
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

import java.io.IOException;
import java.util.Set;

/**
 * z/OSMF-compatible CSRF guard: state-changing requests must carry X-CSRF-ZOSMF-HEADER, which
 * browsers cannot add cross-site without a CORS preflight. Zowe clients always send it.
 */
@Component
@Order(Ordered.HIGHEST_PRECEDENCE + 1)
public class CsrfHeaderFilter extends OncePerRequestFilter {

    public static final String HEADER = "X-CSRF-ZOSMF-HEADER";
    private static final Set<String> SAFE_METHODS = Set.of("GET", "HEAD", "OPTIONS", "TRACE");

    private final boolean required;

    public CsrfHeaderFilter(@Value("${zo.security.require-csrf-header:true}") boolean required) {
        this.required = required;
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest request) {
        String path = request.getRequestURI().substring(request.getContextPath().length());
        return !required || SAFE_METHODS.contains(request.getMethod())
                || !(path.startsWith("/zosmf/") || path.startsWith("/zo/"));
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
            throws ServletException, IOException {
        if (request.getHeader(HEADER) == null) {
            response.setStatus(HttpServletResponse.SC_FORBIDDEN);
            response.setContentType(MediaType.APPLICATION_JSON_VALUE);
            response.getWriter().write("{\"rc\":-1,\"message\":\"Request is missing the " + HEADER
                    + " header\",\"details\":[]}");
            return;
        }
        chain.doFilter(request, response);
    }
}
