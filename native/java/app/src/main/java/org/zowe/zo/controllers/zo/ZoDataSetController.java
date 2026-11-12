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

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.DataSetConflictException;
import org.zowe.zo.service.DataSetService;
import org.zowe.zo.service.DataSetService.DataSetInfo;
import org.zowe.zo.service.DataSetService.ListPage;
import org.zowe.zo.service.DataSetService.ListQuery;
import org.zowe.zo.service.DataSetService.MemberInfo;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Data set capabilities that z/OSMF does not offer. */
@RestController
@RequestMapping("/zo/api/v1/ds")
@RequiredArgsConstructor
@Tag(name = "zo data sets", description = "zo extensions to the z/OSMF data set interface")
public class ZoDataSetController {

    private static final String DEFAULT_MAX_ITEMS = "1000";

    private final DataSetService dataSetService;

    @ExceptionHandler(DataSetConflictException.class)
    public ResponseEntity<Map<String, Object>> handleConflict(DataSetConflictException ex) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("rc", HttpStatus.CONFLICT.value());
        body.put("message", ex.getMessage());
        body.put("details", List.of());
        return ResponseEntity.status(HttpStatus.CONFLICT).body(body);
    }

    private static void requireText(String name, String value) {
        if (value == null || value.isEmpty()) {
            throw new IllegalArgumentException(name + " parameter is required");
        }
    }

    private static boolean flag(String name, String value) {
        if ("true".equalsIgnoreCase(value) || "false".equalsIgnoreCase(value)) {
            return Boolean.parseBoolean(value);
        }
        throw new IllegalArgumentException(name + " must be true or false");
    }

    private static int count(String value) {
        try {
            int parsed = Integer.parseInt(value.trim());
            if (parsed >= 0) {
                return parsed;
            }
        } catch (NumberFormatException e) {
            // falls through to the validation error
        }
        throw new IllegalArgumentException("maxItems must be a non-negative whole number");
    }

    private static Map<String, Object> pageBody(ListPage<?> page) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("items", page.items());
        body.put("returnedRows", page.items().size());
        body.put("moreRows", page.moreRows());
        if (page.totalRows() != null) {
            body.put("totalRows", page.totalRows());
        }
        return body;
    }

    @Operation(summary = "List data sets with the full zo attribute set (maxItems 0 = unlimited)")
    @GetMapping
    public Map<String, Object> listDataSets(@RequestParam(value = "pattern", required = false) String pattern,
                                            @RequestParam(value = "volser", required = false) String volser,
                                            @RequestParam(value = "start", required = false) String start,
                                            @RequestParam(value = "maxItems", defaultValue = DEFAULT_MAX_ITEMS) String maxItems,
                                            @RequestParam(value = "attributes", defaultValue = "true") String attributes)
            throws ZoRpcException {
        requireText("pattern", pattern);
        ListPage<DataSetInfo> page = dataSetService.listDataSets(pattern, volser,
                new ListQuery(null, start, count(maxItems), flag("attributes", attributes), true));
        return pageBody(page);
    }

    @Operation(summary = "List members with the full zo attribute set")
    @GetMapping("/{dsn}/members")
    public Map<String, Object> listMembers(@PathVariable("dsn") String dsn,
                                           @RequestParam(value = "pattern", required = false) String pattern,
                                           @RequestParam(value = "start", required = false) String start,
                                           @RequestParam(value = "maxItems", defaultValue = DEFAULT_MAX_ITEMS) String maxItems,
                                           @RequestParam(value = "attributes", defaultValue = "true") String attributes)
            throws ZoRpcException {
        ListPage<MemberInfo> page = dataSetService.listMembers(dsn, pattern,
                new ListQuery(null, start, count(maxItems), flag("attributes", attributes), true));
        return pageBody(page);
    }

    @Operation(summary = "Create an empty member in an existing partitioned data set")
    @PostMapping("/{dsn}/member/{member}")
    public ResponseEntity<Void> createMember(@PathVariable("dsn") String dsn, @PathVariable("member") String member,
                                             @RequestParam(value = "overwrite", defaultValue = "false") String overwrite)
            throws ZoRpcException {
        dataSetService.createMember(dsn, member, flag("overwrite", overwrite));
        return ResponseEntity.status(HttpStatus.CREATED).build();
    }

    @Operation(summary = "Search data set content (ISRSUPC); the response data is the raw search output")
    @GetMapping("/{dsn}/search")
    public Map<String, String> search(@PathVariable("dsn") String dsn, @RequestParam(value = "string", required = false) String string,
                                      @RequestParam(value = "parms", required = false) String parms)
            throws ZoRpcException {
        requireText("string", string);
        return Map.of("data", dataSetService.search(dsn, string, parms));
    }

    @Operation(summary = "Resolve an alias to the data set it points to")
    @GetMapping("/{dsn}/alias")
    public Map<String, String> resolveAlias(@PathVariable("dsn") String dsn) throws ZoRpcException {
        return Map.of("targetDsn", dataSetService.resolveAlias(dsn));
    }

    @Operation(summary = "Delete a VSAM cluster (IDCAMS DELETE CLUSTER)")
    @DeleteMapping("/{dsn}/vsam")
    public ResponseEntity<Void> deleteVsam(@PathVariable("dsn") String dsn,
                                           @RequestParam(value = "erase", required = false) String erase,
                                           @RequestParam(value = "purge", required = false) String purge)
            throws ZoRpcException {
        dataSetService.deleteVsam(dsn, erase == null ? null : flag("erase", erase),
                purge == null ? null : flag("purge", purge));
        return ResponseEntity.noContent().build();
    }
}
