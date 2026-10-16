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
import type { Stream } from "node:stream";
import { ImperativeError, Logger } from "@zowe/imperative";
import type { SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import type {
    ClientOptions,
    CommandRequest,
    CommandResponse,
    ExistingClientRequest,
    RpcNotification,
    RpcRequest,
    RpcResponse,
    StatusMessage,
} from "./doc";
import { RpcClientApi } from "./RpcClientApi";
import { RpcStreamManager } from "./RpcStreamManager";
import { matchLeRuntimeFailure } from "./SshErrors";
import { type Client, type ClientChannel, createClient } from "./ssh-rs";
import { ZSshUtils } from "./ZSshUtils";

export class ZSshClient extends RpcClientApi implements Disposable {
    public static readonly DEFAULT_SERVER_PATH = "~/.zowe-server";
    public static readonly BIN_NAME = "zo";
    public static readonly REQUIRED_DEPLOY_SIZE_MB = 20;
    private static readonly DEFAULT_SERVER_STARTUP_TIMEOUT_S = 60;
    private static readonly EXIT_CODE_MARKER = "@@ZOWE_EXIT_CODE@@";
    private static readonly EXIT_CODE_PATTERN = new RegExp(String.raw`${ZSshClient.EXIT_CODE_MARKER}(\d+)`);
    private mErrHandler: ClientOptions["onError"];
    private mResponseTimeout: number;
    private mServerInfo: { version?: string };
    private mSshClient: Client;
    private mSshStream: ClientChannel;
    private mStreamMgr: RpcStreamManager;
    private mClosed = false;
    // Tracked separately from mClosed: dispose() marks the client closed up front, but
    // the disconnect it triggers still needs to reach the onClose handler once.
    private mDisconnectHandled = false;
    private mPartialStderr = "";
    private mPartialStdout = "";
    // Non-JSON output seen while waiting for the server's ready banner. Accumulated because a
    // multi-line diagnostic (e.g. a CEE3561S load failure) can arrive split across chunks, and no
    // single chunk would then match.
    private mStartupOutput = "";
    private readonly mRequestMap: Map<number, ExistingClientRequest> = new Map();
    // biome-ignore lint/correctness/noUnusedPrivateClassMembers: Not unused, sent to SSH server
    private mRequestId = 0;

    private constructor() {
        super();
    }

    private static defaultErrHandler(err: Error): void {
        const errMsg = err.toString();
        console.error(errMsg);
        Logger.getAppLogger().error(errMsg);
    }

    public static async create(session: SshSession, opts: ClientOptions = {}): Promise<ZSshClient> {
        Logger.getAppLogger().debug("Starting SSH client");
        const client = new ZSshClient();
        client.mErrHandler = opts.onError ?? ZSshClient.defaultErrHandler;
        client.mResponseTimeout = opts.responseTimeout ? opts.responseTimeout * 1000 : 60e3;
        client.mSshClient = createClient(opts.useNativeSsh);
        try {
            client.mSshStream = await new Promise<ClientChannel>((resolve, reject) => {
                let established = false;
                const onStartupError = (err: Error) => {
                    if (established) {
                        return;
                    }
                    established = true;
                    clearTimeout(serverStartupTimeoutId);
                    reject(err);
                };
                const serverStartupTimeoutS = opts.serverStartupTimeout ?? ZSshClient.DEFAULT_SERVER_STARTUP_TIMEOUT_S;
                const serverStartupTimeoutId = setTimeout(() => {
                    // Any output collected while waiting is the only clue as to why the server
                    // never reported itself ready, so surface it rather than dropping it.
                    const [, startupOutput] = client.stripExitCode(client.mStartupOutput);
                    onStartupError(
                        new ImperativeError({
                            msg: "Timed out waiting for the Zowe server to start",
                            errorCode: "ESERVERSTARTUPTIMEOUT",
                            additionalDetails: startupOutput,
                        }),
                    );
                }, serverStartupTimeoutS * 1000);
                client.mSshClient.on("error", (err) => {
                    if (!established) {
                        Logger.getAppLogger().error(`Error connecting to SSH: ${err}`);
                        onStartupError(err);
                        return;
                    }
                    // Socket errors during teardown are noise; the disconnect was already
                    // reported. The listener stays attached so an `error` event never goes
                    // unhandled, which Node would rethrow.
                    if (client.mClosed) {
                        Logger.getAppLogger().debug(`Ignoring SSH error after client closed: ${err}`);
                        return;
                    }
                    Logger.getAppLogger().error(`Error connecting to SSH: ${err}`);
                    client.mErrHandler(err);
                });
                client.mSshClient.on("ready", () => {
                    const zoBin = posix.join(opts.serverPath ?? ZSshClient.DEFAULT_SERVER_PATH, ZSshClient.BIN_NAME);
                    const serverArgs = ["server"];
                    if (opts.numWorkers != null) {
                        serverArgs.push("--num-workers", `${opts.numWorkers}`);
                    }
                    if (opts.requestTimeout != null) {
                        serverArgs.push("--request-timeout", `${opts.requestTimeout}`);
                    }
                    if (opts.verbose) {
                        serverArgs.push("--verbose");
                    }
                    client.execAsync(ZSshClient.quoteRemotePath(zoBin), ...serverArgs).then((stream) => {
                        established = true;
                        clearTimeout(serverStartupTimeoutId);
                        resolve(stream);
                    }, onStartupError);
                });
                client.mSshClient.on("close", () => {
                    Logger.getAppLogger().debug("Client disconnected");
                    if (!established) {
                        onStartupError(
                            new ImperativeError({
                                msg: "SSH connection closed before the Zowe Remote SSH server was ready",
                                errorCode: "ECONNCLOSED",
                            }),
                        );
                        return;
                    }
                    client.handleDisconnect("SSH connection closed", opts.onClose);
                });
                const keepAliveMsec = opts.keepAliveInterval != null ? opts.keepAliveInterval * 1000 : 30e3;
                client.mSshClient.connect(
                    ZSshUtils.buildSshConfig(session, {
                        keepaliveInterval: keepAliveMsec,
                        keepaliveCountMax: opts.keepAliveCountMax ?? 3,
                    }),
                );
            });
        } catch (err) {
            // Clean up the SSH connection so failed startups do not leak connections,
            // keep-alive traffic, or remote server processes
            client.mSshClient.removeAllListeners();
            client.mSshClient.end();
            throw err;
        }
        client.mStreamMgr = new RpcStreamManager(client.mSshClient);
        client.watchServerChannel(opts.onClose);

        if (opts.requests != null) {
            for (const req of opts.requests) {
                client.request(req.command).then(req.rpc.resolve, req.rpc.reject);
            }
        }

        return client;
    }

    /**
     * Collects all currently pending RPC requests. Can be used to pause, replay, or inspect inflight requests.
     * * @param silence - If true, silences the requests. They will no longer trigger their resolve or reject methods in normal operation (timeout, server response). The caller assumes responsibility for these resolve and rejects.
     * @returns A set of existing client requests currently pending resolution.
     */
    public collectAllRequests(silence: boolean = false): Set<ExistingClientRequest> {
        const replayRequests: Set<ExistingClientRequest> = new Set();
        this.mRequestMap.forEach((req) => {
            req.silenced = silence;
            replayRequests.add(req);
            if (req.timeoutId) {
                // don't wait for timeout
                clearTimeout(req.timeoutId);
            }
        });
        return replayRequests;
    }

    /**
     * Cleans up the SSH client by ending the SSH connection and rejecting pending requests
     * that have not been silenced. If you want to silence requests, see {@link collectAllRequests}
     * * @param isRestart - Indicates if the disposal is part of a server restart sequence
     * (changes the rejection message sent to pending requests).
     */
    public dispose(isRestart: boolean = false): void {
        Logger.getAppLogger().debug("Stopping SSH client");
        this.mClosed = true;
        this.mRequestMap.forEach((req) => {
            let rejMsg = "Shutting down ZRS. No action is required.";
            if (isRestart) {
                rejMsg = "Restarting ZRS";
            }
            if (!req.silenced) {
                req.rpc.reject(new ImperativeError({ msg: rejMsg, suppressDump: true }));
            }
        });
        this.mRequestMap.clear();
        this.mSshClient?.end();
    }

    public [Symbol.dispose](): void {
        this.dispose();
    }

    public get serverVersion(): string | undefined {
        return this.mServerInfo?.version;
    }

    /**
     * Watches the server's exec channel for the rest of the client's lifetime.
     *
     * The transport can stay healthy while the remote server process exits, in which
     * case no `close` fires on the SSH client and requests would otherwise be written
     * into a dead stream and wait out the full response timeout.
     */
    private watchServerChannel(onClose: ClientOptions["onClose"]): void {
        if (typeof this.mSshStream?.on !== "function") {
            return;
        }
        const onChannelClose = () => {
            this.handleDisconnect("Zowe Remote SSH server process ended", onClose);
        };
        this.mSshStream.on("close", onChannelClose);
        this.mSshStream.on("exit", onChannelClose);
    }

    /**
     * Marks the client as closed and rejects pending requests that the `onClose`
     * handler didn't claim via {@link collectAllRequests}, so they fail fast
     * instead of waiting for the response timeout.
     *
     * Idempotent: the transport and the server channel can both report the same
     * disconnect, and `dispose` closes the channel itself.
     * @param reason - Message used to reject any requests still pending.
     * @param onClose - Caller hook given a chance to claim pending requests (e.g. to retry them elsewhere) before the rest are rejected.
     */
    private handleDisconnect(reason: string, onClose?: ClientOptions["onClose"]): void {
        if (this.mDisconnectHandled) {
            return;
        }
        this.mDisconnectHandled = true;
        this.mClosed = true;
        Logger.getAppLogger().debug(reason);
        // Give onClose a chance to claim pending requests via collectAllRequests()
        // before rejecting whatever's left, so those fail fast instead of timing out
        Promise.resolve(onClose?.())
            .catch((err) => this.mErrHandler(err))
            .finally(() => this.rejectPendingRequests(reason));
    }

    private rejectPendingRequests(reason: string): void {
        for (const [id, req] of this.mRequestMap) {
            if (req.silenced) {
                continue;
            }
            req.rpc.reject(new ImperativeError({ msg: reason, errorCode: "ECONNCLOSED", suppressDump: true }));
            this.mRequestMap.delete(id);
        }
    }

    public async request<T extends CommandResponse>(
        request: CommandRequest,
        progressCallback?: (percent: number) => void,
    ): Promise<T> {
        // `writable` catches a channel that died before its close event was delivered,
        // which would otherwise swallow the write and stall until the response timeout
        if (this.mClosed || this.mSshStream?.stdin?.writable === false) {
            throw new ImperativeError({
                msg: "SSH connection is closed",
                errorCode: "ECONNCLOSED",
                suppressDump: true,
            });
        }
        let timeoutId: NodeJS.Timeout;
        return new Promise<T>((resolve, reject) => {
            const { command, ...rest } = request;
            const rpcRequest: RpcRequest = {
                jsonrpc: "2.0",
                method: command,
                params: rest,
                id: ++this.mRequestId,
            };
            timeoutId = setTimeout(() => {
                const thisReq = this.mRequestMap.get(rpcRequest.id);
                if (thisReq) {
                    this.mRequestMap.delete(rpcRequest.id);
                    if (!thisReq.silenced) {
                        this.mErrHandler(new Error("Request timed out")); // our hook provides better error message and action
                        reject(new ImperativeError({ msg: "Request timed out", errorCode: "ETIMEDOUT" }));
                    }
                }
            }, this.mResponseTimeout);
            if ("stream" in request && this.isStreamFunction(request.stream)) {
                this.mStreamMgr.registerStream(
                    rpcRequest,
                    request.stream,
                    timeoutId,
                    progressCallback && {
                        callback: progressCallback,
                        // If stream is a ReadStream use the size of the localFile in bytes
                        // If stream is a WriteStream, set undefined because the size will be provided by a notification
                        totalBytes: "contentLen" in request ? (request.contentLen as number) : undefined,
                    },
                );
            }
            this.mRequestMap.set(rpcRequest.id, {
                command: request,
                rpc: {
                    resolve: resolve as (value: CommandResponse) => void | Promise<void>,
                    reject,
                },
                silenced: false,
                timeoutId,
            });
            const requestStr = JSON.stringify(rpcRequest);
            Logger.getAppLogger().trace(`Sending request: ${requestStr}`);
            this.mSshStream.stdin.write(`${requestStr}\n`);
        }).finally(() => clearTimeout(timeoutId));
    }

    // Quotes a remote path for safe use in a shell command, leaving a leading "~" outside
    // the quotes so the shell still expands it to the user's home directory.
    private static quoteRemotePath(path: string): string {
        const expandHome = path === "~" || path.startsWith("~/");
        return expandHome ? `~${ZSshUtils.quotePath(path.slice(1))}` : ZSshUtils.quotePath(path);
    }

    private execAsync(...args: string[]): Promise<ClientChannel> {
        return new Promise((resolve, reject) => {
            const cmd = args.join(" ");
            const wrappedCmd = `${cmd} || echo "${ZSshClient.EXIT_CODE_MARKER}$?" >&2`;
            this.mSshClient.exec(wrappedCmd, (err, stream) => {
                if (err) {
                    Logger.getAppLogger().error(`Error running SSH command: ${err}`);
                    reject(err);
                } else {
                    let settled = false;
                    const removeListeners = () => {
                        stream.stderr.removeListener("data", onData);
                        stream.stdout.removeListener("data", onData);
                        if (typeof stream.removeListener === "function") {
                            stream.removeListener("close", onStreamClose);
                        }
                    };
                    const settle = (err?: Error) => {
                        if (settled) {
                            return;
                        }
                        settled = true;
                        removeListeners();
                        if (err) {
                            reject(err);
                        } else {
                            resolve(stream);
                        }
                    };
                    const onStreamClose = () => {
                        // No more output is coming, so report whatever was collected.
                        settle(
                            this.mStartupOutput.length > 0
                                ? this.buildStartupError(cmd)
                                : new ImperativeError({
                                      msg: "Zowe Remote SSH server process ended before it was ready",
                                      errorCode: "ESERVEREXIT",
                                  }),
                        );
                    };
                    const onData = (data: Buffer) => {
                        try {
                            this.mServerInfo = this.getServerStatus(stream, data.toString(), cmd);
                            if (this.mServerInfo) {
                                settle();
                                return;
                            }
                        } catch (err) {
                            settle(err as Error);
                        }
                    };
                    if (typeof stream.on === "function") {
                        stream.on("close", onStreamClose);
                    }
                    stream.stderr.on("data", onData);
                    stream.stdout.on("data", onData);
                }
            });
        });
    }

    private stripExitCode(output: string): [code: number, output: string] {
        // We print the exit code to stderr, since the exit-status of an exec channel only
        // arrives via a separate "exit" event, whose timing relative to stdout/stderr data
        // is not guaranteed, so it can't be read synchronously alongside stderr output.
        const match = ZSshClient.EXIT_CODE_PATTERN.exec(output);
        return [match ? Number(match[1]) : -1, output.replace(ZSshClient.EXIT_CODE_PATTERN, "").trimEnd()];
    }

    private getServerStatus(stream: ClientChannel, data: string, command: string): StatusMessage["data"] | undefined {
        Logger.getAppLogger().debug(`Received SSH data: ${data}`);
        let response: StatusMessage;
        try {
            response = JSON.parse(data);
        } catch {
            this.mStartupOutput += data;
            const errMsg = Logger.getAppLogger().error("Error starting Zowe server: %s\n%s", command, data);
            const [exitCode, cleanOutput] = this.stripExitCode(this.mStartupOutput);
            if (data.includes("FSUM7351") || exitCode === 127) {
                throw new ImperativeError({
                    msg: "Server not found",
                    errorCode: "ENOTFOUND",
                    additionalDetails: cleanOutput,
                });
            }
            if (data.includes("FOTS1681") && exitCode === -1) {
                // non-fatal chdir error, display the error but continue waiting for ready message
                this.mErrHandler(new Error(errMsg));
                return;
            }
            // Language Environment could not load the binary, so the server never started. Match on
            // the accumulated output: the message can be split across chunks.
            const leFailure = matchLeRuntimeFailure(this.mStartupOutput);
            if (leFailure != null) {
                throw new ImperativeError({
                    msg: leFailure,
                    errorCode: "ELERUNTIME",
                    additionalDetails: this.mStartupOutput,
                });
            }
            // The wrapper echoes the exit code after the failing command's own output, so its
            // arrival means the message is complete and the failure is definitive.
            if (exitCode !== -1) {
                throw this.buildStartupError(command);
            }
        }
        if (response?.status === "ready") {
            stream.stderr.on("data", this.onErrData.bind(this));
            stream.stdout.on("data", this.onOutData.bind(this));
            this.mStartupOutput = "";
            Logger.getAppLogger().debug("Client is ready");
            return response.data;
        }
    }

    private buildStartupError(command: string): ImperativeError {
        const [exitCode, cleanOutput] = this.stripExitCode(this.mStartupOutput);
        const exitCodeSuffix = exitCode !== -1 ? ` (exit code ${exitCode})` : "";
        return new ImperativeError({
            msg: `Error starting Zowe server: ${command}${exitCodeSuffix}`,
            additionalDetails: cleanOutput,
        });
    }

    private onErrData(chunk: Buffer) {
        if (this.mRequestMap.size === 0) {
            const errMsg = Logger.getAppLogger().error("Error from server: %s", chunk.toString());
            this.mErrHandler(new Error(errMsg));
            return;
        }
        this.mPartialStderr = this.processResponses(this.mPartialStderr + chunk.toString());
    }

    private onOutData(chunk: Buffer) {
        this.mPartialStdout = this.processResponses(this.mPartialStdout + chunk.toString());
    }

    private processResponses(data: string): string {
        const responses = data.split("\n");
        for (let i = 0; i < responses.length - 1; i++) {
            if (responses[i].length === 0) {
                continue;
            }

            Logger.getAppLogger().trace(`Received response: ${responses[i]}`);
            let response: RpcResponse | RpcNotification;
            try {
                response = JSON.parse(responses[i]);
            } catch {
                const errMsg = Logger.getAppLogger().error("Invalid JSON response: %s", responses[i]);
                this.mErrHandler(new Error(errMsg));
                continue;
            }

            const responseId: number = "id" in response ? response.id : response.params?.id;
            if (!this.mRequestMap.has(responseId)) {
                const errMsg = Logger.getAppLogger().error("Missing promise for response ID: %d", responseId);
                this.mErrHandler(new Error(errMsg));
                continue;
            }

            if ("method" in response) {
                this.handleNotification(response);
            } else {
                this.handleResponse(response);
            }
        }
        return responses[responses.length - 1];
    }

    private handleNotification(notif: RpcNotification): void {
        const rpcPromise = this.mRequestMap.get(notif.params?.id as number);
        try {
            switch (notif.method) {
                case "receiveStream":
                    this.mStreamMgr.linkStreamToPromise(rpcPromise.rpc, notif, "GET");
                    break;
                case "sendStream":
                    this.mStreamMgr.linkStreamToPromise(rpcPromise.rpc, notif, "PUT");
                    break;
                default:
                    throw new Error(`unknown method ${notif.method}`);
            }
        } catch (err) {
            const errMsg = Logger.getAppLogger().error("Failed to handle RPC notification: %s", err.message);
            this.mErrHandler(new Error(errMsg));
        }
    }

    private handleResponse(response: RpcResponse): void {
        const request = this.mRequestMap.get(response.id);

        if (request.silenced) {
            // Requests that are silenced have already been collected.
            this.mRequestMap.delete(response.id);
            return;
        }

        if (response.error != null) {
            Logger.getAppLogger().error(`Error for response ID: ${response.id}\n${JSON.stringify(response.error)}`);
            const { data } = response.error;
            this.mRequestMap.get(response.id).rpc.reject(
                new ImperativeError({
                    msg: response.error.message,
                    errorCode: response.error.code.toString(),
                    // The server may attach a structured error payload (e.g. the safReturns SAF/ESM
                    // codes from a failed certificate command) as an object instead of a string. Keep
                    // the raw value available via causeErrors, and stringify it for additionalDetails
                    // since that field is typed as a string.
                    ...(data !== undefined && {
                        causeErrors: data,
                        additionalDetails: typeof data === "string" ? data : JSON.stringify(data),
                    }),
                }),
            );
        } else {
            this.mRequestMap.get(response.id).rpc.resolve(response.result);
        }

        this.mRequestMap.delete(response.id);
    }

    /**
     * Type guard to verify if an incoming request parameter contains a function that returns a Node.js Stream.
     * * @param req - The unknown property to inspect.
     * @returns True if `req` is a function, and asserts it as a stream generator function.
     */
    private isStreamFunction(req: unknown): req is () => Stream {
        return typeof req === "function";
    }
}
