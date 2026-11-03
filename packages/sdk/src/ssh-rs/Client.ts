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

import { EventEmitter } from "node:events";
import {
    type AgentConnectionSpec,
    type AuthenticatedSSHClient,
    type AuthFailure,
    KeyPair,
    SSHClient,
    type SshPublicKey,
    SshTransport,
} from "russh";
import type { Subscription } from "rxjs";
import type { ConnectConfig } from "ssh2";
import { type SshConnectConfig, SshProxyError } from "../SshProxyConnection";
import { ClientChannel } from "./ClientChannel";

type DebugFn = (message: string) => void;
const noop: DebugFn = () => {};

export class Client extends EventEmitter {
    private static readonly DEFAULT_CONNECT_TIMEOUT_MS = 30e3;
    private sshClient: AuthenticatedSSHClient | null = null;
    private pendingClient: SSHClient | null = null;
    private disconnectSub: Subscription | null = null;
    private readonly jumpClients: AuthenticatedSSHClient[] = [];
    private readonly jumpSubscriptions: Subscription[] = [];
    private closed = false;
    private debug: DebugFn = noop;

    public connect(config: SshConnectConfig): void {
        if (this.closed) {
            throw new Error("Cannot reuse a closed SSH client — create a new instance");
        }
        this.debug = config.debug ?? noop;
        this.connectAsync(config).catch((err) => {
            this.cleanup();
            if (!this.closed) this.emit("error", err);
        });
    }

    public exec(command: string, callback: (err: Error | undefined, channel: ClientChannel) => void): void {
        if (!this.sshClient) {
            callback(new Error("Not connected"), undefined!);
            return;
        }
        this.execAsync(command)
            .then((ch) => callback(undefined, ch))
            .catch((err) => callback(err, undefined!));
    }

    public end(): void {
        this.debug("russh: end() called, disconnecting");
        this.cleanup();
        this.emitClose();
    }

    private cleanup(): void {
        const pending = this.pendingClient;
        this.pendingClient = null;
        pending?.disconnect().catch(() => {});
        for (const subscription of this.jumpSubscriptions.splice(0)) subscription.unsubscribe();
        for (const jump of this.jumpClients.splice(0).reverse()) jump.disconnect().catch(() => {});
        this.disconnectSub?.unsubscribe();
        this.disconnectSub = null;
        const client = this.sshClient;
        this.sshClient = null;
        if (client) {
            client.disconnect().catch(() => {});
        }
    }

    private emitClose(): void {
        if (this.closed) return;
        this.closed = true;
        this.debug("russh: emitting close");
        this.emit("close");
    }

    private async connectAsync(config: SshConnectConfig): Promise<void> {
        const hops = [...(config.jumpHosts ?? []), config];
        const first = hops[0];
        const firstHost = first.host ?? "localhost";
        const address = `${firstHost.includes(":") ? `[${firstHost}]` : firstHost}:${first.port ?? 22}`;
        this.debug(`russh: opening TCP connection to ${address}`);
        let transport = await this.openTransport(address, first.readyTimeout ?? config.readyTimeout);
        for (const [index, hop] of hops.entries()) {
            if (this.closed) throw new Error("SSH connection was disposed");
            let authenticated: AuthenticatedSSHClient;
            try {
                authenticated = await this.connectTransport(transport, hop);
            } catch (error) {
                if (index < hops.length - 1) throw new SshProxyError(index + 1, error);
                throw error;
            }
            if (this.closed) {
                await authenticated.disconnect();
                throw new Error("SSH connection was disposed");
            }
            if (index === hops.length - 1) {
                this.sshClient = authenticated;
            } else {
                this.jumpClients.push(authenticated);
                this.jumpSubscriptions.push(this.disconnectSub!);
                this.disconnectSub = null;
                const next = hops[index + 1];
                const channel = await this.withTimeout(
                    authenticated.openTCPForwardChannel({
                        addressToConnectTo: next.host!,
                        portToConnectTo: next.port ?? 22,
                        originatorAddress: "127.0.0.1",
                        originatorPort: 0,
                    }),
                    hop.readyTimeout ?? config.readyTimeout,
                    `Timed out forwarding through SSH jump host ${index + 1}`,
                );
                transport = await SshTransport.newSshChannel(channel.take());
            }
        }
        this.debug("russh: emitting ready");
        this.emit("ready");
    }

