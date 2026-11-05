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

import {
    ConfigUtils,
    type ICommandHandler,
    type IHandlerParameters,
    type ITaskWithStatus,
    TaskStage,
} from "@zowe/imperative";
import { ZSshClient, ZSshUtils } from "@zowe/zowex-for-zowe-sdk";
import { translateCliError } from "../../CliErrorUtils";
import { SshBaseHandler } from "../../SshBaseHandler";
import { SshCacheScope } from "../../SshCacheScope";

export default class ServerInstallHandler implements ICommandHandler {
    public async process(params: IHandlerParameters): Promise<void> {
        using _cacheScope = new SshCacheScope();
        const session = ZSshUtils.buildSession(params.arguments);
        const serverPath = params.arguments.serverPath ?? ZSshClient.DEFAULT_SERVER_PATH;

        const task: ITaskWithStatus = {
            percentComplete: 0,
            statusMessage: "Deploying Zowe Remote SSH server...",
            stageName: TaskStage.IN_PROGRESS,
        };

        params.response.progress.startBar({ task });
        let caughtError: Error;
        try {
            const installStatus = await ZSshUtils.installServer(session, serverPath, {
                onProgress: (progressIncrement) => {
                    task.percentComplete += progressIncrement;
                },
                onError: async (error: Error, context: string) => {
                    // Log the error for CLI users
                    caughtError = error;
                    const translatedError = translateCliError(error);
                    if ("summary" in translatedError) {
                        SshBaseHandler.logTranslatedError(params, translatedError);
                        return false;
                    }
                    params.response.console.error(`Error during ${context}: ${translatedError}`);

                    // For CLI, we don't retry - just log and continue with the original error
                    return false;
                },
            });
            if (!installStatus && caughtError) {
                throw caughtError;
            }
        } catch (error) {
            throw translateCliError(error as Error);
        } finally {
            task.stageName = TaskStage.COMPLETE;
            params.response.progress.endBar();
        }
        params.response.console.log(
            `Installed Zowe Remote SSH server on ${ConfigUtils.getActiveProfileName("ssh", params.arguments)}`,
        );
    }
}
