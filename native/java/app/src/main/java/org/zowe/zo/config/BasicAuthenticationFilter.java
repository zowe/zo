package org.zowe.zo.config;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

import javax.security.auth.Subject;
import javax.security.auth.callback.Callback;
import javax.security.auth.callback.CallbackHandler;
import javax.security.auth.callback.NameCallback;
import javax.security.auth.callback.PasswordCallback;
import javax.security.auth.callback.UnsupportedCallbackException;
import javax.security.auth.login.LoginException;

import javax.security.auth.login.AppConfigurationEntry;
import javax.security.auth.login.Configuration;
import javax.security.auth.login.LoginContext;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.HashMap;

@Component
public class BasicAuthenticationFilter extends OncePerRequestFilter {

    public BasicAuthenticationFilter() {
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain filterChain)
            throws ServletException, IOException {

        final String authHeader = request.getHeader("Authorization");

        if (authHeader == null || !authHeader.startsWith("Basic ")) {
            filterChain.doFilter(request, response);
            return;
        }

        LoginContext loginContext = null;
        try {
            // Decode Basic Auth header
            String base64Credentials = authHeader.substring("Basic ".length()).trim();
            byte[] credDecoded = Base64.getDecoder().decode(base64Credentials);
            String credentials = new String(credDecoded, StandardCharsets.UTF_8);
            
            final String[] values = credentials.split(":", 2);
            if (values.length != 2) {
                throw new LoginException("Invalid Basic authentication token");
            }
            
            final String username = values[0];
            final String password = values[1];

            // Use LoginContext with a custom Configuration to avoid compile-time dependency on IBM classes
            Configuration config = new Configuration() {
                @Override
                public AppConfigurationEntry[] getAppConfigurationEntry(String name) {
                    HashMap<String, String> options = new HashMap<>();
                    options.put("debug", "true");
                    return new AppConfigurationEntry[] {
                        new AppConfigurationEntry(
                            "com.ibm.security.auth.module.JAASLoginModule",
                            AppConfigurationEntry.LoginModuleControlFlag.REQUIRED,
                            options
                        )
                    };
                }
            };

            CallbackHandler callbackHandler = new CallbackHandler() {
                @Override
                public void handle(Callback[] callbacks) throws IOException, UnsupportedCallbackException {
                    for (Callback callback : callbacks) {
                        if (callback instanceof NameCallback) {
                            ((NameCallback) callback).setName(username);
                        } else if (callback instanceof PasswordCallback) {
                            ((PasswordCallback) callback).setPassword(password.toCharArray());
                        } else {
                            throw new UnsupportedCallbackException(callback, "Unrecognized Callback");
                        }
                    }
                }
            };
            
            Subject subject = new Subject();
            loginContext = new LoginContext("ZoweLogin", subject, callbackHandler, config);
            
            // This will instantiate the IBM JAASLoginModule and call login() and commit()
            loginContext.login();
            
            // The JAASLoginModule should have populated the Subject with Principals.
            // We can extract the authenticated username from the Principal.
            String authenticatedUser = username; // Fallback
            if (!subject.getPrincipals().isEmpty()) {
                authenticatedUser = subject.getPrincipals().iterator().next().getName();
            }
            
            var authentication = new UsernamePasswordAuthenticationToken(
                    authenticatedUser,
                    null, // We don't need to store the credentials in the context
                    java.util.Collections.emptyList());
            System.out.println("User authenticated: " + authenticatedUser);
            SecurityContextHolder.getContext().setAuthentication(authentication);
            
        } catch (LoginException | IllegalArgumentException e) {
            e.printStackTrace();
            System.out.println("Basic Auth Error: " + e.getMessage());
            SecurityContextHolder.clearContext();

            response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
            response.setContentType("application/json;charset=UTF-8");

            String errorMessage = e.getMessage() != null ? e.getMessage().replace("\"", "\\\"")
                    : "Authentication failed";

            response.getWriter()
                    .write(String.format("{\"error\": \"%s\", \"returnCode\": \"401\"}", errorMessage));
            response.getWriter().flush();

            return;
        }

        try {
            filterChain.doFilter(request, response);
        } finally {
            // Cleanup: log out the JAAS context to revert the OS thread identity
            if (loginContext != null) {
                try {
                    loginContext.logout();
                } catch (LoginException e) {
                    System.err.println("Error during JAAS logout: " + e.getMessage());
                }
            }
            SecurityContextHolder.clearContext();
        }
    }
}
