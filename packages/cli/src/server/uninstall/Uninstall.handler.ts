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

import { ConfigUtils, type ICommandHandler, type IHandlerParameters, ImperativeConfig } from "@zowe/imperative";
import { ZSshClient, ZSshUtils } from "@zowe/zowex-for-zowe-sdk";

export default class ServerUninstallHandler implements ICommandHandler {
    public async process(params: IHandlerParameters): Promise<void> {
        const session = ZSshUtils.buildSession(params.arguments);
        const serverPath = params.arguments.serverPath ?? ZSshClient.DEFAULT_SERVER_PATH;
        await ZSshUtils.uninstallServer(session, serverPath, {
            skipCache: ImperativeConfig.instance.daemonContext?.stream == null,
        });
        params.response.console.log(
            `Uninstalled Zowe Remote SSH server from ${ConfigUtils.getActiveProfileName("ssh", params.arguments)}`,
        );
    }
}
