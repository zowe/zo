package org.zowe.zo.config;

import lombok.extern.slf4j.Slf4j;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.zowe.zo.zos.security.platform.SafPlatformError;
import org.zowe.zo.zos.security.service.AccessControlError;
import org.zowe.zo.zos.security.service.SecurityRequestFailed;

import java.util.Map;

@RestControllerAdvice
@Slf4j
public class GlobalExceptionHandler {

    @ExceptionHandler(IllegalArgumentException.class)
    public ResponseEntity<Map<String, String>> handleIllegalArgument(IllegalArgumentException ex) {
        return errorResponse(HttpStatus.BAD_REQUEST, ex.getMessage());
    }

    @ExceptionHandler(AccessControlError.class)
    public ResponseEntity<Map<String, String>> handleAccessControl(AccessControlError ex) {
        log.warn("Access denied: {}", ex.getMessage());
        return errorResponse(HttpStatus.FORBIDDEN, ex.getMessage());
    }

    @ExceptionHandler(SecurityRequestFailed.class)
    public ResponseEntity<Map<String, String>> handleSecurityRequest(SecurityRequestFailed ex) {
        log.warn("Security request failed: module={}, function={}, errno={}", ex.getModule(), ex.getFunction(), ex.getErrno());
        return errorResponse(HttpStatus.FORBIDDEN, ex.getMessage());
    }

    @ExceptionHandler(SafPlatformError.class)
    public ResponseEntity<Map<String, String>> handleSafPlatform(SafPlatformError ex) {
        log.error("SAF platform error", ex);
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, ex.getMessage());
    }

    @ExceptionHandler(RuntimeException.class)
    public ResponseEntity<Map<String, String>> handleRuntime(RuntimeException ex) {
        Throwable cause = unwrap(ex);
        if (cause instanceof AccessControlError ace) return handleAccessControl(ace);
        if (cause instanceof SecurityRequestFailed srf) return handleSecurityRequest(srf);
        if (cause instanceof SafPlatformError spe) return handleSafPlatform(spe);
        if (cause instanceof IllegalArgumentException iae) return handleIllegalArgument(iae);
        log.error("Unhandled runtime exception");
        String message = cause.getMessage() != null ? cause.getMessage() : "An unexpected error occurred";
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, message);
    }

    @ExceptionHandler(Exception.class)
    public ResponseEntity<Map<String, String>> handleChecked(Exception ex) {
        log.error("Unhandled checked exception", ex);
        String message = ex.getMessage() != null ? ex.getMessage() : "An unexpected error occurred";
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, message);
    }

    private Throwable unwrap(Throwable ex) {
        Throwable cause = ex.getCause();
        return (cause != null && cause != ex) ? cause : ex;
    }

    private ResponseEntity<Map<String, String>> errorResponse(HttpStatus status, String message) {
        return ResponseEntity.status(status).body(Map.of("message", message != null ? message : ""));
    }
}
