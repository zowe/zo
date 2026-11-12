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

import type { ICommandDefinition } from "@zowe/imperative";
import { SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import { Constants } from "../Constants";
import { ServerInstallDefinition } from "./install/Install.definition";
import { ServerUninstallDefinition } from "./uninstall/Uninstall.definition";

const ServerDefinition: ICommandDefinition = {
    name: "server",
    aliases: ["srv"],
    summary: "Manage the Zowe Remote SSH server",
    description: "Manage the Zowe Remote SSH server",
    type: "group",
    children: [ServerInstallDefinition, ServerUninstallDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = ServerDefinition;
