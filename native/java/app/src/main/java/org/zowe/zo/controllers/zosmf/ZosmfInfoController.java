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

package org.zowe.zo.controllers.zosmf;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * z/OSMF-compatible GET /zosmf/info. Unauthenticated like z/OSMF; Zowe clients (CheckStatus,
 * profile validation) read zos_version, zosmf_version, zosmf_hostname, zosmf_port and plugins.
 * The z/OS level comes from uname (version.release, e.g. "05.29.00"), exported by start.sh.
 */
@RestController
public class ZosmfInfoController {

    private final Map<String, Object> ordered;

    public ZosmfInfoController(@Value("${zo.info.zos-version:}") String zosVersion,
                               @Value("${zo.info.hostname:${apiml.service.hostname:localhost}}") String hostname,
                               @Value("${zo.info.port:${server.port:0}}") String port,
                               @Value("${zo.info.version:0.5.0}") String zoVersion) {
        String release = zosVersion.contains(".") ? zosVersion.substring(zosVersion.indexOf('.') + 1) : "";
        String major = release.contains(".") ? release.substring(0, release.indexOf('.')) : release;
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("zos_version", zosVersion);
        body.put("zosmf_port", port);
        body.put("zosmf_version", major);
        body.put("zosmf_hostname", hostname.toUpperCase(Locale.ROOT));
        body.put("zosmf_saf_realm", "SAFRealm");
        body.put("zosmf_full_version", major.isEmpty() ? "" : major + ".0");
        body.put("api_version", "1");
        body.put("plugins", List.of(Map.of(
                "pluginVersion", "zo-rest;" + zoVersion,
                "pluginDefaultName", "zo REST API - this is zo, not z/OSMF",
                "pluginStatus", "ACTIVE")));
        this.ordered = Collections.unmodifiableMap(body);
    }

    @GetMapping("/zosmf/info")
    public Map<String, Object> info() {
        return ordered;
    }
}
