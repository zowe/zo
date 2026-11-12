package org.zowe.zo.config;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.WebMvcTest;
import org.springframework.context.annotation.Import;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;
import org.zowe.apiml.zaasclient.exception.ZaasClientErrorCodes;
import org.zowe.apiml.zaasclient.exception.ZaasClientException;
import org.zowe.apiml.zaasclient.service.ZaasClient;
import org.zowe.apiml.zaasclient.service.ZaasToken;
import org.zowe.zo.controllers.ZdsController;
import org.zowe.zo.ffm.ZdsBindings;
import org.zowe.zo.ffm.ZdsService;

import java.util.List;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

/**
 * Integration tests verifying that the JWT and Basic authentication filters
 * are wired into the Spring Security filter chain.
 * <p>
 * These tests exercise the full filter chain: no Spring Security mocking
 * via {@code @WithMockUser} — the request must pass through
 * {@link JwtAuthenticationFilter} and {@link BasicAuthenticationFilter}.
 */
@WebMvcTest(ZdsController.class)
@Import({JwtAuthenticationFilter.class, BasicAuthenticationFilter.class, SecurityConfig.class})
class AuthenticationIntegrationTest {

    @Autowired
    MockMvc mockMvc;

    @MockitoBean
    ZaasClient zaasClient;

    @MockitoBean
    ZdsService zdsService;

    private static final String LIST_DS = "/zosmf/restfiles/ds";

    // -------------------------------------------------------------------------
    // Unauthenticated requests
    // -------------------------------------------------------------------------

    @Test
    void noCredentials_requestIsRejected() throws Exception {
        mockMvc.perform(get(LIST_DS).param("dslevel", "USER"))
               .andExpect(status().is4xxClientError());
    }

    // -------------------------------------------------------------------------
    // JWT authentication
    // -------------------------------------------------------------------------

    @Test
    void validBearerToken_requestIsAuthenticated() throws Exception {
        var token = new ZaasToken();
        token.setUserId("jwtuser");
        when(zaasClient.query("valid.jwt.token")).thenReturn(token);

        var entry = new ZdsBindings.ZDSEntry();
        entry.name = "JWTUSER.DS";
        when(zdsService.listDataSets(any())).thenReturn(List.of(entry));

        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "JWTUSER")
                .header("Authorization", "Bearer valid.jwt.token"))
               .andExpect(status().isOk())
               .andExpect(jsonPath("$.returnedRows").value(1));
    }

    @Test
    void invalidJwtToken_returns401() throws Exception {
        when(zaasClient.query("expired.token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.INVALID_JWT_TOKEN));

        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Bearer expired.token"))
               .andExpect(status().isUnauthorized())
               .andExpect(content().contentTypeCompatibleWith("application/json"))
               .andExpect(jsonPath("$.error").exists());
    }

    @Test
    void expiredJwtToken_returns401() throws Exception {
        when(zaasClient.query("stale.token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.EXPIRED_JWT_EXCEPTION));

        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Bearer stale.token"))
               .andExpect(status().isUnauthorized());
    }

    @Test
    void zaasServiceUnavailable_returnsServerError() throws Exception {
        when(zaasClient.query("some.token"))
                .thenThrow(new ZaasClientException(ZaasClientErrorCodes.SERVICE_UNAVAILABLE));

        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Bearer some.token"))
               .andExpect(status().is5xxServerError());
    }

    @Test
    void bearerTokenWithDifferentUser_authenticatesCorrectUser() throws Exception {
        var token = new ZaasToken();
        token.setUserId("alice");
        when(zaasClient.query("alice.token")).thenReturn(token);
        when(zdsService.listDataSets(any())).thenReturn(List.of());

        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "ALICE")
                .header("Authorization", "Bearer alice.token"))
               .andExpect(status().isOk());
    }

    // -------------------------------------------------------------------------
    // Basic authentication (IBM JAAS not available on non-z/OS JVM)
    // -------------------------------------------------------------------------

    @Test
    void basicAuthCredentials_ibmJaasUnavailable_returns401() throws Exception {
        // On a non-IBM JVM, the JAASLoginModule is not on the classpath,
        // so BasicAuthenticationFilter always returns 401 for Basic credentials.
        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Basic dXNlcjpwYXNzd29yZA==")) // user:password
               .andExpect(status().isUnauthorized())
               .andExpect(content().contentTypeCompatibleWith("application/json"));
    }

    @Test
    void invalidBase64BasicCredentials_returns401() throws Exception {
        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Basic @@@not-base64@@@"))
               .andExpect(status().isUnauthorized());
    }

    // -------------------------------------------------------------------------
    // Filter pass-through: non-matching headers
    // -------------------------------------------------------------------------

    @Test
    void otherAuthScheme_doesNotAuthenticate_andIsRejected() throws Exception {
        // Neither JWT nor Basic auth filter handles custom schemes;
        // the request passes through both filters unauthenticated,
        // then Spring Security rejects it.
        mockMvc.perform(get(LIST_DS)
                .param("dslevel", "USER")
                .header("Authorization", "Custom some-custom-scheme"))
               .andExpect(status().is4xxClientError());
    }
}