    private async connectTransport(transport: SshTransport, config: ConnectConfig): Promise<AuthenticatedSSHClient> {
        this.debug("russh: starting SSH handshake");
        const unauthClient = await SSHClient.connect(
            transport,
            async (key: SshPublicKey) => {
                this.debug(`russh: server key ${key.algorithm()} fingerprint=${key.fingerprint()}`);
                return true;
            },
            {
                connectionTimeoutSeconds: (config.readyTimeout ?? Client.DEFAULT_CONNECT_TIMEOUT_MS) / 1000,
                keepaliveIntervalSeconds:
                    config.keepaliveInterval != null ? config.keepaliveInterval / 1000 : undefined,
                keepaliveCountMax: config.keepaliveCountMax,
            },
        );
        this.debug("russh: SSH handshake complete");
        if (this.closed) {
            await unauthClient.disconnect();
            throw new Error("SSH connection was disposed");
        }
        this.pendingClient = unauthClient;

        const disconnectSub = unauthClient.disconnect$.subscribe(() => {
            this.debug("russh: disconnect event received from server");
            this.cleanup();
            this.emitClose();
        });
        this.disconnectSub = disconnectSub;

        let authClient: AuthenticatedSSHClient;
        try {
            const username = config.username ?? "root";
            authClient = await this.authenticate(unauthClient, username, config);
        } catch (err) {
            this.debug("russh: post-handshake error, tearing down connection");
            disconnectSub.unsubscribe();
            this.disconnectSub = null;
            unauthClient.disconnect().catch(() => {});
            throw err;
        } finally {
            this.pendingClient = null;
        }

        return authClient;
    }

    /**
     * Opens the TCP transport with a timeout so a stalled connection attempt cannot
     * hang the client indefinitely. Falls back to `readyTimeout` when configured.
     */
    private async openTransport(address: string, readyTimeout?: number): Promise<SshTransport> {
        return this.withTimeout(
            SshTransport.newSocket(address),
            readyTimeout,
            `Timed out opening TCP connection to ${address}`,
        );
    }

    private async withTimeout<T>(operation: Promise<T>, readyTimeout: number | undefined, message: string): Promise<T> {
        const timeoutMs = readyTimeout ?? Client.DEFAULT_CONNECT_TIMEOUT_MS;
        let timeoutId: NodeJS.Timeout;
        const timeoutPromise = new Promise<never>((_resolve, reject) => {
            timeoutId = setTimeout(() => reject(new Error(message)), timeoutMs);
        });
        try {
            return await Promise.race([operation, timeoutPromise]);
        } finally {
            clearTimeout(timeoutId!);
        }
    }

    private async authenticate(
        unauthClient: SSHClient,
        username: string,
        config: ConnectConfig,
    ): Promise<AuthenticatedSSHClient> {
        // Checked first: an explicitly configured agent is a deliberate override of
        // whatever privateKey/password a session also carries, not a fallback.
        if (typeof config.agent === "string") {
            this.debug(`russh: authenticating with agent for ${username}`);
            const result = await unauthClient.authenticateWithAgent(username, Client.resolveAgentSpec(config.agent));
            return this.assertAuthSuccess(result, "Agent");
        }

        if (config.privateKey) {
            this.debug(`russh: authenticating with public key for ${username}`);
            const keyData =
                typeof config.privateKey === "string"
                    ? config.privateKey
                    : Buffer.from(config.privateKey).toString("utf-8");
            const passphrase = typeof config.passphrase === "string" ? config.passphrase : undefined;
            const keyPair = await KeyPair.parse(keyData, passphrase);

            const result = await unauthClient.authenticateWithKeyPair(username, keyPair, null);
            return this.assertAuthSuccess(result, "Public key");
        }

        if (config.password) {
            this.debug(`russh: authenticating with password for ${username}`);
            const result = await unauthClient.authenticateWithPassword(username, config.password);
            return this.assertAuthSuccess(result, "Password");
        }

        throw new Error("No authentication method available: provide agent, privateKey, or password");
    }

    /**
     * Maps an ssh2-style `agent` string to a russh connection spec, following the same
     * convention as ssh2's own `agent` option: `"pageant"` selects Pageant, otherwise the
     * value is a socket path on POSIX or a named pipe path on Windows.
     */
    private static resolveAgentSpec(agent: string): AgentConnectionSpec {
        if (agent === "pageant") {
            return { kind: "pageant" };
        }
        return process.platform === "win32"
            ? { kind: "named-pipe", path: agent }
            : { kind: "unix-socket", path: agent };
    }

    private assertAuthSuccess(result: AuthenticatedSSHClient | AuthFailure, method: string): AuthenticatedSSHClient {
        if ("remainingMethods" in result) {
            const remaining = (result as AuthFailure).remainingMethods.join(", ");
            throw new Error(`${method} authentication failed. Remaining methods: ${remaining}`);
        }
        this.debug(`russh: ${method.toLowerCase()} authentication succeeded`);
        return result as AuthenticatedSSHClient;
    }

    private async execAsync(command: string): Promise<ClientChannel> {
        this.debug(`russh: opening channel for exec: ${command}`);
        const newCh = await this.sshClient!.openSessionChannel();
        const channel = await this.sshClient!.activateChannel(newCh);
        await channel.requestExec(command);
        this.debug(`russh: [ch ${channel.id}] exec channel opened`);
        return new ClientChannel(channel, this.debug);
    }
}
