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

export const ListJobsDefinition: ICommandDefinition = {
    handler: `${__dirname}/Jobs.handler`,
    type: "command",
    name: "jobs",
    aliases: ["j", "js"],
    summary: "List jobs by prefix",
    description:
        "List jobs on JES spool/queues. By default, the command lists jobs owned (owner) by the user specified in your SSH profile." +
        ' The default for prefix is "*".',
    examples: [
        {
            description: 'List all jobs with names beginning beginning with "ibmu"',
            options: '--prefix "ibmu*"',
        },
        {
            description: 'List Alice\'s jobs with names beginning beginning with "ibmu"',
            options: '--prefix "ibmu*" --owner "alice"',
        },
        {
            description: "List Alice's jobs and include execution data (system, member, submitted/started/ended times)",
            options: '--owner "alice" --exec-data',
        },
    ],
    options: [
        {
            name: "prefix",
            aliases: ["p"],
            description:
                "Specify the job name prefix of the jobs you own and want to list. " +
                'You can specify a wildcard, which is usually in the form "JOB*". \n ' +
                "If you don't specify this option all jobs under this owner will be listed by default.",
            type: "string",
            required: false,
        },
        {
            name: "owner",
            aliases: ["o"],
            description:
                "Specify the owner user ID of the jobs you want to list. The owner " +
                "is the individual/user who submitted the job OR the user ID assigned to the job. \n ",
            type: "string",
            required: false,
        },
        {
            name: "status",
            aliases: ["s"],
            description:
                "Specify the status of the jobs you want to list. The status is " +
                "one of the following: INPUT, ACTIVE and OUTPUT. \n ",
            type: "string",
            required: false,
        },
        {
            name: "exec-data",
            aliases: ["ed"],
            description:
                "Include execution data for each job (execSystem, execMember, execSubmitted, execStarted, execEnded) " +
                "in addition to the default information.",
            type: "boolean",
            required: false,
        },
    ],
    profile: { optional: ["ssh"] },
    outputFormatOptions: true,
};
