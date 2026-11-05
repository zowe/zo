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

import { type ICommandHandler, type IHandlerParameters, TextUtils } from "@zowe/imperative";
import { type ISshSession, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import {
    type ClientOptions,
    type CommandResponse,
    type ISshErrorDefinition,
    ZSshClient,
    ZSshUtils,
} from "@zowe/zowex-for-zowe-sdk";
import { translateCliError } from "./CliErrorUtils";
import { SshCacheScope } from "./SshCacheScope";

export abstract class SshBaseHandler implements ICommandHandler {
    public async process(commandParameters: IHandlerParameters) {
        using _cacheScope = new SshCacheScope();
        const session = ZSshUtils.buildSession(commandParameters.arguments);

        try {
            await this._processCommandWithClient(commandParameters, session);
        } catch (error) {
            const errorMessage = `${error}`;

            // Check if this is a private key authentication failure
            if (ZSshUtils.isPrivateKeyAuthFailure(errorMessage, !!session.ISshSession.privateKey)) {
                commandParameters.response.console.log(
                    TextUtils.chalk.yellow(
                        "Private key authentication failed. Falling back to password authentication...",
                    ),
                );

                // Prompt for password
                const password = await this.promptForPassword(commandParameters, session);
                if (password) {
                    // Create a new session with password authentication
                    const passwordSession = this.createPasswordSession(session, password);

                    // Retry the connection with password
                    await this._processCommandWithClient(commandParameters, passwordSession);
                } else {
                    throw error; // Re-throw if user cancelled password prompt
                }
            } else {
                throw error; // Re-throw for other types of errors
            }
        }
    }

    /**
     * Prompts the user for a password with retry logic
     */
    private async promptForPassword(
        commandParameters: IHandlerParameters,
        session: SshSession,
    ): Promise<string | undefined> {
        for (let attempts = 0; attempts < 3; attempts++) {
            const password = await commandParameters.response.console.prompt(
                `${session.ISshSession.user}@${session.ISshSession.hostname}'s password: `,
                { hideText: true },
            );

            if (!password) {
                return undefined; // User cancelled
            }

            // Test the password by attempting a connection
            try {
                const testSession = this.createPasswordSession(session, password);
                using _testClient = await ZSshClient.create(testSession, this.buildSshClientOptions(commandParameters));
                // If we get here, the password is valid
                return password;
            } catch (error) {
                const errorMessage = `${error}`;
                if (errorMessage.includes("FOTS1668")) {
                    commandParameters.response.console.error("Password expired on target system");
                    return undefined;
                }
                commandParameters.response.console.error(`Password authentication failed (${attempts + 1}/3)`);
                if (attempts === 2) {
                    return undefined; // Max attempts reached
                }
            }
        }
        return undefined;
    }

    /**
     * Creates a new session with password authentication, disabling private key and agent
     */
    private createPasswordSession(originalSession: SshSession, password: string): SshSession {
        const newSessionConfig: ISshSession = {
            ...originalSession.ISshSession,
            password,
            privateKey: undefined as string | undefined,
            keyPassphrase: undefined as string | undefined,
            identityAgent: undefined as string | undefined,
        };
        return new SshSession(newSessionConfig);
    }

    public abstract processWithClient(
        commandParameters: IHandlerParameters,
        client: ZSshClient,
    ): Promise<CommandResponse>;

    private async _processCommandWithClient(commandParameters: IHandlerParameters, session: SshSession): Promise<void> {
        try {
            using client = await ZSshClient.create(session, this.buildSshClientOptions(commandParameters));

            const response = await this.processWithClient(commandParameters, client);

            commandParameters.response.progress.endBar(); // end any progress bars

            // Return as an object when using --response-format-json
            commandParameters.response.data.setObj(response);
        } catch (error) {
            const translatedError = translateCliError(error as Error);

            if ("summary" in translatedError) {
                // This is an ISshErrorDefinition
                SshBaseHandler.logTranslatedError(commandParameters, translatedError);
                return;
            }

            const errorMessage = `${translatedError}`;

            // Check if this is a private key authentication failure
            if (ZSshUtils.isPrivateKeyAuthFailure(errorMessage, !!session.ISshSession.privateKey)) {
                commandParameters.response.console.log(
                    TextUtils.chalk.yellow(
                        "Private key authentication failed. Falling back to password authentication...",
                    ),
                );

                // Prompt for password
                const password = await this.promptForPassword(commandParameters, session);
                if (password) {
                    // Create a new session with password authentication
                    const passwordSession = this.createPasswordSession(session, password);

                    // Retry the connection with password
                    using client = await ZSshClient.create(
                        passwordSession,
                        this.buildSshClientOptions(commandParameters),
                    );

                    const response = await this.processWithClient(commandParameters, client);

                    commandParameters.response.progress.endBar(); // end any progress bars

                    // Return as an object when using --response-format-json
                    commandParameters.response.data.setObj(response);
                } else {
                    throw error; // Re-throw if user cancelled password prompt
                }
            } else {
                throw error; // Re-throw for other types of errors
            }
        }
    }

    private buildSshClientOptions(commandParameters: IHandlerParameters): ClientOptions {
        return {
            numWorkers: 1,
            requestTimeout: commandParameters.arguments.responseTimeout,
            responseTimeout: commandParameters.arguments.responseTimeout,
            serverPath: commandParameters.arguments.serverPath,
        };
    }

    /**
     * Logs a translated error with tips and resources to the console.
     * @param commandParameters The handler parameters
     * @param errorDef The translated error definition
     */
    public static logTranslatedError(commandParameters: IHandlerParameters, errorDef: ISshErrorDefinition): void {
        commandParameters.response.console.log(errorDef.summary);

        if (errorDef.tips) {
            commandParameters.response.console.log("\nTips:");
            errorDef.tips.forEach((tip) => {
                commandParameters.response.console.log(`- ${tip}`);
            });
        }

        if (errorDef.resources) {
            commandParameters.response.console.log("\nResources:");
            errorDef.resources.forEach((resource) => {
                commandParameters.response.console.log(`- ${resource.title}: ${resource.href}`);
            });
        }
    }
}
