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
import { UploadFileToDataSetDefinition } from "./file-to-data-set/FileToDataSet.definition";
import { UploadFileToUssFileDefinition } from "./file-to-uss/FileToUss.definition";

const UploadDefinition: ICommandDefinition = {
    name: "upload",
    aliases: ["ul"],
    summary: "Upload data set and USS content",
    description: "Upload data set and USS content",
    type: "group",
    children: [UploadFileToDataSetDefinition, UploadFileToUssFileDefinition],
    passOn: [
        {
            property: "options",
            value: [...SshSession.SSH_CONNECTION_OPTIONS, ...Constants.ZSSH_EXTRA_OPTIONS],
            merge: true,
            ignoreNodes: [{ type: "group" }],
        },
    ],
};

export = UploadDefinition;
