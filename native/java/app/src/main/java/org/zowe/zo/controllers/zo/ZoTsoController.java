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
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoRpcException;
import org.zowe.zo.service.TsoService;
import org.zowe.zo.service.TsoService.CommandResult;
import org.zowe.zo.service.TsoService.SessionInfo;

import java.util.List;
import java.util.Map;

/**
 * TSO under /zo/api/v1/tso. Every command is a one-shot tsocmd under the caller's identity;
 * "sessions" only buffer output and do not keep TSO state. Not z/OSMF tsoApp compatible.
 */
@RestController
@RequestMapping("/zo/api/v1/tso")
@Tag(name = "TSO", description = "One-shot TSO commands (tsocmd) and emulated sessions; see TSO-LIMITATIONS.md")
@ConditionalOnProperty(name = "zo.tso.enabled", havingValue = "true", matchIfMissing = true)
public class ZoTsoController {

    private final TsoService tso;

    public ZoTsoController(TsoService tso) {
        this.tso = tso;
    }

    @PostMapping("/command")
    @Operation(summary = "Run one TSO command",
            description = "Runs in a fresh TSO environment; a non-zero TSO return code is reported in 'rc'.")
    public CommandResult command(@RequestBody Map<String, String> body) throws ZoRpcException {
        return tso.issue(body == null ? null : body.get("command"));
    }

    @PostMapping("/sessions")
    @Operation(summary = "Start an emulated TSO session",
            description = "Returns a session ID owned by the caller. No TSO address space is kept between commands.")
    public ResponseEntity<SessionInfo> start() throws ZoRpcException {
        return ResponseEntity.status(HttpStatus.CREATED).body(tso.start());
    }

    @GetMapping("/sessions/{id}/info")
    @Operation(summary = "Describe a session")
    public SessionInfo describe(@PathVariable String id) throws ZoRpcException {
        return tso.describe(id);
    }

    @PutMapping("/sessions/{id}")
    @Operation(summary = "Send a command in a session",
            description = "Runs the command, buffers its output for the next receive, and also returns it.")
    public CommandResult send(@PathVariable String id, @RequestBody Map<String, String> body) throws ZoRpcException {
        return tso.send(id, body == null ? null : body.get("command"));
    }

    @GetMapping("/sessions/{id}")
    @Operation(summary = "Receive buffered output", description = "Returns and clears output buffered since the last receive.")
    public Map<String, List<CommandResult>> receive(@PathVariable String id) throws ZoRpcException {
        return Map.of("results", tso.receive(id));
    }

    @DeleteMapping("/sessions/{id}")
    @Operation(summary = "End a session")
    public ResponseEntity<Void> end(@PathVariable String id) throws ZoRpcException {
        tso.end(id);
        return ResponseEntity.noContent().build();
    }
}
