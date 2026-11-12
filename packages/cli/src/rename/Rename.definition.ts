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
import { RenameDataSetDefinition } from "./data-set/DataSet.definition";
import { RenameMemberDefinition } from "./data-set-members/DataSetMembers.definition";

const RenameDefinition: ICommandDefinition = {
    name: "rename",
    summary: "Rename a data set or a data set member",
    description: "Rename a data set or a data set member",
    type: "group",
    children: [RenameDataSetDefinition, RenameMemberDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = RenameDefinition;
