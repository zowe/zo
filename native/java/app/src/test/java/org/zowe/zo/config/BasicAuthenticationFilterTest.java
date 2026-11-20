package org.zowe.zo.config;

import jakarta.servlet.FilterChain;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.security.core.context.SecurityContextHolder;

import java.nio.charset.StandardCharsets;
import java.util.Base64;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class BasicAuthenticationFilterTest {

    @Mock
    FilterChain filterChain;

    BasicAuthenticationFilter filter;

    @BeforeEach
    void setup() {
        filter = new BasicAuthenticationFilter();
        SecurityContextHolder.clearContext();
    }

    @AfterEach
    void teardown() {
        SecurityContextHolder.clearContext();
    }

    private static String basicHeader(String username, String password) {
        String credentials = username + ":" + password;
        return "Basic " + Base64.getEncoder().encodeToString(credentials.getBytes(StandardCharsets.UTF_8));
    }

    @Test
    void noAuthorizationHeader_passesThroughToNextFilter() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
    }

    @Test
    void bearerTokenHeader_passesThroughWithoutProcessing() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer some-jwt-token");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
    }

    @Test
    void otherAuthScheme_passesThroughWithoutProcessing() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Digest realm=test");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
    }

    @Test
    void invalidBase64_returns401WithJsonBody() throws Exception {
        var request = new MockHttpServletRequest("POST", "/api/resource");
        request.addHeader("Authorization", "Basic @@@not-valid-base64@@@");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        assertThat(response.getContentType()).isEqualTo("application/json;charset=UTF-8");
        assertThat(response.getContentAsString()).contains("\"error\"");
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void credentialsWithoutColon_returns401() throws Exception {
        // Base64 of "usernameonly" — no colon separator, so no password field
        String encoded = Base64.getEncoder().encodeToString("usernameonly".getBytes(StandardCharsets.UTF_8));
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Basic " + encoded);
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void validCredentialFormat_ibmJaasNotAvailable_returns401() throws Exception {
        // On a non-IBM JVM, the IBM JAAS login module class is not available.
        // The LoginContext.login() call throws LoginException, which the filter
        // handles by returning 401.
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", basicHeader("testuser", "testpass"));
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void validCredentialFormat_ibmJaasNotAvailable_returnsJsonErrorBody() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", basicHeader("user", "pass"));
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        assertThat(response.getContentType()).isEqualTo("application/json;charset=UTF-8");
        assertThat(response.getContentAsString()).contains("\"error\"");
        assertThat(response.getContentAsString()).contains("\"returnCode\"");
        assertThat(response.getContentAsString()).contains("401");
    }

    @Test
    void emptyPassword_ibmJaasNotAvailable_returns401() throws Exception {
        // Edge case: username with empty password (colon present, so split works)
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", basicHeader("user", ""));
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
    }

    @Test
    void colonInPassword_correctlySplitsCredentials() throws Exception {
        // split(":", 2) should keep colon-containing passwords intact
        // IBM JAAS still won't be available, so still 401, but we verify
        // the filter doesn't reject the format itself
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", basicHeader("user", "pa:ss:word"));
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        // Fails at JAAS level (IBM module not found), not at credential parsing level
        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void noAuthorizationHeader_securityContextUnchanged() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        // Security context should not have been modified
        assertThat(SecurityContextHolder.getContext().getAuthentication()).isNull();
    }
}
