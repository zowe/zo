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
    type ICommandHandler,
    type IConfigLayer,
    type IHandlerParameters,
    type IHandlerResponseApi,
    ImperativeConfig,
    ImperativeError,
    type IProfileTypeConfiguration,
    ProfileInfo,
    TextUtils,
} from "@zowe/imperative";
import type { ISshSession } from "@zowe/zos-uss-for-zowe-sdk";
import {
    AbstractConfigManager,
    type IDisposable,
    type inputBoxOpts,
    MESSAGE_TYPE,
    type PrivateKeyWarningOptions,
    type ProgressCallback,
    type qpItem,
    type qpOpts,
} from "@zowe/zowex-for-zowe-sdk";
import * as termkit from "terminal-kit";

export default class ServerInstallHandler implements ICommandHandler {
    public async process(params: IHandlerParameters): Promise<void> {
        const profInfo = new ProfileInfo("zowe");
        await profInfo.readProfilesFromDisk();
        const cliPromptApi = new CliPromptApi(profInfo, params.response);

        // Get all profiles before selecting a profile
        const configAllBeforeProfiles = profInfo.getAllProfiles().filter((prof) => prof.profLoc.osLoc.length !== 0);
        const sshBeforeProfiles = configAllBeforeProfiles.filter((prof) => prof.profType === "ssh");

        const allBeforeMergedProf = [];
        for (const profile of sshBeforeProfiles) {
            allBeforeMergedProf.push(profInfo.mergeArgsForProfile(profile));
        }

        const profile = await cliPromptApi.promptForProfile(undefined, {
            skipCache: ImperativeConfig.instance.daemonContext?.stream == null,
            setExistingProfile: false,
            disableCreateNewProfile: true,
        });

        if (!profile) {
            params.response.console.error(
                "SSH setup cancelled or unable to validate config. Please check credentials for the selected profile.",
            );
            return;
        }

        // Get all profiles after selecting a profile
        const configAllAfterProfiles = profInfo.getAllProfiles().filter((prof) => prof.profLoc.osLoc.length !== 0);

        // Get profiles from before selecting that match the user of the selected profile
        const beforeMergedSelectedProf = allBeforeMergedProf.find((prof) =>
            prof.knownArgs.find(
                (arg) =>
                    arg.argName === "user" &&
                    arg.argLoc.jsonLoc ===
                        `${configAllAfterProfiles.find((find) => find.profName === profile.name)?.profLoc?.jsonLoc}.properties.user`,
            ),
        )?.knownArgs;

        // Map all argument names to their jsonLoc
        const beforeKeys = new Set(
            beforeMergedSelectedProf?.map((field) => `${field.argName}::${field.argLoc.jsonLoc}`),
        );

        // Find profile with matching name and type ssh
        const afterProfiles = configAllAfterProfiles.find(
            (prof) => prof.profName === profile.name && prof.profType === "ssh",
        );

        // Get merged args for the found profile
        const afterMergedSelectedProf = profInfo.mergeArgsForProfile(afterProfiles).knownArgs;

        // Get the difference between the new profiles and the old profiles to set on the Config object
        const newProperties = afterMergedSelectedProf.filter(
            (field) => !beforeKeys.has(`${field.argName}::${field.argLoc.jsonLoc}`),
        );

        const Config = ImperativeConfig.instance.config;

        // Set the new fields on the Config object, set as secure if a credential manager is active and the property is secure.
        const isSecured = profInfo.isSecured();
        for (const property of newProperties) {
            Config.set(property.argLoc.jsonLoc, property.argValue, { secure: isSecured && property.secure });
        }

        if (newProperties.length > 0 && newProperties[0].argLoc?.osLoc?.[0]) {
            const segments = profile.name.split(".");
            const profileTypeJsonId = segments.reduce(
                (all, val, i) => (i === segments.length - 1 ? `${all}.${val}.type` : `${all}.${val}.profiles`),
                "profiles",
            );
            Config.set(profileTypeJsonId, "ssh");
        }

        if (profile && Config.properties.defaults.ssh !== profile.name) {
            const defaultResponse = (
                await params.response.console.prompt(
                    `Would you like to set ${profile.name} as the default SSH Profile? (Y/n)`,
                )
            )
                .toLocaleLowerCase()
                .trim();
            if (defaultResponse === "y" || defaultResponse === "") {
                await profInfo.readProfilesFromDisk();
                Config.api.profiles.defaultSet("ssh", profile.name);
                await Config.save();
            }
        }
        if (profile) {
            params.response.console.log(
                `SSH Profile Validated: ${(profInfo.getTeamConfig().api.layers.find(profile.name) as IConfigLayer).path}`,
            );
        }
    }
}

