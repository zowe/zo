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
import { DownloadDataSetDefinition } from "./data-set/DataSet.definition";
import { DownloadJobFileDefinition } from "./job-file/JobFile.definition";
import { DownloadUssFileDefinition } from "./uss-file/UssFile.definition";

const DownloadDefinition: ICommandDefinition = {
    name: "download",
    aliases: ["dl"],
    summary: "Download data set, job output, and USS content",
    description: "Download data sets, job output, and USS content",
    type: "group",
    children: [DownloadDataSetDefinition, DownloadJobFileDefinition, DownloadUssFileDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = DownloadDefinition;
