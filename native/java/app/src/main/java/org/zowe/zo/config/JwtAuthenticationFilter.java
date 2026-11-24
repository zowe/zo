package org.zowe.zo.config;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;
import org.zowe.apiml.zaasclient.exception.ZaasClientException;
import org.zowe.apiml.zaasclient.service.ZaasClient;

import java.io.IOException;

@Component
public class JwtAuthenticationFilter extends OncePerRequestFilter {

    private final ZaasClient zaasClient;

    public JwtAuthenticationFilter(ZaasClient zaasClient) {
        this.zaasClient = zaasClient;
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain filterChain)
            throws ServletException, IOException {

        final String authHeader = request.getHeader("Authorization");

        if (authHeader == null || !authHeader.startsWith("Bearer ")) {
            filterChain.doFilter(request, response);
            return;
        }

        final String jwt = authHeader.substring(7);

        try {
            var tokenResult = zaasClient.query(jwt);

            // Must use the 3-arg constructor to create an authenticated token.
            // Calling setAuthenticated(true) on a 2-arg token throws
            // IllegalArgumentException.
            var authentication = new UsernamePasswordAuthenticationToken(
                    tokenResult.getUserId(),
                    jwt,
                    java.util.Collections.emptyList());

            SecurityContextHolder.getContext().setAuthentication(authentication);
        } catch (ZaasClientException e) {
            System.out.println("Error: " + e.getMessage());
            // Validation failed. Clear the context to be safe.
            SecurityContextHolder.clearContext();
            // Get the return code safely
            int statusCode = e.getErrorCode() != null ? e.getErrorCode().getReturnCode()
                    : HttpServletResponse.SC_UNAUTHORIZED;
            String returnCodeStr = e.getErrorCode() != null ? String.valueOf(statusCode) : "UNKNOWN";

            // If the status code is a 4xx client error, map it to 401 Unauthorized
            if (statusCode >= 400 && statusCode < 500) {
                statusCode = HttpServletResponse.SC_UNAUTHORIZED;
            }

            // Set the response status according to the mapped returnCode
            response.setStatus(statusCode);
            response.setContentType("application/json;charset=UTF-8");

            // Safely escape the message (basic escaping for JSON)
            String errorMessage = e.getMessage() != null ? e.getMessage().replace("\"", "\\\"")
                    : "Authentication failed";

            // Write the exception message and return code to the response body
            response.getWriter()
                    .write(String.format("{\"error\": \"%s\", \"returnCode\": \"%s\"}", errorMessage, returnCodeStr));
            response.getWriter().flush();

            // Return immediately so the request doesn't continue down the filter chain
            return;
        }

        try {
            // Continue the filter chain. If authentication was not set, Spring Security
            // will block the request before it reaches the controller.
            filterChain.doFilter(request, response);
        } finally {
            // This block is guaranteed to run after the controller returns and the response is written,
            // right before the thread is returned to the thread pool.
            // 
            // Example: ThreadLocalContext.clear(); or any other thread-local cleanup.
            // SecurityContextHolder.clearContext() is usually handled by Spring Security's 
            // SecurityContextPersistenceFilter, but you can do your own custom thread cleanup here.
        }
    }
}
