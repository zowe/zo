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

export const VsamDefinition: ICommandDefinition = {
    handler: `${__dirname}/Vsam.handler`,
    description: "Delete a VSAM cluster using IDCAMS DELETE CLUSTER",
    type: "command",
    name: "vsam",
    summary: "Delete a VSAM cluster",
    examples: [
        {
            description: "Delete a VSAM cluster",
            options: '"ibmuser.test.vsam"',
        },
        {
            description: "Erase and delete a VSAM cluster even if its retention period has not expired",
            options: '"ibmuser.test.vsam" --erase --purge',
        },
    ],
    positionals: [
        {
            name: "name",
            description: "The VSAM cluster to delete",
            type: "string",
            required: true,
        },
    ],
    options: [
        {
            name: "erase",
            description: "Overwrite the cluster's data component with binary zeros before deletion",
            type: "boolean",
            defaultValue: false,
        },
        {
            name: "purge",
            description: "Delete the cluster even if its retention period has not expired",
            type: "boolean",
            defaultValue: false,
        },
    ],
    profile: { optional: ["ssh"] },
};
