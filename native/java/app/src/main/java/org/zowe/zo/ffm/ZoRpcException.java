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

package org.zowe.zo.ffm;

import com.fasterxml.jackson.databind.JsonNode;
import lombok.Getter;

/**
 * A JSON-RPC error returned by zo. {@code code} is either a JSON-RPC standard code
 * (negative, see constants) or the non-zero return code of the zo command handler.
 * {@code data} carries handler detail (a string or a structured object such as SAF codes).
 */
@Getter
public class ZoRpcException extends Exception {

    public static final int PARSE_ERROR = -32700;
    public static final int INVALID_REQUEST = -32600;
    public static final int METHOD_NOT_FOUND = -32601;
    public static final int INVALID_PARAMS = -32602;
    public static final int INTERNAL_ERROR = -32603;
    /** Implementation-defined: write rejected because the supplied ETag no longer matches. */
    public static final int ETAG_MISMATCH = -32010;
    /** Implementation-defined: the target data set, member or file does not exist. */
    public static final int NOT_FOUND = -32011;

    private final String method;
    private final int code;
    private final transient JsonNode data;

    public ZoRpcException(String method, int code, String message, JsonNode data) {
        super(message);
        this.method = method;
        this.code = code;
        this.data = data;
    }

    /** Detail text from the handler (stderr remainder or structured "details"), if any. */
    public String getDetails() {
        if (data == null || data.isNull()) return null;
        if (data.isTextual()) return data.asText();
        JsonNode details = data.get("details");
        return details != null && details.isTextual() ? details.asText() : data.toString();
    }
}
