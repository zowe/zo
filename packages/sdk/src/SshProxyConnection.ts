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

import { Client, type ClientChannel, type ConnectConfig } from "ssh2";

export interface SshConnectConfig extends ConnectConfig {
    jumpHosts?: ConnectConfig[];
}

export class SshProxyError extends Error {
    constructor(position: number, cause: unknown) {
        super(`SSH jump host ${position} failed: ${cause instanceof Error ? cause.message : String(cause)}`, { cause });
        this.name = "SshProxyError";
    }
}

/** Owns the jump connections for one target connection. */
export class SshProxyConnection implements Disposable {
    private readonly clients: Client[] = [];
    private readonly channels: ClientChannel[] = [];
    private disposed = false;

    public [Symbol.dispose](): void {
        if (this.disposed) return;
        this.disposed = true;
        for (const channel of this.channels.reverse()) channel.destroy();
        for (const client of this.clients.reverse()) client.end();
    }

    public async open(config: SshConnectConfig): Promise<ConnectConfig> {
        const { jumpHosts = [], ...target } = config;
        let sock = config.sock;
        let position = 1;
        try {
            for (const [index, hop] of jumpHosts.entries()) {
                position = index + 1;
                if (this.disposed) throw new Error("SSH jump connection was disposed");
                const client = new Client();
                this.clients.push(client);
                const timeout = hop.readyTimeout ?? config.readyTimeout ?? 30000;
                await new Promise<void>((resolve, reject) => {
                    client.on("error", reject);
                    client.once("close", () => reject(new Error(`SSH jump host ${index + 1} closed`)));
                    client.once("ready", resolve);
                    client.connect({
                        ...hop,
                        sock,
                        readyTimeout: timeout,
                        keepaliveInterval: config.keepaliveInterval ?? 30000,
                        keepaliveCountMax: config.keepaliveCountMax ?? 3,
                    });
                });
                const next = jumpHosts[index + 1] ?? target;
                sock = await new Promise<ClientChannel>((resolve, reject) => {
                    const timer = setTimeout(
                        () => reject(new Error(`Timed out forwarding through SSH jump host ${index + 1}`)),
                        timeout,
                    );
                    const closed = (): void => {
                        clearTimeout(timer);
                        reject(new Error(`SSH jump host ${index + 1} closed while forwarding`));
                    };
                    client.once("close", closed);
                    client.forwardOut("127.0.0.1", 0, next.host!, next.port ?? 22, (error, channel) => {
                        clearTimeout(timer);
                        client.removeListener("close", closed);
                        if (error)
                            return reject(
                                new Error(
                                    `SSH jump host ${index + 1} could not forward to the next host: ${error.message}`,
                                    {
                                        cause: error,
                                    },
                                ),
                            );
                        if (this.disposed) {
                            channel.destroy();
                            reject(new Error("SSH jump connection was disposed"));
                            return;
                        }
                        this.channels.push(channel);
                        channel.on("error", () => this[Symbol.dispose]());
                        resolve(channel);
                    });
                });
            }
            sock?.once("close", () => this[Symbol.dispose]());
            return { ...target, sock };
        } catch (error) {
            this[Symbol.dispose]();
            throw new SshProxyError(position, error);
        }
    }
}
