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
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.CertificateRequests.CertificateQuery;
import org.zowe.zo.service.CertificateRequests.ExportRequest;
import org.zowe.zo.service.CertificateRequests.ImportRequest;
import org.zowe.zo.service.CertificateRequests.OwnerCertificateAction;
import org.zowe.zo.service.CertificateRequests.RingCertificateAction;

import java.util.LinkedHashMap;
import java.util.Map;

import static org.zowe.zo.service.ZoInputValidator.*;

/**
 * Certificate and RACF key ring operations backed by the zo RPC dispatcher (certificates.ts).
 * Native code drives R_datalib with binary parameter lists, so values are never parsed as
 * command text; validation here exists to stop the native layer silently truncating over-long
 * names (which would address a different ring or certificate) and to return 400 instead of 500.
 */
@Service
@RequiredArgsConstructor
public class CertificateService {

    private static final String NOT_FOUND_MARKER = "not found in";

    private final ZoRpcClient rpc;

    public JsonNode listRings(String owner, String keyring) throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner, null);
        if (keyring != null) params.put("keyring", ring(keyring, true));
        return call("listRings", params);
    }

    public JsonNode countRing(String owner, String keyring) throws ZoRpcException, ZoCertificateException {
        return call("countRing", base(owner, ring(keyring, true)));
    }

    public JsonNode listCertificates(String owner, String keyring, CertificateQuery query)
            throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner, ring(keyring, true));
        put(params, "label", optionalLabel(query.label()));
        put(params, "usage", query.usage() == null ? null : usage(query.usage(), true));
        put(params, "labelOnly", query.labelOnly());
        put(params, "ownerOnly", query.ownerOnly());
        put(params, "maxEntries", bounded(query.maxEntries(), "maxEntries", 0, MAX_CERT_ENTRIES));
        return call("listCertificates", params);
    }

    public JsonNode showCertificate(String owner, String keyring, String label)
            throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner, ring(keyring, true));
        params.put("label", label(label, "label"));
        return call("showCertificate", params);
    }

    public JsonNode exportCertificate(String owner, String keyring, String label, ExportRequest request)
            throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner, ring(keyring, true));
        params.put("label", label(label, "label"));
        String format = exportFormat(request.format());
        params.put("format", format);
        String file = request.file() == null ? null : path(request.file());
        String dsn = request.dsn() == null ? null : dataSet(request.dsn());
        fileOrDsn(file, dsn, "p12".equals(format));
        if ("p12".equals(format)) {
            params.put("password", exportPassword(request.password()));
        } else if (request.password() != null) {
            throw new IllegalArgumentException("password is only valid for p12 export");
        }
        put(params, "file", file);
        put(params, "dsn", dsn);
        return call("exportCertificate", params);
    }

    public JsonNode importCertificate(ImportRequest request) throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner(request.owner()), ring(request.keyring(), false));
        params.put("label", label(request.label(), "label"));
        params.put("usage", usage(request.usage(), false));
        String file = request.file() == null ? null : path(request.file());
        String dsn = request.dsn() == null ? null : dataSet(request.dsn());
        fileOrDsn(file, dsn, true);
        put(params, "file", file);
        put(params, "dsn", dsn);
        params.put("password", importPassword(request.password()));
        put(params, "skipRefresh", request.skipRefresh());
        return call("importCertificate", params);
    }

    public JsonNode createKeyring(String owner, String keyring) throws ZoRpcException, ZoCertificateException {
        return call("createKeyring", base(owner, ring(keyring, false)));
    }

    public JsonNode deleteKeyring(String owner, String keyring) throws ZoRpcException, ZoCertificateException {
        return call("deleteKeyring", base(owner, ring(keyring, false)));
    }

    public JsonNode refreshDigtcert() throws ZoRpcException, ZoCertificateException {
        return call("refreshDigtcert", Map.of());
    }

    /** Disconnects from the ring when keyring is given, otherwise deletes from the ESM database. */
    public JsonNode deleteCertificate(String owner, String keyring, String label, Boolean skipRefresh)
            throws ZoRpcException, ZoCertificateException {
        Map<String, Object> params = base(owner, null);
        if (keyring != null) {
            params.put("keyring", ring(keyring, false));
        } else {
            params.put("database", true);
        }
        params.put("label", label(label, "label"));
        put(params, "skipRefresh", skipRefresh);
        return call("deleteCertificate", params);
    }

    public JsonNode ringAction(String owner, String keyring, String label, RingCertificateAction action)
            throws ZoRpcException, ZoCertificateException {
        String request = action.request() == null ? "" : action.request();
        Map<String, Object> params = base(owner, ring(keyring, false));
        params.put("label", label(label, "label"));
        switch (request) {
            case "connect" -> {
                boolean fromDatabase = Boolean.TRUE.equals(action.fromDatabase());
                if (fromDatabase == (action.fromRing() != null)) {
                    throw new IllegalArgumentException("specify exactly one of fromRing or fromDatabase");
                }
                if (action.fromRing() != null) params.put("fromRing", ring(action.fromRing(), false));
                if (fromDatabase) params.put("fromDatabase", true);
                put(params, "usage", action.usage() == null ? null : usage(action.usage(), false));
                put(params, "default", action.makeDefault());
                return call("connectCertificate", params);
            }
            case "set-default" -> {
                return call("setDefaultCertificate", params);
            }
            default -> throw new IllegalArgumentException("request must be connect or set-default");
        }
    }

    public JsonNode ownerAction(String owner, String label, OwnerCertificateAction action)
            throws ZoRpcException, ZoCertificateException {
        String request = action.request() == null ? "" : action.request();
        Map<String, Object> params = base(owner, null);
        params.put("label", label(label, "label"));
        switch (request) {
            case "rename" -> {
                params.put("newLabel", label(action.newLabel(), "newLabel"));
                return call("renameCertificate", params);
            }
            case "trust" -> {
                params.put("status", trustStatus(action.status()));
                return call("trustCertificate", params);
            }
            default -> throw new IllegalArgumentException("request must be rename or trust");
        }
    }

    private static Map<String, Object> base(String owner, String keyring) {
        Map<String, Object> params = new LinkedHashMap<>();
        params.put("owner", owner(owner));
        if (keyring != null) params.put("keyring", keyring);
        return params;
    }

    private static void put(Map<String, Object> params, String key, Object value) {
        if (value != null) params.put(key, value);
    }

    private JsonNode call(String method, Object params) throws ZoRpcException, ZoCertificateException {
        try {
            return rpc.call(method, params);
        } catch (ZoRpcException e) {
            if (isNotFound(e)) {
                JsonNode data = e.getData();
                throw new ZoCertificateException(HttpStatus.NOT_FOUND, e.getCode(), e.getMessage(),
                        data == null ? null : data.get("safReturns"));
            }
            throw e;
        }
    }

    private static boolean isNotFound(ZoRpcException e) {
        if (e.getMessage() != null && e.getMessage().contains(NOT_FOUND_MARKER)) return true;
        String details = e.getDetails();
        return details != null && details.contains(NOT_FOUND_MARKER);
    }
}
