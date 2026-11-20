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
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.util.LinkedHashMap;
import java.util.Map;

/** System information operations backed by the zo RPC dispatcher (system.ts). */
@Service
@RequiredArgsConstructor
public class SystemService {

    private final ZoRpcClient rpc;

    public JsonNode getInfo() throws ZoRpcException {
        return rpc.call("getInfo", Map.of());
    }

    public JsonNode viewSyslog(String date, String time, Integer secondsAgo, Integer maxLines) throws ZoRpcException {
        ZoInputValidator.syslogDate(date);
        ZoInputValidator.syslogTime(time);
        ZoInputValidator.bounded(secondsAgo, "secondsAgo", 1, ZoInputValidator.MAX_SECONDS_AGO);
        ZoInputValidator.bounded(maxLines, "maxLines", 1, ZoInputValidator.MAX_SYSLOG_LINES);
        if (secondsAgo != null && (date != null || time != null)) {
            throw new IllegalArgumentException("secondsAgo is mutually exclusive with date and time");
        }
        Map<String, Object> params = new LinkedHashMap<>();
        putIfPresent(params, "date", date);
        putIfPresent(params, "time", time);
        putIfPresent(params, "secondsAgo", secondsAgo);
        putIfPresent(params, "maxLines", maxLines);
        return rpc.call("viewSyslog", params);
    }

    public JsonNode listParmlib() throws ZoRpcException {
        return rpc.call("listParmlib", Map.of());
    }

    public JsonNode listProclib() throws ZoRpcException {
        return rpc.call("listProclib", Map.of());
    }

    public JsonNode listApf() throws ZoRpcException {
        return rpc.call("listApf", Map.of());
    }

    public JsonNode listLinklist() throws ZoRpcException {
        return rpc.call("listLinklist", Map.of());
    }

    private static void putIfPresent(Map<String, Object> params, String key, Object value) {
        if (value != null) params.put(key, value);
    }
}
