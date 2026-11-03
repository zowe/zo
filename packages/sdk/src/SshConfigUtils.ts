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

import { accessSync, constants, readdirSync, readFileSync } from "node:fs";
import { homedir, userInfo } from "node:os";
import * as path from "node:path";
import type { ISshSession } from "@zowe/zos-uss-for-zowe-sdk";
import * as sshConfig from "ssh-config";

export interface ISshConfigExt extends ISshSession {
    name?: string;
    /** Explicit, ordered SSH jump hosts. Omitted or "none" means a direct connection. */
    proxyJump?: string;
}
// biome-ignore lint/complexity/noStaticOnlyClass: Utilities class has static methods
export class SshConfigUtils {
    public static async findPrivateKeys(): Promise<string[]> {
        return SshConfigUtils.privateKeyPaths();
    }

    private static privateKeyPaths(): string[] {
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
        const parsedConfig = SshConfigUtils.readConfig();
        const sessions: ISshConfigExt[] = [];
        for (const config of parsedConfig) {
            if (config.type !== sshConfig.LineType.DIRECTIVE || config.param !== "Host") continue;
            const name = typeof config.value === "object" ? config.value[0].val : config.value;
            if (/[*!?]/.test(name)) continue;
            sessions.push({ name, ...SshConfigUtils.resolveConfig(parsedConfig, name, true) });
        }
        return sessions;
    }

    /** Resolve only the explicitly listed hops; never inherit another ProxyJump. */
    public static resolveJumpHosts(value?: string): ISshConfigExt[] {
        if (value == null) return [];
        if (typeof value !== "string" || !value.trim())
            throw new Error("proxyJump must be a non-empty string or 'none'");
        if (value.trim().toLowerCase() === "none") return [];
        const parsedConfig = SshConfigUtils.readConfig();
        return value.split(",").map((hop) => {
            const match = /^(?:([^@\s]+)@)?(\[[\da-fA-F:.]+\]|[^\s:@,[\]]+)(?::(\d+))?$/.exec(hop.trim());
            let host: string;
            let user: string | undefined;
            let port: number | undefined;
            if (hop.trim().startsWith("ssh://")) {
                const uri = new URL(hop.trim());
                if (uri.password || uri.search || uri.hash || (uri.pathname && uri.pathname !== "/")) {
                    throw new Error("Invalid proxyJump SSH URI");
                }
                host = uri.hostname;
                user = uri.username ? decodeURIComponent(uri.username) : undefined;
                port = uri.port ? Number(uri.port) : undefined;
            } else {
                if (!match) throw new Error(`Invalid proxyJump host at position ${value.split(",").indexOf(hop) + 1}`);
                [, user, host] = match;
                port = match[3] ? Number(match[3]) : undefined;
            }
            host = host.replace(/^\[|\]$/g, "");
            if (!host || host.startsWith("-")) throw new Error("Invalid proxyJump host");
            const config = SshConfigUtils.resolveConfig(parsedConfig, host);
            config.port = port ?? config.port ?? 22;
            if (!Number.isInteger(config.port) || config.port < 1 || config.port > 65535) {
                throw new Error("proxyJump port must be between 1 and 65535");
            }
            config.user = user ?? config.user ?? userInfo().username;
            if (!config.identityAgent && !config.privateKey) {
                const effective = parsedConfig.compute(host);
                if (effective.IdentityAgent?.toString().toLowerCase() !== "none")
                    config.identityAgent = process.env.SSH_AUTH_SOCK;
                if (!config.identityAgent) config.privateKey = SshConfigUtils.privateKeyPaths()[0];
            }
            return config;
        });
    }