export class CliPromptApi extends AbstractConfigManager {
    protected async showPrivateKeyWarning(opts: PrivateKeyWarningOptions): Promise<boolean> {
        // Display the warning message
        const warningMessage = `\n[!]  Private key for "${opts.profileName}" is invalid and was moved to a comment.`;
        this.mResponseApi.console.log(TextUtils.chalk.yellow(warningMessage));
        this.mResponseApi.console.log(TextUtils.chalk.grey(`     Original value: ${opts.privateKeyPath}`));
        this.mResponseApi.console.log(TextUtils.chalk.bold.green("\nHow would you like to proceed?"));

        const menuItems = [
            `Accept and continue          ${TextUtils.chalk.dim("│")} ${TextUtils.chalk.italic("keep the invalid private key comment and proceed")}`,
            `Delete comment and continue  ${TextUtils.chalk.dim("│")} ${TextUtils.chalk.italic("remove the private key comment and proceed")}`,
            `Undo and cancel              ${TextUtils.chalk.dim("│")} ${TextUtils.chalk.italic("restore the private key and cancel the operation")}`,
        ];

        const selectedIndex = 0;
        const menu = this.term.singleColumnMenu(menuItems, {
            cancelable: true,
            continueOnSubmit: false,
            oneLineItem: true,
            selectedIndex,
            submittedStyle: this.term.bold.green,
            selectedStyle: this.term.green,
            leftPadding: "  ",
            selectedLeftPadding: "> ",
            submittedLeftPadding: "> ",
        });
        const response = await menu.promise;
        this.mResponseApi.console.log("");

        switch (response.selectedIndex) {
            case 0: // Accept and continue
                this.mResponseApi.console.log("Continuing with commented private key...");
                return true;
            case 1: // Delete comment and continue
                this.mResponseApi.console.log("Deleting commented line...");
                if (opts.onDelete) {
                    await opts.onDelete();
                }
                return true;
            default:
                this.mResponseApi.console.log("Restoring private key and cancelling...");
                if (opts.onUndo) {
                    await opts.onUndo();
                }
                return false;
        }
    }

    constructor(
        mProfilesCache: ProfileInfo,
        private mResponseApi: IHandlerResponseApi,
    ) {
        super(mProfilesCache);
    }

    private term = termkit.terminal;
    protected showMessage(message: string, type: MESSAGE_TYPE): void {
        switch (type) {
            case MESSAGE_TYPE.INFORMATION:
                this.mResponseApi.console.log(message);
                break;
            case MESSAGE_TYPE.WARNING:
                this.mResponseApi.console.log(TextUtils.chalk.yellow(message));
                break;
            case MESSAGE_TYPE.ERROR:
                this.mResponseApi.console.error(TextUtils.chalk.red(message));
                break;
            default:
                throw new ImperativeError({ msg: "Unknown message type" });
        }
    }

    protected async showInputBox(opts: inputBoxOpts): Promise<string | undefined> {
        return await this.mResponseApi.console.prompt(`${opts.title}: `.replace("::", ":"), {
            hideText: opts.password,
        });
    }

    protected async withProgress<T>(_message: string, task: (progress: ProgressCallback) => Promise<T>): Promise<T> {
        return await task(() => {});
    }

    public async showMenu(opts: qpOpts): Promise<string | undefined> {
        return (await this.showCustomMenu(opts))?.label;
    }

