package org.zowe.zo.config;

import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.TypeMismatchException;
import org.springframework.http.HttpStatus;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.ErrorResponse;
import org.springframework.web.ErrorResponseException;
import org.springframework.web.HttpMediaTypeNotAcceptableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.HttpRequestMethodNotSupportedException;
import org.springframework.web.bind.MissingRequestHeaderException;
import org.springframework.web.bind.MissingServletRequestParameterException;
import org.springframework.web.method.annotation.MethodArgumentTypeMismatchException;
import org.springframework.web.servlet.NoHandlerFoundException;
import org.springframework.web.servlet.resource.NoResourceFoundException;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.zos.security.platform.SafPlatformError;
import org.zowe.zo.zos.security.service.AccessControlError;
import org.zowe.zo.zos.security.service.SecurityRequestFailed;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

@RestControllerAdvice
@Slf4j
public class GlobalExceptionHandler {

    private static final String UNEXPECTED = "An unexpected error occurred";

    @ExceptionHandler(IllegalArgumentException.class)
    public ResponseEntity<Map<String, Object>> handleIllegalArgument(IllegalArgumentException ex) {
        return errorResponse(HttpStatus.BAD_REQUEST, ex.getMessage());
    }

    @ExceptionHandler(AccessControlError.class)
    public ResponseEntity<Map<String, Object>> handleAccessControl(AccessControlError ex) {
        log.warn("Access denied: {}", ex.getMessage());
        return errorResponse(HttpStatus.FORBIDDEN, ex.getMessage());
    }

    @ExceptionHandler(SecurityRequestFailed.class)
    public ResponseEntity<Map<String, Object>> handleSecurityRequest(SecurityRequestFailed ex) {
        log.warn("Security request failed: module={}, function={}, errno={}", ex.getModule(), ex.getFunction(), ex.getErrno());
        return errorResponse(HttpStatus.FORBIDDEN, ex.getMessage());
    }

    @ExceptionHandler(SafPlatformError.class)
    public ResponseEntity<Map<String, Object>> handleSafPlatform(SafPlatformError ex) {
        log.error("SAF platform error", ex);
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, ex.getMessage());
    }

    @ExceptionHandler(ZoRpcException.class)
    public ResponseEntity<Map<String, Object>> handleZoRpc(ZoRpcException ex) {
        HttpStatus status = switch (ex.getCode()) {
            case ZoRpcException.PARSE_ERROR, ZoRpcException.INVALID_REQUEST, ZoRpcException.INVALID_PARAMS -> HttpStatus.BAD_REQUEST;
            case ZoRpcException.METHOD_NOT_FOUND -> HttpStatus.NOT_IMPLEMENTED;
            case ZoRpcException.ETAG_MISMATCH -> HttpStatus.PRECONDITION_FAILED;
            case ZoRpcException.NOT_FOUND -> HttpStatus.NOT_FOUND;
            case ZoRpcException.CONFLICT -> HttpStatus.CONFLICT;
            default -> HttpStatus.INTERNAL_SERVER_ERROR;
        };
        log.warn("zo RPC {} failed: code={}, message={}", ex.getMethod(), ex.getCode(), ex.getMessage());
        String details = ex.getDetails();
        return body(status, ex.getCode(), ex.getMessage(), details != null ? List.of(details) : List.of());
    }

    /** Body is never echoed: Jackson messages can quote request content such as passwords. */
    @ExceptionHandler(HttpMessageNotReadableException.class)
    public ResponseEntity<Map<String, Object>> handleUnreadable(HttpMessageNotReadableException ex) {
        return errorResponse(HttpStatus.BAD_REQUEST, "Request body is missing or malformed");
    }

    @ExceptionHandler(TypeMismatchException.class)
    public ResponseEntity<Map<String, Object>> handleTypeMismatch(TypeMismatchException ex) {
        String name = ex instanceof MethodArgumentTypeMismatchException m ? m.getName() : ex.getPropertyName();
        return errorResponse(HttpStatus.BAD_REQUEST, "Invalid value for parameter '" + name + "'");
    }

    /** Spring MVC framework errors (missing params, 404 routes, 405, 415, ...) keep their own status. */
    @ExceptionHandler(ErrorResponseException.class)
    public ResponseEntity<Map<String, Object>> handleFramework(ErrorResponseException ex) {
        return frameworkResponse(ex);
    }

    @ExceptionHandler({MissingServletRequestParameterException.class, MissingRequestHeaderException.class,
            HttpMediaTypeNotSupportedException.class, HttpMediaTypeNotAcceptableException.class,
            HttpRequestMethodNotSupportedException.class, NoResourceFoundException.class,
            NoHandlerFoundException.class})
    public ResponseEntity<Map<String, Object>> handleFrameworkErrors(Exception ex) {
        return frameworkResponse((ErrorResponse) ex);
    }

    @ExceptionHandler(RuntimeException.class)
    public ResponseEntity<Map<String, Object>> handleRuntime(RuntimeException ex) {
        Throwable cause = rootDomainCause(ex);
        if (cause instanceof AccessControlError ace) return handleAccessControl(ace);
        if (cause instanceof SecurityRequestFailed srf) return handleSecurityRequest(srf);
        if (cause instanceof SafPlatformError spe) return handleSafPlatform(spe);
        if (cause instanceof ZoRpcException zre) return handleZoRpc(zre);
        if (cause instanceof IllegalArgumentException iae) return handleIllegalArgument(iae);
        log.error("Unhandled runtime exception", ex);
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, UNEXPECTED);
    }

    @ExceptionHandler(Exception.class)
    public ResponseEntity<Map<String, Object>> handleChecked(Exception ex) {
        log.error("Unhandled checked exception", ex);
        return errorResponse(HttpStatus.INTERNAL_SERVER_ERROR, UNEXPECTED);
    }

    /** First domain exception in the cause chain (the security aspect wraps native calls). */
    private Throwable rootDomainCause(Throwable ex) {
        Throwable current = ex;
        for (int depth = 0; current != null && depth < 10; depth++) {
            if (current instanceof ZoRpcException || current instanceof AccessControlError
                    || current instanceof SecurityRequestFailed || current instanceof SafPlatformError
                    || current instanceof IllegalArgumentException) {
                return current;
            }
            if (current.getCause() == current) break;
            current = current.getCause();
        }
        return ex;
    }

    private ResponseEntity<Map<String, Object>> frameworkResponse(ErrorResponse ex) {
        HttpStatus status = HttpStatus.resolve(ex.getStatusCode().value());
        String detail = ex.getBody().getDetail();
        ResponseEntity<Map<String, Object>> response = body(status != null ? status : HttpStatus.BAD_REQUEST, -1,
                detail != null ? detail : "Request could not be processed", List.of());
        return ex.getHeaders().isEmpty() ? response
                : ResponseEntity.status(response.getStatusCode()).headers(ex.getHeaders()).body(response.getBody());
    }

    private ResponseEntity<Map<String, Object>> errorResponse(HttpStatus status, String message) {
        return body(status, -1, message, List.of());
    }

    private ResponseEntity<Map<String, Object>> body(HttpStatus status, int rc, String message, List<String> details) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("rc", rc);
        body.put("message", message != null ? message : "");
        body.put("details", details);
        return ResponseEntity.status(status).body(body);
    }
}