    private static resolveConfig(config: sshConfig.default, host: string, includeJump = false): ISshConfigExt {
        const values = config.compute(host);
        const get = (name: string): string | undefined => {
            const value = values[name];
            return Array.isArray(value) ? value[0] : value;
        };
        const session: ISshConfigExt = {};
        session.hostname = get("HostName") ?? host;
        if (get("Port")) session.port = Number(get("Port"));
        if (get("User")) session.user = get("User");
        const key = get("IdentityFile");
        if (key && key.toLowerCase() !== "none") session.privateKey = SshConfigUtils.expandHome(key);
        if (get("ConnectTimeout")) session.handshakeTimeout = Number(get("ConnectTimeout")) * 1000;
        const agent = get("IdentityAgent");
        if (agent && agent.toLowerCase() !== "none") {
            session.identityAgent = agent === "SSH_AUTH_SOCK" ? agent : SshConfigUtils.expandHome(agent);
        }
        if (includeJump && get("ProxyJump")) session.proxyJump = get("ProxyJump");
        return session;
    }

    private static expandHome(value: string): string {
        return path.normalize(value.startsWith("~/") ? path.join(homedir(), value.slice(2)) : value);
    }

    private static readConfig(): sshConfig.default {
        const homeDir = homedir();
        const filePath = path.join(homeDir, ".ssh", "config");
        let fileContent: string;
        try {
            fileContent = readFileSync(filePath, "utf-8");
        } catch {
            return sshConfig.parse("");
        }
        const expanded = SshConfigUtils.expandIncludes(fileContent, new Set([filePath]));
        const parsed = sshConfig.parse(expanded);
        const names = [
            "Host",
            "Match",
            "HostName",
            "Port",
            "User",
            "IdentityFile",
            "IdentityAgent",
            "ConnectTimeout",
            "ProxyJump",
        ];
        const normalize = (lines: sshConfig.default): void => {
            for (const line of lines) {
                if (line.type !== sshConfig.LineType.DIRECTIVE) continue;
                line.param = names.find((name) => name.toLowerCase() === line.param.toLowerCase()) ?? line.param;
                if ("config" in line) normalize(line.config);
            }
        };
        normalize(parsed);
        return parsed;
    }

    private static expandIncludes(content: string, ancestors: Set<string>): string {
        return content.replace(/^\s*Include\s+(.+)$/gim, (_line, args: string) => {
            const tokens = args.match(/"[^"]+"|'[^']+'|#[^\n]*|[^\s#]+/g) ?? [];
            const commentIndex = tokens.findIndex((token) => token.startsWith("#"));
            const patterns = commentIndex < 0 ? tokens : tokens.slice(0, commentIndex);
            return patterns
                .flatMap((pattern) => {
                    const expanded = SshConfigUtils.expandHome(pattern.replace(/^["']|["']$/g, ""));
                    const absolute = path.isAbsolute(expanded) ? expanded : path.join(homedir(), ".ssh", expanded);
                    let paths = [path.parse(absolute).root];
                    for (const segment of absolute.slice(path.parse(absolute).root.length).split(path.sep)) {
                        paths = paths.flatMap((parent) => {
                            if (!/[*?]/.test(segment)) return [path.join(parent, segment)];
                            const expression = new RegExp(
                                `^${segment
                                    .replace(/[.+^${}()|[\]\\]/g, "\\$&")
                                    .replace(/\*/g, ".*")
                                    .replace(/\?/g, ".")}$`,
                            );
                            try {
                                return readdirSync(parent)
                                    .sort()
                                    .filter((name) => expression.test(name))
                                    .map((name) => path.join(parent, name));
                            } catch {
                                return [];
                            }
                        });
                    }
                    return paths.map((file) => {
                        if (ancestors.has(file) || ancestors.size >= 32)
                            throw new Error("Cyclic or excessively nested SSH Include");
                        try {
                            return SshConfigUtils.expandIncludes(
                                readFileSync(file, "utf-8"),
                                new Set([...ancestors, file]),
                            );
                        } catch (error) {
                            if ((error as NodeJS.ErrnoException).code === "ENOENT") return "";
                            throw error;
                        }
                    });
                })
                .join("\n");
        });
    }
}
