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

import { posix } from "node:path";
import { DeferredPromise, ImperativeError, type IProfileLoaded, Logger } from "@zowe/imperative";
import type { SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import { type Config, NodeSSH } from "node-ssh";
import type { ClientOptions, ExistingClientRequest, IDisposable } from "./doc";
import type { SessionContext } from "./utils/SessionContext";
import { ZSshClient } from "./ZSshClient";
import { ZSshUtils } from "./ZSshUtils";

export interface SshConnectionOptions {
    keepaliveInterval?: number;
    keepaliveCountMax?: number;
    readyTimeout?: number;
}

export interface SshConnectionLease extends Disposable {
    readonly ssh: NodeSSH;
    readonly key: string;
}

interface CachedConnection {
    ssh: NodeSSH;
    ready: Promise<void>;
    users: number;
    expiry?: NodeJS.Timeout;
    closed: boolean;
    connected: boolean;
}

class AsyncMutex extends DeferredPromise<void> implements Disposable {
    public constructor(private onDispose?: () => void) {
        super();
    }

    public [Symbol.dispose](): void {
        this.resolve();
        this.onDispose?.();
    }
}

export interface ZSshRestartOptions {
    restart: boolean;
    retryRequests: boolean;
}

export interface ZSshClientSessions {
    client: ZSshClient;
    profile: IProfileLoaded;
    status: ServerStatus;
    startTime: number;
    responseTimeoutMillis: number;
}

export enum ServerStatus {
    UP,
    DOWN,
    RESTARTING,
}

/** Reuses SSH transports and server clients, with overridable deployment and presentation hooks. */
export class SshClientCache implements Disposable {
    public static readonly TTL_MS = 10 * 60 * 1000;
    private static instance = new SshClientCache();
    protected readonly mClientSessionMap = new Map<string, ZSshClientSessions>();
    private readonly mMutexMap = new Map<string, AsyncMutex>();
    private static readonly mNoRestart: ZSshRestartOptions = { restart: false, retryRequests: false };

    protected constructor() {}

    public static get inst(): SshClientCache {
        return SshClientCache.instance;
    }

    protected static setInstance(instance: SshClientCache): void {
        SshClientCache.instance.dispose();
        SshClientCache.instance = instance;
    }

    public static readonly WRITE_ACCESS_TO_SERVER_PATH_ERR =
        `You do not have write access to the deployment directory '{0}'. ` +
        `The zo server could not be started. Configure a different directory, ` +
        `or grant yourself write permission to the deployment directory if possible. `;

    private readonly connections = new Map<string, CachedConnection>();

    /** Readable server identity; credentials are deliberately excluded. */
    public getKey(session: SshSession | SessionContext, serverPath = ZSshClient.DEFAULT_SERVER_PATH): string {
        const { user, hostname, port } = session.ISshSession;
        const host = hostname?.includes(":") && !hostname.startsWith("[") ? `[${hostname}]` : hostname;
        const normalizedPath =
            serverPath && !serverPath.startsWith("/") && !serverPath.startsWith("~") ? `~/${serverPath}` : serverPath;
        const remotePath = normalizedPath.split("/").map(encodeURIComponent).join("/");
        const uriPath = serverPath.startsWith("/") ? remotePath : `/${remotePath}`;
        return `ssh://${encodeURIComponent(user ?? "")}@${host}:${port ?? 22}${uriPath}`;
    }

    private transportKey(key: string): string {
        return key.slice(0, key.indexOf("/", "ssh://".length) + 1);
    }

    /** Publishes a connection only after the caller has validated authentication and command execution. */
    public store(session: SshSession, ssh: NodeSSH): void {
        const key = this.getKey(session, "");
        this.invalidate(key);
        const entry: CachedConnection = { ssh, ready: Promise.resolve(), users: 0, closed: false, connected: true };
        this.connections.set(key, entry);
        this.watchConnection(key, entry);
        this.scheduleExpiry(key, entry);
    }

    private watchConnection(key: string, entry: CachedConnection): void {
        const invalidate = () => {
            if (this.connections.get(key) === entry) this.invalidate(key);
        };
        // NodeSSH removes its handshake error listener after ready. Keep one while idle, too.
        entry.ssh.connection?.on("error", () => queueMicrotask(invalidate));
        entry.ssh.connection?.once("close", invalidate);
    }

    private scheduleExpiry(key: string, entry: CachedConnection): void {
        entry.expiry = setTimeout(() => {
            if (this.connections.get(key) === entry) this.invalidate(key);
        }, SshClientCache.TTL_MS);
        entry.expiry.unref?.();
    }

    /** Holds the transport until the lease is disposed; idle transports expire after ten minutes. */
    public async acquire(session: SshSession, options?: SshConnectionOptions): Promise<SshConnectionLease> {
        const key = this.getKey(session, "");
        let entry = this.connections.get(key);
        if (entry?.connected && !entry.ssh.isConnected()) {
            this.invalidate(key);
            entry = undefined;
        }
        if (!entry) {
            const ssh = new NodeSSH();
            entry = { ssh, ready: Promise.resolve(), users: 0, closed: false, connected: false };
            this.connections.set(key, entry);
            const current = entry;
            entry.ready = (async () => {
                try {
                    await ssh.connect(
                        ZSshUtils.buildSshConfig(session, {
                            keepaliveInterval: options?.keepaliveInterval ?? 30_000,
                            keepaliveCountMax: options?.keepaliveCountMax ?? 3,
                            readyTimeout: options?.readyTimeout ?? session.ISshSession.handshakeTimeout,
                        }) as Config,
                    );
                    if (current.closed) {
                        ssh.dispose();
                        throw new Error("SSH connection was invalidated while connecting");
                    }
                    current.connected = true;
                    this.watchConnection(key, current);
                } catch (error) {
                    if (this.connections.get(key) === current) this.invalidate(key);
                    throw error;
                }
            })();
        }
        const current = entry;
        clearTimeout(current.expiry);
        current.users++;
        let released = false;
        const release = () => {
            if (released) return;
            released = true;
            current.users--;
            if (!current.closed && current.users === 0) {
                this.scheduleExpiry(key, current);
            }
        };
        try {
            await current.ready;
            if (current.closed) throw new Error("SSH connection was invalidated while connecting");
            return { ssh: current.ssh, key: this.getKey(session), [Symbol.dispose]: release };
        } catch (error) {
            release();
            throw error;
        }
    }

    /** Immediately closes a transport, including any clients using it. */
    public invalidate(key: string | undefined): void {
        if (key == null) return;
        const connectionKey = this.transportKey(key);
        const entry = this.connections.get(connectionKey);
        if (!entry) return;
        this.connections.delete(connectionKey);
        entry.closed = true;
        for (const [clientId, session] of this.mClientSessionMap) {
            if (session.client.cacheKey != null && this.transportKey(session.client.cacheKey) === connectionKey)
                this.end(clientId);
        }
        clearTimeout(entry.expiry);
        entry.ssh.dispose();
    }

    public dispose(opts?: ZSshRestartOptions): void {
        const sessions = [...this.mClientSessionMap.values()];
        this.mClientSessionMap.clear();
        for (const session of sessions) session.client.dispose(opts?.restart);
        for (const key of this.connections.keys()) this.invalidate(key);
    }

    public async connect(
        profile: IProfileLoaded,
        opts: ZSshRestartOptions = SshClientCache.mNoRestart,
    ): Promise<ZSshClient> {
        const clientId = this.getClientId(profile);
        let replayRequests: Set<ExistingClientRequest> = new Set();
        while (this.mMutexMap.has(clientId)) {
            await this.mMutexMap.get(clientId)!.promise;
        }
        const cachedClient = this.mClientSessionMap.get(clientId)?.client;
        if (
            cachedClient?.cacheKey &&
            cachedClient.cacheKey !==
                this.getKey(ZSshUtils.buildSession(profile.profile!), this.getClientOptions(profile).serverPath)
        ) {
            this.end(clientId);
        }
        if (opts.restart) {
            if (opts.retryRequests) {
                const existingClient = this.mClientSessionMap.get(clientId)?.client;
                if (existingClient) {
                    replayRequests = existingClient.collectAllRequests(opts.retryRequests);
                }
            }
            this.end(clientId, opts);
        }

        if (!this.mClientSessionMap.has(clientId)) {
            using _lock = this.acquireProfileLock(clientId);
            const statusBarMsg = this.onConnecting(profile, opts);
            const session = ZSshUtils.buildSession(profile.profile!);

            const clientOptions = this.getClientOptions(profile);
            let serverPath = clientOptions.serverPath ?? ZSshClient.DEFAULT_SERVER_PATH;
            const responseTimeout = clientOptions.responseTimeout ?? 60;

            let newClient: ZSshClient | undefined;
            let serverNotFound = false;
            let serverShouldDeploy = true;

            const launchServer = async (): Promise<boolean> => {
                newClient?.dispose();
                newClient = await this.buildClient(session, clientId, {
                    ...clientOptions,
                    serverPath,
                    requests: replayRequests,
                });
                Logger.getAppLogger().debug(`Server version: ${newClient.serverVersion}`);
                serverNotFound = false;
                if (ZSshUtils.checkIfOutdated(newClient.serverVersion)) {
                    // assume autoUpdate is allowed unless the SSH profile says otherwise
                    if (profile.profile?.autoUpdate === false) {
                        Logger.getAppLogger().warn(
                            `Profile has autoUpdate set to false, skipping update for ${profile.name}`,
                        );
                        return false;
                    } else {
                        Logger.getAppLogger().info(
                            `Server is out of date, deploying to ${profile.name} at %s`,
                            serverPath,
                        );

                        return true;
                    }
                }
                return false;
            };

            try {
                using _workflowLease =
                    !clientOptions.skipCache && !clientOptions.useNativeSsh
                        ? await this.acquire(session, {
                              keepaliveInterval:
                                  clientOptions.keepAliveInterval != null
                                      ? clientOptions.keepAliveInterval * 1000
                                      : undefined,
                              keepaliveCountMax: clientOptions.keepAliveCountMax,
                          })
                        : undefined;
                try {
                    serverShouldDeploy = await launchServer();
                } catch (err) {
                    if (err instanceof Error && "errorCode" in err && err.errorCode === "ENOTFOUND") {
                        serverNotFound = true;
                        Logger.getAppLogger().info(`Server is missing, deploying to ${profile.name}`);
                    } else {
                        throw err;
                    }
                }
                if (serverShouldDeploy) {
                    if (serverNotFound) {
                        const onEnvPathServer = await this.detectServerOnPath(session);
                        if (onEnvPathServer) {
                            try {
                                serverPath = onEnvPathServer;
                                Logger.getAppLogger().info(
                                    `Launching the server found at the user's $PATH at ${onEnvPathServer}`,
                                );
                                serverShouldDeploy = await launchServer();
                            } catch (err) {
                                Logger.getAppLogger().error(
                                    `Failed to launch server for profile ${profile.name} after detecting it on the user's $PATH:`,
                                    err,
                                );
                                throw err;
                            }
                        }
                    }

                    if (serverShouldDeploy) {
                        if (await ZSshUtils.lacksWriteAccess(session, serverPath)) {
                            if (serverNotFound) {
                                // the user has no usable instance of the zo server so we should notify them
                                const errMsg = this.getWriteAccessError(serverPath);
                                Logger.getAppLogger().error(errMsg);
                                throw new ImperativeError({ msg: errMsg });
                            } else {
                                // otherwise we were just trying to update and the user can use the old version
                                Logger.getAppLogger().warn(
                                    "Skipped deploy step as server path '%s' is not writeable by the user",
                                    serverPath,
                                );
                            }
                        } else {
                            if (
                                !(await this.confirmServerDeployment()) ||
                                !(await this.deployServer(session, serverPath))
                            ) {
                                throw this.createCancellationError();
                            }
                            newClient?.dispose();
                            newClient = await this.buildClient(session, clientId, {
                                ...clientOptions,
                                serverPath,
                                requests: replayRequests,
                            });
                        }
                    }
                }

                this.mClientSessionMap.set(clientId, {
                    client: newClient!,
                    profile: profile,
                    status: ServerStatus.UP,
                    startTime: Date.now(),
                    responseTimeoutMillis: responseTimeout * 1000,
                });
            } catch (error) {
                newClient?.dispose();
                throw error;
            } finally {
                statusBarMsg?.dispose();
            }
        }

        return this.mClientSessionMap.get(clientId)?.client as ZSshClient;
    }

    public end(hostOrProfile: string | IProfileLoaded, opts: ZSshRestartOptions = SshClientCache.mNoRestart): void {
        const clientId = typeof hostOrProfile === "string" ? hostOrProfile : this.getClientId(hostOrProfile);
        const session = this.mClientSessionMap.get(clientId);
        this.mClientSessionMap.delete(clientId);
        session?.client.dispose(opts.restart);
    }

    protected getClientId(profile: IProfileLoaded): string {
        return `${profile.name}_${profile.type}`;
    }

    private acquireProfileLock(clientId: string): AsyncMutex {
        const lock = new AsyncMutex(() => this.mMutexMap.delete(clientId));
        this.mMutexMap.set(clientId, lock);
        return lock;
    }

    private async buildClient(session: SshSession, clientId: string, opts: ClientOptions): Promise<ZSshClient> {
        // biome-ignore lint/style/useConst: The close callback may run before create resolves.
        let client: ZSshClient | undefined;
        client = await ZSshClient.create(session, {
            ...opts,
            onClose: () => {
                if (client && this.mClientSessionMap.get(clientId)?.client === client) this.end(clientId);
            },
            onError: (err: Error) => {
                this.handleClientError(clientId, err);
            },
        });
        return client;
    }

    protected getClientOptions(profile: IProfileLoaded): ClientOptions {
        return { serverPath: profile.profile?.serverPath };
    }

    protected onConnecting(_profile: IProfileLoaded, _opts: ZSshRestartOptions): IDisposable | undefined {
        return undefined;
    }

    protected async confirmServerDeployment(): Promise<boolean> {
        return true;
    }

    protected deployServer(session: SshSession | SessionContext, serverPath: string): Promise<boolean> {
        return ZSshUtils.installServer(session, serverPath);
    }

    protected getWriteAccessError(serverPath: string): string {
        return SshClientCache.WRITE_ACCESS_TO_SERVER_PATH_ERR.replace("{0}", serverPath);
    }

    protected createCancellationError(): Error {
        return new ImperativeError({ msg: "Server deployment cancelled", errorCode: "ECANCELLED" });
    }

    protected handleClientError(_clientId: string, error: Error): void {
        Logger.getAppLogger().error(error.toString());
    }

    public async detectServerOnPath(session: SshSession | SessionContext): Promise<string | undefined> {
        const details = await ZSshUtils.detectServerOnPath(session);
        return details.serverPath && details.hasExecutePermission ? posix.dirname(details.serverPath) : undefined;
    }

    public [Symbol.dispose](): void {
        this.dispose();
    }
}
