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
import { JobDefinition } from "./job/Job.definition";
import { ItemDefinition as UssDefinition } from "./uss/Item.definition";
import { VsamDefinition } from "./vsam/Vsam.definition";

const DeleteDefinition: ICommandDefinition = {
    name: "delete",
    summary: "Delete data sets and/or members",
    description: "Delete data sets and/or data set members",
    type: "group",
    aliases: ["del"],
    children: [DataSetDefinition, VsamDefinition, UssDefinition, JobDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = DeleteDefinition;
