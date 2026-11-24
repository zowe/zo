package org.zowe.zo.config;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.zos.security.platform.PlatformReturned;
import org.zowe.zo.zos.security.platform.SafPlatformError;
import org.zowe.zo.zos.security.service.AccessControlError;
import org.zowe.zo.zos.security.service.SecurityRequestFailed;

import java.util.Map;

import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.containsString;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

class GlobalExceptionHandlerTest {

    @RestController
    @RequestMapping("/test")
    static class ThrowingController {

        @GetMapping("/illegal-arg")
        public void illegalArg() {
            throw new IllegalArgumentException("bad parameter");
        }

        @GetMapping("/access-control")
        public void accessControl() {
            throw new AccessControlError("access denied to resource", PlatformReturned.builder().build());
        }

        @GetMapping("/security-request")
        public void securityRequest() {
            throw new SecurityRequestFailed("IRRSIA00", 1, 8);
        }

        @GetMapping("/saf-platform")
        public void safPlatform() {
            throw new SafPlatformError("SAF platform failure");
        }

        @GetMapping("/runtime-wrapping-access-control")
        public void runtimeWrappingAccessControl() {
            throw new RuntimeException(new AccessControlError("wrapped: no permission", PlatformReturned.builder().build()));
        }

        @GetMapping("/runtime-wrapping-security-request")
        public void runtimeWrappingSecurityRequest() {
            throw new RuntimeException(new SecurityRequestFailed("MODULE", 2, 4));
        }

        @GetMapping("/runtime-wrapping-illegal-arg")
        public void runtimeWrappingIllegalArg() {
            throw new RuntimeException(new IllegalArgumentException("wrapped: bad arg"));
        }

        @GetMapping("/runtime-unknown")
        public void runtimeUnknown() {
            throw new RuntimeException("unexpected runtime failure");
        }

        @GetMapping("/runtime-null-message")
        public void runtimeNullMessage() {
            throw new RuntimeException((String) null);
        }

        @GetMapping("/checked")
        public void checked() throws Exception {
            throw new Exception("checked exception message");
        }

        @GetMapping("/needs-param")
        public void needsParam(@RequestParam("count") int count) {
        }

        @PostMapping(value = "/json-body", consumes = MediaType.APPLICATION_JSON_VALUE)
        public void jsonBody(@RequestBody Map<String, Object> body) {
        }

        @GetMapping("/deep-rpc")
        public void deepRpc() {
            var rpc = new ZoRpcException("listDatasets", ZoRpcException.NOT_FOUND, "gone", null);
            throw new RuntimeException(new RuntimeException(rpc));
        }

        @DeleteMapping("/checked-null-message")
        public void checkedNullMessage() throws Exception {
            throw new Exception();
        }
    }

    MockMvc mockMvc;

    @BeforeEach
    void setup() {
        mockMvc = MockMvcBuilders
                .standaloneSetup(new ThrowingController())
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    void illegalArgumentException_returns400WithMessage() throws Exception {
        mockMvc.perform(get("/test/illegal-arg"))
               .andExpect(status().isBadRequest())
               .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
               .andExpect(jsonPath("$.message").value("bad parameter"));
    }

    @Test
    void accessControlError_returns403WithMessage() throws Exception {
        mockMvc.perform(get("/test/access-control"))
               .andExpect(status().isForbidden())
               .andExpect(jsonPath("$.message").value("access denied to resource"));
    }

    @Test
    void securityRequestFailed_returns403WithMessage() throws Exception {
        mockMvc.perform(get("/test/security-request"))
               .andExpect(status().isForbidden())
               .andExpect(jsonPath("$.message").value(
                       "Platform security request has failed: module=IRRSIA00, function=1, errno=8"));
    }

    @Test
    void safPlatformError_returns500WithMessage() throws Exception {
        mockMvc.perform(get("/test/saf-platform"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("SAF platform failure"));
    }

    @Test
    void runtimeWrappingAccessControl_returns403() throws Exception {
        mockMvc.perform(get("/test/runtime-wrapping-access-control"))
               .andExpect(status().isForbidden())
               .andExpect(jsonPath("$.message").value("wrapped: no permission"));
    }

    @Test
    void runtimeWrappingSecurityRequest_returns403() throws Exception {
        mockMvc.perform(get("/test/runtime-wrapping-security-request"))
               .andExpect(status().isForbidden())
               .andExpect(jsonPath("$.message").value(
                       "Platform security request has failed: module=MODULE, function=2, errno=4"));
    }

    @Test
    void runtimeWrappingIllegalArg_returns400() throws Exception {
        mockMvc.perform(get("/test/runtime-wrapping-illegal-arg"))
               .andExpect(status().isBadRequest())
               .andExpect(jsonPath("$.message").value("wrapped: bad arg"));
    }

    @Test
    void runtimeUnknown_returns500WithoutLeakingMessage() throws Exception {
        mockMvc.perform(get("/test/runtime-unknown"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("An unexpected error occurred"))
               .andExpect(jsonPath("$.rc").value(-1))
               .andExpect(jsonPath("$.details").isArray());
    }

    @Test
    void runtimeWithNullMessage_returns500WithFallback() throws Exception {
        mockMvc.perform(get("/test/runtime-null-message"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("An unexpected error occurred"));
    }

    @Test
    void checkedException_returns500WithoutLeakingMessage() throws Exception {
        mockMvc.perform(get("/test/checked"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("An unexpected error occurred"));
    }

    @Test
    void checkedExceptionWithNullMessage_returns500WithFallback() throws Exception {
        mockMvc.perform(delete("/test/checked-null-message"))
               .andExpect(status().isInternalServerError())
               .andExpect(jsonPath("$.message").value("An unexpected error occurred"));
    }

    @Test
    void missingParameter_returns400() throws Exception {
        mockMvc.perform(get("/test/needs-param")).andExpect(status().isBadRequest());
    }

    @Test
    void typeMismatch_returns400NamingParameter() throws Exception {
        mockMvc.perform(get("/test/needs-param").param("count", "abc"))
               .andExpect(status().isBadRequest())
               .andExpect(jsonPath("$.message").value("Invalid value for parameter 'count'"));
    }

    @Test
    void malformedBody_returns400WithoutEchoingContent() throws Exception {
        mockMvc.perform(post("/test/json-body").contentType(MediaType.APPLICATION_JSON).content("{\"password\":\"s3cret"))
               .andExpect(status().isBadRequest())
               .andExpect(content().string(not(containsString("s3cret"))));
    }

    @Test
    void unsupportedMediaType_returns415() throws Exception {
        mockMvc.perform(post("/test/json-body").contentType(MediaType.TEXT_PLAIN).content("x"))
               .andExpect(status().isUnsupportedMediaType());
    }

    @Test
    void wrongMethod_returns405() throws Exception {
        mockMvc.perform(put("/test/illegal-arg")).andExpect(status().isMethodNotAllowed());
    }

    @Test
    void deeplyWrappedRpcException_mapsToItsStatus() throws Exception {
        mockMvc.perform(get("/test/deep-rpc"))
               .andExpect(status().isNotFound())
               .andExpect(jsonPath("$.rc").value(ZoRpcException.NOT_FOUND));
    }
}
