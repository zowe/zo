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
import { DataSetDefinition } from "./data-set/DataSet.definition";
import { DataSetMemberDefinition } from "./data-set-member/DataSetMember.definition";
import { FileDefinition } from "./uss/File.definition";

const CreateDefinition: ICommandDefinition = {
    name: "create",
    summary: "Create/initialize various resources",
    description: "Create files and directories, data sets, and members",
    type: "group",
    aliases: ["cre"],
    children: [DataSetDefinition, FileDefinition, DataSetMemberDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = CreateDefinition;
