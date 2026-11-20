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

package org.zowe.zo.controllers.zo;

import lombok.extern.slf4j.Slf4j;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.bind.MissingServletRequestParameterException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.springframework.web.method.annotation.MethodArgumentTypeMismatchException;
import org.zowe.zo.service.ZoCertificateException;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Error mapping for the zo extension controllers. Runs ahead of the global handler (whose
 * catch-alls would otherwise return 500) and never logs or echoes request-body content,
 * because parse errors can quote secrets such as passwords.
 */
@RestControllerAdvice(basePackageClasses = {ZoSystemController.class, ZoCertificateController.class})
@Order(Ordered.HIGHEST_PRECEDENCE)
@Slf4j
public class ZoExceptionAdvice {

    @ExceptionHandler(ZoCertificateException.class)
    public ResponseEntity<Map<String, Object>> handleCertificate(ZoCertificateException ex) {
        log.warn("Certificate operation failed: status={}, rc={}", ex.getStatus().value(), ex.getRc());
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("rc", ex.getRc());
        body.put("message", ex.getMessage() != null ? ex.getMessage() : "");
        body.put("details", List.of());
        if (ex.getSafReturns() != null) body.put("safReturns", ex.getSafReturns());
        return ResponseEntity.status(ex.getStatus()).body(body);
    }

    @ExceptionHandler(HttpMessageNotReadableException.class)
    public ResponseEntity<Map<String, String>> handleUnreadable(HttpMessageNotReadableException ex) {
        return message(HttpStatus.BAD_REQUEST, "Request body is missing or is not valid JSON");
    }

    @ExceptionHandler(MissingServletRequestParameterException.class)
    public ResponseEntity<Map<String, String>> handleMissingParameter(MissingServletRequestParameterException ex) {
        return message(HttpStatus.BAD_REQUEST, "Missing required parameter '" + ex.getParameterName() + "'");
    }

    @ExceptionHandler(MethodArgumentTypeMismatchException.class)
    public ResponseEntity<Map<String, String>> handleTypeMismatch(MethodArgumentTypeMismatchException ex) {
        return message(HttpStatus.BAD_REQUEST, "Invalid value for parameter '" + ex.getName() + "'");
    }

    @ExceptionHandler(HttpMediaTypeNotSupportedException.class)
    public ResponseEntity<Map<String, String>> handleMediaType(HttpMediaTypeNotSupportedException ex) {
        return message(HttpStatus.UNSUPPORTED_MEDIA_TYPE, "Content type must be application/json");
    }

    private static ResponseEntity<Map<String, String>> message(HttpStatus status, String text) {
        return ResponseEntity.status(status).body(Map.of("message", text));
    }
}