    protected async showCustomMenu(opts: qpOpts): Promise<qpItem | undefined> {
        return new Promise<qpItem | undefined>((resolve) => {
            this.term.grabInput(true);

            // Add title and spacing to the menuItems as separators for easy handling
            opts.items.unshift({ label: "", separator: true });
            opts.items.unshift({
                label: TextUtils.chalk.bold.green(
                    `${opts.placeholder.replace("enter user@host", "create a new SSH host")}`,
                ),
                separator: true,
            });

            const menuItems = opts.items.map((item) =>
                item.separator && item.label === "Migrate From SSH Config"
                    ? `${"─".repeat(10)}Migrate from SSH Config${"─".repeat(10)}`
                    : item.label.replace("$(plus)", "+"),
            );

            let selectedIndex = 0;

            // Logic to skip over separators from opts objects
            while (opts.items[selectedIndex]?.separator) {
                selectedIndex++;
            }

            this.term.getCursorLocation((error, _x, y) => {
                if (error) {
                    console.error("Error getting cursor location:", error);
                    resolve(undefined);
                    return;
                }

                // Offset the terminal if near the end
                if (this.term.height - y - opts.items.length < 0) {
                    this.term.scrollUp(this.term.height - y);
                    this.term.move(0, this.term.height - y);
                }

                const menu = this.term.singleColumnMenu(menuItems, {
                    cancelable: true,
                    continueOnSubmit: false,
                    oneLineItem: true,
                    selectedIndex,
                    y: y + 1,
                    submittedStyle: this.term.bold.green,
                    selectedStyle: this.term.brightGreen,
                    leftPadding: "  ",
                    selectedLeftPadding: "> ",
                    submittedLeftPadding: "> ",
                }) as unknown as {
                    // biome-ignore lint/suspicious/noExplicitAny: Required for callback
                    on: (event: string, handler: (response: any) => void) => void;
                    abort: () => void;
                    select: (index: number) => void;
                };

                // Menu selection logic
                const moveSelection = (direction: 1 | -1) => {
                    let newIndex = selectedIndex;
                    do {
                        newIndex += direction;
                        if (newIndex < 0) newIndex = opts.items.length - 1;
                        if (newIndex >= opts.items.length) newIndex = 0;
                    } while (opts.items[newIndex]?.separator && newIndex >= 0 && newIndex < opts.items.length);

                    selectedIndex = newIndex;
                    menu.select(selectedIndex);
                };

                // Key bindings for navigation
                const keyHandler = (key: string) => {
                    if (key === "UP" || key === "k") moveSelection(-1);
                    if (key === "DOWN" || key === "j") moveSelection(1);
                    if (key === "TAB") moveSelection(1);
                };
                this.term.on("key", keyHandler);

                // Handle menu submission
                // biome-ignore lint/suspicious/noExplicitAny: Required for callback
                menu.on("submit", (response: any) => {
                    const selected = opts.items[response.selectedIndex];

                    // Cleanup event listeners and input grabbing
                    this.term.removeListener("key", keyHandler);
                    this.term.grabInput(false);
                    resolve(selected?.separator ? undefined : selected);
                });

                // Handle cancellation or ESCAPE
                const cancelHandler = (key: string) => {
                    if (key === "ESCAPE" || key === "CTRL_C") {
                        if (key === "CTRL_C") this.term.moveTo(1, y + opts.items.length + 2);
                        // Cleanup event listeners and input grabbing
                        this.term.removeListener("key", keyHandler);
                        this.term.removeListener("key", cancelHandler);
                        menu.abort();
                        this.term.grabInput(false);
                        resolve(undefined);
                    }
                };
                this.term.on("key", cancelHandler);
            });
        });
    }

    protected getCurrentDir(): string | undefined {
        return process.cwd();
    }

    protected getProfileSchemas(): IProfileTypeConfiguration[] {
        return ImperativeConfig.instance.loadedConfig.profiles;
    }
    protected storeServerPath(_host: string, _path: string): void {}

    protected getClientSetting<T>(_setting: keyof ISshSession): T | undefined {
        return undefined;
    }

    protected showStatusBar(): IDisposable | undefined {
        return undefined;
    }
}
