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
import { ViewDataSetDefinition } from "./data-set/DataSet.definition";
import { ViewJobFileDefinition } from "./job-file/JobFile.definition";
import { ViewJobJclDefinition } from "./job-jcl/JobJcl.definition";
import { ViewJobStatusDefinition } from "./job-status/JobStatus.definition";
import { ViewUssFileDefinition } from "./uss-file/UssFile.definition";

const ViewDefinition: ICommandDefinition = {
    name: "view",
    aliases: ["vw"],
    summary: "View data set, job output, and USS content",
    description: "View data sets, job output, and USS content",
    type: "group",
    children: [
        ViewDataSetDefinition,
        ViewJobJclDefinition,
        ViewJobFileDefinition,
        ViewJobStatusDefinition,
        ViewUssFileDefinition,
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

export = ViewDefinition;
