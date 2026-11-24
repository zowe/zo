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
import { ListAliasDefinition } from "./alias/Alias.definition";
import { ListDataSetDefinition } from "./data-set/DataSet.definition";
import { ListDataSetMembersDefinition } from "./data-set-members/DataSetMembers.definition";
import { ListJobsDefinition } from "./jobs/Jobs.definition";
import { ListSpoolFilesDefinition } from "./spool-files/SpoolFiles.definition";
import { ListUssFilesDefinition } from "./uss-files/UssFiles.definition";

const ListDefinition: ICommandDefinition = {
    name: "list",
    aliases: ["ls"],
    summary: "List data sets, data set members, uss files, jobs, spool files",
    description: "List data sets, data set members, uss files, jobs, spool files",
    type: "group",
    children: [
        ListAliasDefinition,
        ListDataSetDefinition,
        ListDataSetMembersDefinition,
        ListUssFilesDefinition,
        ListJobsDefinition,
        ListSpoolFilesDefinition,
    ],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = ListDefinition;
