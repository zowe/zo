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

import type { IImperativeConfig } from "@zowe/imperative";

const config: IImperativeConfig = {
    commandModuleGlobs: ["*/*.definition!(.d).*s"],
    rootCommandDescription: "Zowe Remote SSH Plug-in for Zowe CLI",
    productDisplayName: "z/OS SSH Plug-in",
    name: "zo",
    pluginAliases: ["zssh", "zowex"],
    pluginSummary: "z/OS Files and jobs via SSH",
    pluginFirstSteps: `
If you do not have a Zowe team configuration, run the \`zowe config init\` command to create one.
Edit the team configuration file to add a SSH profile, including the hostname, port, and credentials. We recommend using a private key for authentication.
Run the \`zowe zssh server install\` command to install the Zowe Remote SSH server.
    `,
};

export = config;
