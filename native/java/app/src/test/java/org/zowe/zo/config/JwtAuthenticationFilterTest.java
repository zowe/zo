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
import org.zowe.apiml.zaasclient.exception.ZaasClientErrorCodes;
import org.zowe.apiml.zaasclient.exception.ZaasClientException;
import org.zowe.apiml.zaasclient.service.ZaasClient;
import org.zowe.apiml.zaasclient.service.ZaasToken;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class JwtAuthenticationFilterTest {

    @Mock
    ZaasClient zaasClient;

    @Mock
    FilterChain filterChain;

    JwtAuthenticationFilter filter;

    @BeforeEach
    void setup() {
        filter = new JwtAuthenticationFilter(zaasClient);
        SecurityContextHolder.clearContext();
    }

    @AfterEach
    void teardown() {
        SecurityContextHolder.clearContext();
    }

    @Test
    void noAuthorizationHeader_passesThroughToNextFilter() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
        verifyNoInteractions(zaasClient);
        assertThat(SecurityContextHolder.getContext().getAuthentication()).isNull();
    }

    @Test
    void basicAuthorizationHeader_passesThroughWithoutProcessing() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Basic dXNlcjpwYXNz");
        var response = new MockHttpServletResponse();

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
        verifyNoInteractions(zaasClient);
    }

    @Test
    void validBearerToken_setsAuthenticationAndContinuesChain() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer valid-token-abc");
        var response = new MockHttpServletResponse();

        var token = new ZaasToken();
        token.setUserId("testuser");
        when(zaasClient.query("valid-token-abc")).thenReturn(token);

        filter.doFilter(request, response, filterChain);

        verify(filterChain).doFilter(request, response);
        var auth = SecurityContextHolder.getContext().getAuthentication();
        assertThat(auth).isNotNull();
        assertThat(auth.getName()).isEqualTo("testuser");
        assertThat(auth.isAuthenticated()).isTrue();
    }

    @Test
    void validBearerToken_storesJwtAsCredential() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer my-jwt");
        var response = new MockHttpServletResponse();

        var token = new ZaasToken();
        token.setUserId("user1");
        when(zaasClient.query("my-jwt")).thenReturn(token);

        filter.doFilter(request, response, filterChain);

        var auth = SecurityContextHolder.getContext().getAuthentication();
        assertThat(auth.getCredentials()).isEqualTo("my-jwt");
    }

    @Test
    void invalidJwtToken_returns401AndDoesNotContinueChain() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer tampered-token");
        var response = new MockHttpServletResponse();

        when(zaasClient.query("tampered-token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.INVALID_JWT_TOKEN));

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
        assertThat(SecurityContextHolder.getContext().getAuthentication()).isNull();
    }

    @Test
    void expiredToken_returns401() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer expired-token");
        var response = new MockHttpServletResponse();

        when(zaasClient.query("expired-token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.EXPIRED_JWT_EXCEPTION));

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void serviceUnavailable_returnsNon4xxMappedStatus() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer some-token");
        var response = new MockHttpServletResponse();

        when(zaasClient.query("some-token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.SERVICE_UNAVAILABLE));

        filter.doFilter(request, response, filterChain);

        // SERVICE_UNAVAILABLE has a 5xx return code — not remapped to 401
        assertThat(response.getStatus()).isGreaterThanOrEqualTo(500);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void invalidToken_responseBodyContainsJsonError() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer bad-token");
        var response = new MockHttpServletResponse();

        when(zaasClient.query("bad-token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.INVALID_JWT_TOKEN, "Token validation failed"));

        filter.doFilter(request, response, filterChain);

        assertThat(response.getContentType()).isEqualTo("application/json;charset=UTF-8");
        assertThat(response.getContentAsString()).contains("\"error\"");
    }

    @Test
    void nullErrorCode_returns401() throws Exception {
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer some-token");
        var response = new MockHttpServletResponse();

        ZaasClientException ex = mock(ZaasClientException.class);
        when(ex.getErrorCode()).thenReturn(null);
        when(zaasClient.query("some-token")).thenThrow(ex);

        filter.doFilter(request, response, filterChain);

        assertThat(response.getStatus()).isEqualTo(401);
        verify(filterChain, never()).doFilter(any(), any());
    }

    @Test
    void bearerTokenExtractsEverythingAfterBearerPrefix() throws Exception {
        // Ensures the token value includes the full string after "Bearer "
        var request = new MockHttpServletRequest("GET", "/api/resource");
        request.addHeader("Authorization", "Bearer token.with.dots.123");
        var response = new MockHttpServletResponse();

        var token = new ZaasToken();
        token.setUserId("svc-account");
        when(zaasClient.query("token.with.dots.123")).thenReturn(token);

        filter.doFilter(request, response, filterChain);

        verify(zaasClient).query("token.with.dots.123");
        assertThat(response.getStatus()).isEqualTo(200);
    }
}
