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

import com.fasterxml.jackson.databind.JsonNode;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.SystemService;

@RestController
@RequestMapping("/zo/api/v1/system")
@RequiredArgsConstructor
@Tag(name = "zo System", description = "z/OS system information (zo extension, not available in z/OSMF)")
public class ZoSystemController {

    private final SystemService systemService;

    @GetMapping("/info")
    @Operation(summary = "Get zo version and build information")
    public JsonNode getInfo() throws ZoRpcException {
        return systemService.getInfo();
    }

    @GetMapping("/syslog")
    @Operation(summary = "View the system log",
            description = "Defaults to the last 30 seconds. secondsAgo is mutually exclusive with date/time.")
    public JsonNode viewSyslog(
            @Parameter(description = "Start date, yyyy-mm-dd") @RequestParam(required = false) String date,
            @Parameter(description = "Start time, hh:mm:ss") @RequestParam(required = false) String time,
            @Parameter(description = "Start this many seconds ago (1-86400)") @RequestParam(required = false) Integer secondsAgo,
            @Parameter(description = "Maximum lines to return (1-10000)") @RequestParam(required = false) Integer maxLines)
            throws ZoRpcException {
        return systemService.viewSyslog(date, time, secondsAgo, maxLines);
    }

    @GetMapping("/parmlib")
    @Operation(summary = "List the PARMLIB concatenation")
    public JsonNode listParmlib() throws ZoRpcException {
        return systemService.listParmlib();
    }

    @GetMapping("/proclib")
    @Operation(summary = "List the PROCLIB concatenation")
    public JsonNode listProclib() throws ZoRpcException {
        return systemService.listProclib();
    }

    @GetMapping("/apf")
    @Operation(summary = "List APF-authorized data sets")
    public JsonNode listApf() throws ZoRpcException {
        return systemService.listApf();
    }

    @GetMapping("/linklist")
    @Operation(summary = "List link list data sets")
    public JsonNode listLinklist() throws ZoRpcException {
        return systemService.listLinklist();
    }
}
