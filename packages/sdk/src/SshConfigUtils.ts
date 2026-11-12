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

import { accessSync, constants, readFileSync } from "node:fs";
import { homedir } from "node:os";
import * as path from "node:path";
import type { ISshSession } from "@zowe/zos-uss-for-zowe-sdk";
import * as sshConfig from "ssh-config";

export interface ISshConfigExt extends ISshSession {
    name?: string;
}
// biome-ignore lint/complexity/noStaticOnlyClass: Utilities class has static methods
export class SshConfigUtils {
    public static async findPrivateKeys(): Promise<string[]> {
        const keyNames = ["id_ed25519", "id_rsa", "id_ecdsa", "id_dsa"];
        const privateKeyPaths: Set<string> = new Set();
        // Check standard ~/.ssh private keys
        for (const algo of keyNames) {
            const keyPath = path.resolve(homedir(), ".ssh", algo);
            try {
                accessSync(keyPath, constants.R_OK);
                privateKeyPaths.add(keyPath);
            } catch {
                // Ignore missing keys
            }
        }
        return Array.from(privateKeyPaths);
    }

    public static async migrateSshConfig(): Promise<ISshConfigExt[]> {
        const homeDir = homedir();
        const filePath = path.join(homeDir, ".ssh", "config");
        let fileContent: string;
        try {
            fileContent = readFileSync(filePath, "utf-8");
        } catch {
            return [];
        }

        const parsedConfig = sshConfig.parse(fileContent);
        const SSHConfigs: ISshConfigExt[] = [];

        for (const config of parsedConfig) {
            if (config.type === sshConfig.LineType.DIRECTIVE && config.param === "Host") {
                const session: ISshConfigExt = {};
                // If it has multiple names, take the first
                session.name = typeof config.value === "object" ? config.value[0].val : (config.value as string);
                // Skip host names that contain wildcard characters
                if (session.name.includes("*") || session.name.includes("?")) continue;

                if (Array.isArray((config as sshConfig.Section).config)) {
                    for (const subConfig of (config as sshConfig.Section).config) {
                        if (typeof subConfig === "object" && "param" in subConfig && "value" in subConfig) {
                            const param = (subConfig as sshConfig.Directive).param.toLowerCase();
                            const value = subConfig.value as string;

                            switch (param) {
                                case "hostname":
                                    session.hostname = value;
                                    break;
                                case "port":
                                    session.port = Number.parseInt(value, 10);
                                    break;
                                case "user":
                                    session.user = value;
                                    break;
                                case "identityfile":
                                    session.privateKey = path.normalize(
                                        value.startsWith("~") ? path.join(homeDir, value.slice(2)) : value,
                                    );
                                    break;
                                case "connecttimeout":
                                    session.handshakeTimeout = Number.parseInt(value, 10) * 1000;
                                    break;
                                case "identityagent":
                                    // "none" explicitly disables the agent; leave session.identityAgent unset.
                                    if (value.toLowerCase() === "none") {
                                        break;
                                    }
                                    session.identityAgent =
                                        value === "SSH_AUTH_SOCK"
                                            ? value
                                            : path.normalize(
                                                  value.startsWith("~") ? path.join(homeDir, value.slice(2)) : value,
                                              );
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                SSHConfigs.push(session);
            }
        }
        return SSHConfigs;
    }
}
