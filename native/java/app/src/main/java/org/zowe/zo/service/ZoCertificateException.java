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

package org.zowe.zo.service;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.Getter;
import org.springframework.http.HttpStatus;

/** A certificate operation failure that maps to a specific HTTP status (for example 404). */
@Getter
public class ZoCertificateException extends Exception {

    private final HttpStatus status;
    private final int rc;
    private final transient JsonNode safReturns;

    public ZoCertificateException(HttpStatus status, int rc, String message, JsonNode safReturns) {
        super(message);
        this.status = status;
        this.rc = rc;
        this.safReturns = safReturns;
    }
}
