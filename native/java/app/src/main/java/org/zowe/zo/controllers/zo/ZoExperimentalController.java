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
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.zowe.zo.ffm.ZoRpcClient;
import org.zowe.zo.ffm.ZoRpcException;

import java.util.Map;

/**
 * Evaluation-only endpoints for zo commands that spawn child processes. Disabled unless
 * zo.experimental.spawning-commands=true (and the native allowlist is extended to match).
 */
@RestController
@RequestMapping("/zo/api/v1/experimental")
@ConditionalOnProperty(name = "zo.experimental.spawning-commands", havingValue = "true")
public class ZoExperimentalController {

    private static final int MAX_COMMAND_LENGTH = 1024;

    private final ZoRpcClient rpc;

    public ZoExperimentalController(ZoRpcClient rpc) {
        this.rpc = rpc;
    }

    @PostMapping(value = "/unix-command", produces = MediaType.TEXT_PLAIN_VALUE)
    public String unixCommand(@RequestBody Map<String, String> body) throws ZoRpcException {
        return text(rpc.call("unixCommand", Map.of("commandText", command(body))));
    }

    @PostMapping(value = "/tso-command", produces = MediaType.TEXT_PLAIN_VALUE)
    public String tsoCommand(@RequestBody Map<String, String> body) throws ZoRpcException {
        return text(rpc.call("tsoCommand", Map.of("commandText", command(body))));
    }

    private static String command(Map<String, String> body) {
        String command = body == null ? null : body.get("command");
        if (command == null || command.isBlank() || command.length() > MAX_COMMAND_LENGTH
                || command.chars().anyMatch(c -> c < 0x20)) {
            throw new IllegalArgumentException("'command' is required (printable, at most " + MAX_COMMAND_LENGTH + " characters)");
        }
        return command;
    }

    private static String text(JsonNode result) {
        JsonNode data = result.path("data");
        return data.isMissingNode() ? result.toString() : data.asText();
    }
}
