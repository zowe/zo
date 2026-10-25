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

import { EventEmitter, Readable } from "node:stream";
import { Censor, Logger } from "@zowe/imperative";
import { type ISshSession, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import { Client, type ClientCallback, type ConnectConfig } from "ssh2";
import type { ReadableStreamRpc } from "../lib";
import type { CommandRequest, RpcRequest, RpcResponse } from "../src/doc";
import * as sshRs from "../src/ssh-rs";
import { ZSshClient } from "../src/ZSshClient";
import { ZSshUtils } from "../src/ZSshUtils";

vi.mock("ssh2");
vi.mock("../src/ssh-rs", () => {
    return { createClient: () => new Client() };
});

const fakeSession: ISshSession = {
    hostname: "example.com",
    port: 22,
    user: "admin",
    privateKey: "~/.ssh/id_rsa",
};

function setupMockSshClient(options?: {
    connectEvent?: "ready" | "error" | "close" | "closeBeforeReady";
    connectError?: Error;
    sshStream?: any;
    mockExecAsync?: boolean;
}) {
    const sshStream = options?.sshStream ?? new EventEmitter();
    const connectSpy = vi.spyOn(Client.prototype, "connect").mockImplementation(function (_config: ConnectConfig) {
        if (options?.connectEvent === "error") {
            this.emit("error", options.connectError ?? new Error("bad ssh"));
        } else if (options?.connectEvent === "close") {
            this.emit("ready");
            // Emit close after the server becomes ready so it exercises the post-connect path
            queueMicrotask(() => queueMicrotask(() => this.emit("close")));
        } else if (options?.connectEvent === "closeBeforeReady") {
            this.emit("ready");
            this.emit("close");
        } else {
            this.emit("ready");
        }
        return this;
    });
    let execAsyncSpy: any;
    if (options?.mockExecAsync !== false) {
        execAsyncSpy = vi.spyOn(ZSshClient.prototype as any, "execAsync").mockResolvedValue(sshStream);
    }
    return { sshStream, connectSpy, execAsyncSpy };
}

describe("ZSshClient", () => {
    const readyMessage = JSON.stringify({ status: "ready", data: { version: "0.6.1" } });
    const rpcRequest: RpcRequest = {
        jsonrpc: "2.0",
        method: "ping",
        params: {},
        id: 1,
    };
    const rpcResponseBad: RpcResponse = {
        jsonrpc: "2.0",
        error: { code: 0, message: "bad rpc" },
        id: 1,
    };
    const rpcResponseGood: RpcResponse = {
        jsonrpc: "2.0",
        result: { success: true },
        id: 1,
    };

    beforeAll(() => {
        vi.useFakeTimers();
    });

    beforeEach(() => {
        vi.spyOn(ZSshUtils, "buildSshConfig").mockReturnValue({});
    });

    describe("create function", () => {
        it("should start SSH client with default options", async () => {
            setupMockSshClient();
            const client = await ZSshClient.create(new SshSession(fakeSession));
            expect(client).toBeInstanceOf(ZSshClient);
        });

        it("should handle error thrown by SSH client", async () => {
            const sshError = new Error("bad ssh");
            setupMockSshClient({ connectEvent: "error", connectError: sshError });
            await expect(ZSshClient.create(new SshSession(fakeSession))).rejects.toThrow(sshError);
        });

        it("should handle error in SSH exec callback", async () => {
            const sshError = new Error("bad ssh");
            setupMockSshClient({ mockExecAsync: false });
            vi.spyOn(Client.prototype, "exec").mockImplementation(function (
                _command: string,
                callback: ClientCallback,
            ) {
                callback(sshError, undefined as any);
                return this;
            });
            await expect(ZSshClient.create(new SshSession(fakeSession))).rejects.toThrow(sshError);
        });

        it("should handle error in SSH data handler", async () => {
            const sshError = new Error("bad ssh");
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            setupMockSshClient({ sshStream, mockExecAsync: false });
            vi.spyOn(Client.prototype, "exec").mockImplementation(function (
                _command: string,
                callback: ClientCallback,
            ) {
                callback(undefined, sshStream as any);
                sshStream.stderr.emit("data", "");
                return this;
            });
            vi.spyOn(ZSshClient.prototype as any, "getServerStatus").mockImplementation(() => {
                throw sshError;
            });
            await expect(ZSshClient.create(new SshSession(fakeSession))).rejects.toThrow(sshError);
        });

        it("should invoke onClose callback", async () => {
            setupMockSshClient({ connectEvent: "close" });
            const onCloseMock = vi.fn();
            await ZSshClient.create(new SshSession(fakeSession), {
                onClose: onCloseMock,
            });
            expect(onCloseMock).toHaveBeenCalledTimes(1);
        });

        it("should invoke onError callback", async () => {
            setupMockSshClient();
            const onErrorMock = vi.fn();
            const client = await ZSshClient.create(new SshSession(fakeSession), {
                onError: onErrorMock,
            });
            (client as any).onErrData("bad ssh");
            expect(onErrorMock).toHaveBeenCalledTimes(1);
        });

        it("should respect keepAliveInterval option", async () => {
            setupMockSshClient();
            const buildSshConfigMock = vi.spyOn(ZSshUtils, "buildSshConfig");
            await ZSshClient.create(new SshSession(fakeSession), {
                keepAliveInterval: 5,
            });
            expect(buildSshConfigMock).toHaveBeenCalledTimes(1);
            expect(buildSshConfigMock.mock.calls[0][buildSshConfigMock.mock.calls[0].length - 1]).toEqual({
                keepaliveInterval: 5e3,
                keepaliveCountMax: 3,
            });
        });

        it("should pass default keepAliveCountMax to SSH config", async () => {
            setupMockSshClient();
            const buildSshConfigMock = vi.spyOn(ZSshUtils, "buildSshConfig");
            await ZSshClient.create(new SshSession(fakeSession));
            expect(buildSshConfigMock).toHaveBeenCalledTimes(1);
            expect(buildSshConfigMock.mock.calls[0][buildSshConfigMock.mock.calls[0].length - 1]).toEqual({
                keepaliveInterval: 30e3,
                keepaliveCountMax: 3,
            });
        });

        it("should respect keepAliveCountMax option", async () => {
            setupMockSshClient();
            const buildSshConfigMock = vi.spyOn(ZSshUtils, "buildSshConfig");
            await ZSshClient.create(new SshSession(fakeSession), {
                keepAliveCountMax: 5,
            });
            expect(buildSshConfigMock).toHaveBeenCalledTimes(1);
            expect(buildSshConfigMock.mock.calls[0][buildSshConfigMock.mock.calls[0].length - 1]).toEqual({
                keepaliveInterval: 30e3,
                keepaliveCountMax: 5,
            });
        });

        it("should reject when the connection closes before the server is ready", async () => {
            setupMockSshClient({ connectEvent: "closeBeforeReady" });
            await expect(ZSshClient.create(new SshSession(fakeSession))).rejects.toMatchObject({
                errorCode: "ECONNCLOSED",
            });
        });

        it("should clean up the SSH connection when startup fails", async () => {
            setupMockSshClient({ connectEvent: "closeBeforeReady" });
            const endSpy = vi.spyOn(Client.prototype, "end").mockImplementation(() => {});
            await expect(ZSshClient.create(new SshSession(fakeSession))).rejects.toMatchObject({
                errorCode: "ECONNCLOSED",
            });
            expect(endSpy).toHaveBeenCalledTimes(1);
        });

        it("should time out when the server never becomes ready", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            execAsyncSpy.mockReturnValue(new Promise(() => {}));
            const endSpy = vi.spyOn(Client.prototype, "end").mockImplementation(() => {});
            const promise = ZSshClient.create(new SshSession(fakeSession), { serverStartupTimeout: 5 });
            const assertion = expect(promise).rejects.toMatchObject({ errorCode: "ESERVERSTARTUPTIMEOUT" });
            await vi.advanceTimersByTimeAsync(5e3);
            await assertion;
            expect(endSpy).toHaveBeenCalledTimes(1);
        });

        it("should time out when startup output arrives but the failure is never confirmed", async () => {
            // Unrecognized output with no exit code and no channel close: execAsync keeps waiting,
            // so the startup timeout has to be what ends it.
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            setupMockSshClient({ sshStream, mockExecAsync: false });
            vi.spyOn(Client.prototype, "exec").mockImplementation(function (
                _command: string,
                callback: ClientCallback,
            ) {
                callback(undefined, sshStream as any);
                sshStream.stderr.emit("data", "some unexpected warning\n");
                return this;
            });
            vi.spyOn(Client.prototype, "end").mockImplementation(() => {});
            const promise = ZSshClient.create(new SshSession(fakeSession), { serverStartupTimeout: 5 });
            // The collected output is the only clue as to why startup never completed.
            const assertion = expect(promise).rejects.toMatchObject({
                errorCode: "ESERVERSTARTUPTIMEOUT",
                mDetails: expect.objectContaining({ additionalDetails: "some unexpected warning" }),
            });
            await vi.advanceTimersByTimeAsync(5e3);
            await assertion;
        });

        it("should time out after 60 seconds by default when the server never becomes ready", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            execAsyncSpy.mockReturnValue(new Promise(() => {}));
            const promise = ZSshClient.create(new SshSession(fakeSession));
            const assertion = expect(promise).rejects.toMatchObject({ errorCode: "ESERVERSTARTUPTIMEOUT" });
            await vi.advanceTimersByTimeAsync(60e3);
            await assertion;
        });

        it("should route post-connect errors to the onError handler", async () => {
            const { connectSpy } = setupMockSshClient();
            const onErrorMock = vi.fn();
            await ZSshClient.create(new SshSession(fakeSession), { onError: onErrorMock });
            const sshClient = connectSpy.mock.contexts[0] as Client;
            sshClient.emit("error", new Error("connection died"));
            expect(onErrorMock).toHaveBeenCalledTimes(1);
        });

        it("should reject pending requests when the connection closes", async () => {
            const sshStream = { stdin: { write: vi.fn() }, stdout: new EventEmitter(), stderr: new EventEmitter() };
            const { connectSpy } = setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            const response = client.request({ command: "ping" });
            const sshClient = connectSpy.mock.contexts[0] as Client;
            sshClient.emit("close");
            await expect(response).rejects.toMatchObject({ errorCode: "ECONNCLOSED" });
        });

        it("should not reject silenced requests when the connection closes", async () => {
            const sshStream = { stdin: { write: vi.fn() }, stdout: new EventEmitter(), stderr: new EventEmitter() };
            const { connectSpy } = setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            const rejectMock = vi.fn();
            (client as any).mRequestMap.set(99, {
                command: { command: "ping" },
                rpc: { resolve: vi.fn(), reject: rejectMock },
                silenced: true,
            });
            const sshClient = connectSpy.mock.contexts[0] as Client;
            sshClient.emit("close");
            await vi.waitFor(() => expect((client as any).mClosed).toBe(true));
            expect(rejectMock).not.toHaveBeenCalled();
        });

        it("should reject new requests after the connection is closed", async () => {
            const sshStream = { stdin: { write: vi.fn() }, stdout: new EventEmitter(), stderr: new EventEmitter() };
            const { connectSpy } = setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            const sshClient = connectSpy.mock.contexts[0] as Client;
            sshClient.emit("close");
            await vi.waitFor(() => expect((client as any).mClosed).toBe(true));
            await expect(client.request({ command: "ping" })).rejects.toMatchObject({ errorCode: "ECONNCLOSED" });
        });

        it.each(["close", "exit"])("should reject pending requests when the server channel emits %s", async (event) => {
            const sshStream = Object.assign(new EventEmitter(), {
                stdin: { write: vi.fn(), writable: true },
                stdout: new EventEmitter(),
                stderr: new EventEmitter(),
            });
            setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            const response = client.request({ command: "ping" });
            sshStream.emit(event);
            await expect(response).rejects.toMatchObject({ errorCode: "ECONNCLOSED" });
        });

        it("should invoke onClose only once when the channel and transport both close", async () => {
            const sshStream = Object.assign(new EventEmitter(), {
                stdin: { write: vi.fn(), writable: true },
                stdout: new EventEmitter(),
                stderr: new EventEmitter(),
            });
            const { connectSpy } = setupMockSshClient({ sshStream });
            const onCloseMock = vi.fn();
            await ZSshClient.create(new SshSession(fakeSession), { onClose: onCloseMock });
            sshStream.emit("close");
            (connectSpy.mock.contexts[0] as Client).emit("close");
            expect(onCloseMock).toHaveBeenCalledTimes(1);
        });

        it("should still invoke onClose when the client is disposed", async () => {
            const sshStream = Object.assign(new EventEmitter(), {
                stdin: { write: vi.fn(), writable: true },
                stdout: new EventEmitter(),
                stderr: new EventEmitter(),
            });
            const { connectSpy } = setupMockSshClient({ sshStream });
            const onCloseMock = vi.fn();
            const client = await ZSshClient.create(new SshSession(fakeSession), { onClose: onCloseMock });
            client.dispose();
            (connectSpy.mock.contexts[0] as Client).emit("close");
            expect(onCloseMock).toHaveBeenCalledTimes(1);
        });

        it("should not route SSH errors to onError once the client is closed", async () => {
            const { connectSpy } = setupMockSshClient();
            const onErrorMock = vi.fn();
            const client = await ZSshClient.create(new SshSession(fakeSession), { onError: onErrorMock });
            const sshClient = connectSpy.mock.contexts[0] as Client;

            client.dispose();
            sshClient.emit("error", new Error("socket torn down"));

            expect(onErrorMock).not.toHaveBeenCalled();
        });

        it("should reject requests when the channel is no longer writable", async () => {
            const sshStream = {
                stdin: { write: vi.fn(), writable: false },
                stdout: new EventEmitter(),
                stderr: new EventEmitter(),
            };
            setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            await expect(client.request({ command: "ping" })).rejects.toMatchObject({ errorCode: "ECONNCLOSED" });
            expect(sshStream.stdin.write).not.toHaveBeenCalled();
        });

        it("should censor sensitive information when trace logging a request", async () => {
            interface MySensitiveInfo extends CommandRequest {
                password: string;
                tokenValue: string;
                passphrase: string;
                aNonSensitiveArg: string;
            }
            const logTraceMock = vi.fn();
            vi.spyOn(Logger, "getAppLogger").mockReturnValue({
                debug: () => {},
                trace: logTraceMock,
            } as any);
            const request: MySensitiveInfo = {
                command: "test",
                password: "hunter2",
                tokenValue: "omgsecret0454654",
                passphrase: "letmein",
                aNonSensitiveArg: "do not censor me",
            };
            const sshStream = {
                stdin: { write: vi.fn(), writable: true },
                stdout: new EventEmitter(),
                stderr: new EventEmitter(),
            };

            setupMockSshClient({ sshStream });
            const client = await ZSshClient.create(new SshSession(fakeSession));
            const response = client.request(request);

            const responseUncensored: RpcResponse = {
                jsonrpc: "2.0",
                result: {
                    password: "secretlyAPassword",
                    tokenValue: "2898293839",
                    passphrase: "post-it",
                    okField: "puppy",
                },
                id: 1,
            };
            (client as any).processResponses(`${JSON.stringify(responseUncensored)}\n`);
            expect(await response).toEqual(responseUncensored.result);
            expect(sshStream.stdin.write).toHaveBeenCalled();
            const { command, ...rest } = request;

            const rpcRequest: RpcRequest = {
                jsonrpc: "2.0",
                method: command,
                params: rest,
                id: 1,
            };
            expect(logTraceMock).toHaveBeenCalledWith(
                `Sending request: ${JSON.stringify({
                    ...rpcRequest,
                    params: {
                        ...rpcRequest.params,
                        tokenValue: Censor.CENSOR_RESPONSE,
                        password: Censor.CENSOR_RESPONSE,
                        passphrase: Censor.CENSOR_RESPONSE,
                    },
                })}`,
            );
            expect(logTraceMock).toHaveBeenCalledWith(
                `Received response: ${JSON.stringify({
                    ...responseUncensored,
                    result: {
                        ...responseUncensored.result,
                        tokenValue: Censor.CENSOR_RESPONSE,
                        password: Censor.CENSOR_RESPONSE,
                        passphrase: Censor.CENSOR_RESPONSE,
                    },
                })}`,
            );
        });

        it("should respect numWorkers option", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            await ZSshClient.create(new SshSession(fakeSession), {
                numWorkers: 1,
            });
            expect(execAsyncSpy).toHaveBeenCalledTimes(1);
            expect(execAsyncSpy!.mock.calls[0].slice(1)).toEqual(["server", "--num-workers", "1"]);
        });

        it("should respect requestTimeout option", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            await ZSshClient.create(new SshSession(fakeSession), {
                requestTimeout: 300,
            });
            expect(execAsyncSpy).toHaveBeenCalledTimes(1);
            expect(execAsyncSpy!.mock.calls[0].slice(1)).toEqual(["server", "--request-timeout", "300"]);
        });

        it("should respect responseTimeout option", async () => {
            setupMockSshClient();
            const client = await ZSshClient.create(new SshSession(fakeSession), {
                responseTimeout: 300,
            });
            expect((client as any).mResponseTimeout).toBe(3e5);
        });

        it("should respect verbose option", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            await ZSshClient.create(new SshSession(fakeSession), {
                verbose: true,
            });
            expect(execAsyncSpy).toHaveBeenCalledTimes(1);
            expect(execAsyncSpy!.mock.calls[0].slice(1)).toEqual(["server", "--verbose"]);
        });

        it("should respect serverPath option", async () => {
            const { execAsyncSpy } = setupMockSshClient();
            await ZSshClient.create(new SshSession(fakeSession), {
                serverPath: "/tmp/zowe-server",
            });
            expect(execAsyncSpy).toHaveBeenCalledTimes(1);
            expect(execAsyncSpy!.mock.calls[0][0]).toBe("/tmp/zowe-server/zo");
        });

        it("should respect useNativeSsh option", async () => {
            setupMockSshClient();
            const createClientMock = vi.spyOn(sshRs, "createClient").mockReturnValue(new Client() as any);
            await ZSshClient.create(new SshSession(fakeSession), { useNativeSsh: true });
            expect(createClientMock).toHaveBeenCalledWith(true);
        });

        it("should start the server on an existing connection without connecting again", async () => {
            const { connectSpy, execAsyncSpy, sshStream } = setupMockSshClient();
            const createClientMock = vi.spyOn(sshRs, "createClient");
            createClientMock.mockClear();
            const existing = new Client();

            const client = await ZSshClient.create(new SshSession(fakeSession), {
                existingConnection: existing as any,
            });

            expect(client).toBeInstanceOf(ZSshClient);
            expect(connectSpy).not.toHaveBeenCalled();
            expect(createClientMock).not.toHaveBeenCalled();
            expect(execAsyncSpy).toHaveBeenCalledTimes(1);
            expect((client as any).mSshClient).toBe(existing);
            expect((client as any).mSshStream).toBe(sshStream);
        });

        it("should end the existing connection when the client is disposed", async () => {
            setupMockSshClient();
            const endSpy = vi.spyOn(Client.prototype, "end").mockImplementation(() => {});
            const client = await ZSshClient.create(new SshSession(fakeSession), {
                existingConnection: new Client() as any,
            });

            client.dispose();

            expect(endSpy).toHaveBeenCalledTimes(1);
        });

        it("should end the existing connection when startup fails", async () => {
            setupMockSshClient({ mockExecAsync: false });
            vi.spyOn(ZSshClient.prototype as any, "execAsync").mockRejectedValue(new Error("no server"));
            const endSpy = vi.spyOn(Client.prototype, "end").mockImplementation(() => {});

            await expect(
                ZSshClient.create(new SshSession(fakeSession), { existingConnection: new Client() as any }),
            ).rejects.toThrow("no server");

            expect(endSpy).toHaveBeenCalledTimes(1);
        });

        it("should invoke onClose when the existing connection closes", async () => {
            setupMockSshClient();
            const onClose = vi.fn();
            const existing = new Client();
            await ZSshClient.create(new SshSession(fakeSession), { existingConnection: existing as any, onClose });

            existing.emit("close");

            expect(onClose).toHaveBeenCalledTimes(1);
        });

        it("should ignore an existing connection when using the native client", async () => {
            const { connectSpy } = setupMockSshClient();
            const createClientMock = vi.spyOn(sshRs, "createClient").mockReturnValue(new Client() as any);
            const existing = new Client();

            const client = await ZSshClient.create(new SshSession(fakeSession), {
                existingConnection: existing as any,
                useNativeSsh: true,
            });

            expect(createClientMock).toHaveBeenCalledWith(true);
            expect(connectSpy).toHaveBeenCalledTimes(1);
            expect((client as any).mSshClient).not.toBe(existing);
        });

        it("should use the session agent without loading a private key or sending a password", async () => {
            const { connectSpy } = setupMockSshClient();
            vi.spyOn(ZSshUtils, "buildSshConfig").mockRestore();
            const session = ZSshUtils.buildSession({
                host: "example.com",
                user: "admin",
                identityAgent: "/tmp/ssh-agent.sock",
                privateKey: "/path/to/key",
                keyPassphrase: "key-passphrase",
                password: "saved-password",
            });

            await ZSshClient.create(session);

            expect(connectSpy).toHaveBeenCalledWith(
                expect.objectContaining({
                    agent: "/tmp/ssh-agent.sock",
                    privateKey: undefined,
                    passphrase: undefined,
                    password: undefined,
                }),
            );
        });
    });

    describe("dispose function", () => {
        it("should end SSH connection", () => {
            const endMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = { end: endMock };
            client.dispose();
            expect(endMock).toHaveBeenCalledTimes(1);
        });

        it("should end SSH connection 2", () => {
            const endMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = { end: endMock };
            client[Symbol.dispose]();
            expect(endMock).toHaveBeenCalledTimes(1);
        });
    });

    describe("request function", () => {
        it("should send request that succeeds", async () => {
            const request: CommandRequest = { command: "ping" };
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            const response = client.request(request);
            (client as any).processResponses(`${JSON.stringify(rpcResponseGood)}\n`);
            expect(await response).toEqual({ success: true });
            expect(writeMock.mock.calls[0]).toEqual([`${JSON.stringify(rpcRequest)}\n`]);
        });

        it("should send request that fails", async () => {
            const request: CommandRequest = { command: "ping" };
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            const response = client.request(request);
            (client as any).processResponses(`${JSON.stringify(rpcResponseBad)}\n`);
            await expect(response).rejects.toMatchObject({ message: rpcResponseBad.error?.message });
            expect(writeMock.mock.calls[0]).toEqual([`${JSON.stringify(rpcRequest)}\n`]);
        });

        it("should censor a JSON response that fails to parse in logging", async () => {
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = vi.fn();
            const censorRawDataMock = vi.spyOn(Censor, "censorRawData");
            const badJsonResponse = "This is not JSON password:ilovedogs";
            (client as any).processResponses(`${badJsonResponse}\n`);
            expect(censorRawDataMock).toHaveBeenCalledWith(badJsonResponse);
        });
        it("should surface a structured error payload via causeErrors and additionalDetails", async () => {
            const errorData = { success: false, safReturns: { safReturnCode: 8 } };
            const rpcResponseStructuredError: RpcResponse = {
                jsonrpc: "2.0",
                error: { code: 1, message: "Command execution failed", data: errorData },
                id: 1,
            };
            const request: CommandRequest = { command: "ping" };
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            const response = client.request(request);
            (client as any).processResponses(`${JSON.stringify(rpcResponseStructuredError)}\n`);
            await expect(response).rejects.toMatchObject({
                message: rpcResponseStructuredError.error?.message,
                causeErrors: errorData,
                additionalDetails: JSON.stringify(errorData),
            });
        });

        it("should keep a string error data as-is in additionalDetails", async () => {
            const rpcResponseStringError: RpcResponse = {
                jsonrpc: "2.0",
                error: { code: 1, message: "Command execution failed", data: "plain text detail" },
                id: 1,
            };
            const request: CommandRequest = { command: "ping" };
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            const response = client.request(request);
            (client as any).processResponses(`${JSON.stringify(rpcResponseStringError)}\n`);
            await expect(response).rejects.toMatchObject({
                causeErrors: "plain text detail",
                additionalDetails: "plain text detail",
            });
        });

        it("should send request that times out", async () => {
            const request: CommandRequest = { command: "ping" };
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            (client as any).mErrHandler = (_err: Error) => {};
            const response = client.request(request);
            vi.runAllTimers();
            await expect(response).rejects.toMatchObject({ errorCode: "ETIMEDOUT" });
            expect(writeMock.mock.calls[0]).toEqual([`${JSON.stringify(rpcRequest)}\n`]);
        });

        it("should skip empty response lines", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            const response = client.request(request);
            // Send response with empty lines
            fakeStdout.emit("data", `\n\n${JSON.stringify(rpcResponseGood)}\n`);

            expect(await response).toEqual({ success: true });
        });
    });

    describe("callbacks", () => {
        it("should receive ready message from Zowe server", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const onStderrMock = vi.spyOn(sshStream.stderr, "on");
            const onStdoutMock = vi.spyOn(sshStream.stdout, "on");
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    sshStream.stdout.emit("data", readyMessage);
                    return this;
                },
            };
            await (client as any).execAsync();
            expect(onStderrMock).toHaveBeenCalledTimes(2);
            expect(onStdoutMock).toHaveBeenCalledTimes(2);
            expect(client.serverVersion).not.toBeNull();
        });

        it("should reject execAsync when the stream closes before the server is ready", async () => {
            const sshStream = Object.assign(new EventEmitter(), {
                stderr: new EventEmitter(),
                stdout: new EventEmitter(),
            });
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    sshStream.emit("close");
                    return this;
                },
            };
            await expect((client as any).execAsync("zo", "server")).rejects.toMatchObject({
                errorCode: "ESERVEREXIT",
            });
        });

        it("should handle not found error from Zowe server", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            const execMock = vi.fn(function (command: string, callback: ClientCallback) {
                callback(undefined, sshStream as any);
                expect(command).toBe(`zo server || echo "${(ZSshClient as any).EXIT_CODE_MARKER}$?" >&2`);
                sshStream.stderr.emit("data", "sh: zo: not found\n");
                sshStream.stderr.emit("data", `${(ZSshClient as any).EXIT_CODE_MARKER}127\n`);
                return this;
            });
            (client as any).mSshClient = { exec: execMock };
            await expect((client as any).execAsync("zo", "server")).rejects.toMatchObject({ errorCode: "ENOTFOUND" });
            expect(execMock).toHaveBeenCalledTimes(1);
        });

        it("should handle startup error from Zowe server", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    // The exit code is echoed in a later chunk than the error text, so the
                    // failure is only definitive once it arrives.
                    sshStream.stderr.emit("data", "bad json");
                    sshStream.stderr.emit("data", `${(ZSshClient as any).EXIT_CODE_MARKER}1\n`);
                    return this;
                },
            };
            await expect((client as any).execAsync()).rejects.toThrow("Error starting Zowe server");
        });

        it("should collect a multi-line startup error before aborting", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    sshStream.stderr.emit("data", "first line of trouble\n");
                    sshStream.stderr.emit("data", "second line of trouble\n");
                    sshStream.stderr.emit("data", `${(ZSshClient as any).EXIT_CODE_MARKER}8\n`);
                    return this;
                },
            };
            // Every line is reported, and the marker itself is stripped from the details.
            await expect((client as any).execAsync("zo", "server")).rejects.toMatchObject({
                mDetails: expect.objectContaining({
                    additionalDetails: "first line of trouble\nsecond line of trouble",
                    msg: expect.stringContaining("(exit code 8)"),
                }),
            });
        });

        it("should not abort startup on unrecognized output that precedes the ready message", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = vi.fn();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    // A warning with no exit code means the server is still running, so startup
                    // must keep waiting rather than giving up on the first odd line.
                    sshStream.stderr.emit("data", "some unexpected warning\n");
                    sshStream.stdout.emit("data", readyMessage);
                    return this;
                },
            };
            await (client as any).execAsync();
            expect(client.serverVersion).not.toBeNull();
        });

        it("should handle a Language Environment load failure from Zowe server", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    sshStream.stderr.emit(
                        "data",
                        "CEE3561S The external function _ZNSt5__1_e13__hash_memoryEPKvm was not found in DLL CRTEQCXE.",
                    );
                    return this;
                },
            };
            await expect((client as any).execAsync()).rejects.toMatchObject({ errorCode: "ELERUNTIME" });
        });

        it("should detect a load failure split across multiple stderr chunks", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = vi.fn();
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    // z/OS can split the message across reads; neither half matches on its own.
                    sshStream.stderr.emit("data", "CEE3561S The external function _ZNSt5__1_e13__hash");
                    sshStream.stderr.emit("data", "_memoryEPKvm was not found in DLL CRTEQCXE.");
                    return this;
                },
            };
            await expect((client as any).execAsync()).rejects.toMatchObject({ errorCode: "ELERUNTIME" });
        });

        it("should process stderr data from Zowe server", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStderr = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: { on: vi.fn() }, stderr: fakeStderr };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");
            const response = client.request(request);
            fakeStderr.emit("data", `${JSON.stringify(rpcResponseBad)}\n`);
            await expect(response).rejects.toMatchObject({ message: rpcResponseBad.error?.message });
        });

        it("should process stdout data from Zowe server", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");
            const response = client.request(request);
            fakeStdout.emit("data", `${JSON.stringify(rpcResponseGood)}\n`);
            expect(await response).toEqual({ success: true });
        });

        it("should handle chdir error from Zowe server without throwing", async () => {
            const sshStream = { stderr: new EventEmitter(), stdout: new EventEmitter() };
            const onErrorMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = onErrorMock;
            (client as any).mSshClient = {
                exec: function (_command: string, callback: ClientCallback) {
                    callback(undefined, sshStream as any);
                    sshStream.stderr.emit("data", "FOTS1681 chdir error");
                    // Send ready message after the non-fatal error
                    sshStream.stdout.emit("data", readyMessage);
                    return this;
                },
            };

            await (client as any).execAsync();
            expect(onErrorMock).toHaveBeenCalledTimes(1);
            expect(onErrorMock.mock.calls[0][0]).toBeInstanceOf(Error);
        });

        it("should handle invalid response from Zowe server", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = (err: Error) => {
                if (!err.toString().includes("Request timed out")) throw err; // we're expecting a request timed out error for the handler
            };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");
            const response = client.request(request);
            expect(() => fakeStdout.emit("data", "bad json\n")).toThrow("Invalid JSON response");
            vi.runAllTimers();
            await expect(response).rejects.toBeDefined();
        });

        it("should handle unmapped response from Zowe server", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = (err: Error) => {
                if (!err.toString().includes("Request timed out")) throw err; // we're expecting a request timed out error for the handler
            };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");
            const response = client.request(request);
            expect(() => fakeStdout.emit("data", `${JSON.stringify({ ...rpcResponseGood, id: -1 })}\n`)).toThrow(
                "Missing promise for response ID",
            );
            vi.runAllTimers();
            await expect(response).rejects.toBeDefined();
        });

        it("should handle receiveStream and sendStream notifications and route them correctly", async () => {
            const request: CommandRequest = { command: "ping" };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            const linkStreamMock = vi.fn();
            (client as any).mStreamMgr = {
                linkStreamToPromise: linkStreamMock,
            };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            const responsePromise = client.request(request);

            // Emit a receiveStream notification
            const receiveStreamNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: { id: 1, pipePath: "/tmp/pipe" },
            };
            fakeStdout.emit("data", `${JSON.stringify(receiveStreamNotification)}\n`);
            expect(linkStreamMock).toHaveBeenLastCalledWith(expect.any(Object), receiveStreamNotification, "GET");

            // Emit a sendStream notification
            const sendStreamNotification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: { id: 1, pipePath: "/tmp/pipe" },
            };
            fakeStdout.emit("data", `${JSON.stringify(sendStreamNotification)}\n`);
            expect(linkStreamMock).toHaveBeenLastCalledWith(expect.any(Object), sendStreamNotification, "PUT");

            // Emit an unknown notification
            const unknownNotification = {
                jsonrpc: "2.0",
                method: "unknownStream",
                params: { id: 1, pipePath: "/tmp/pipe" },
            };
            const errHandlerMock = vi.fn();
            (client as any).mErrHandler = errHandlerMock;
            fakeStdout.emit("data", `${JSON.stringify(unknownNotification)}\n`);
            expect(errHandlerMock).toHaveBeenCalled();

            // Finally resolve the response to clean up
            fakeStdout.emit("data", `${JSON.stringify(rpcResponseGood)}\n`);
            await responsePromise;
        });

        it("should register a stream with a progressCallback but without contentLen", async () => {
            const streamFn = () => new EventEmitter();
            const request: CommandRequest = { command: "ping", stream: streamFn };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            const registerStreamMock = vi.fn();
            (client as any).mStreamMgr = {
                registerStream: registerStreamMock,
            };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            const progressCallback = vi.fn();
            const responsePromise = client.request(request, progressCallback);

            expect(registerStreamMock).toHaveBeenCalledWith(expect.any(Object), streamFn, expect.any(Object), {
                callback: progressCallback,
                totalBytes: undefined,
            });

            // Clean up
            fakeStdout.emit("data", `${JSON.stringify(rpcResponseGood)}\n`);
            await responsePromise;
        });

        it("should register a stream with a progressCallback and with contentLen", async () => {
            const streamFn = () => new EventEmitter();
            const request: CommandRequest = { command: "ping", stream: streamFn, contentLen: 42 } as any;
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            const registerStreamMock = vi.fn();
            (client as any).mStreamMgr = {
                registerStream: registerStreamMock,
            };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            const progressCallback = vi.fn();
            const responsePromise = client.request(request, progressCallback);

            expect(registerStreamMock).toHaveBeenCalledWith(expect.any(Object), streamFn, expect.any(Object), {
                callback: progressCallback,
                totalBytes: 42,
            });

            // Clean up
            fakeStdout.emit("data", `${JSON.stringify(rpcResponseGood)}\n`);
            await responsePromise;
        });

        it("should log message with default error handler", () => {
            const logErrorMock = vi.fn();
            const consoleErrorMock = vi.spyOn(console, "error").mockImplementation(() => {});
            vi.spyOn(Logger, "getAppLogger").mockReturnValue({
                error: logErrorMock,
            } as any);
            const testError = new Error("test error");
            (ZSshClient as any).defaultErrHandler(testError);
            expect(logErrorMock).toHaveBeenCalledTimes(1);
            expect(logErrorMock.mock.calls[0][0]).toBe(`Error: ${testError.message}`);
            expect(consoleErrorMock).toHaveBeenCalledTimes(1);
            consoleErrorMock.mockRestore();
        });
    });

    describe("quoteRemotePath function", () => {
        const quoteRemotePath = (path: string): string => (ZSshClient as any).quoteRemotePath(path);

        it("should leave a lone tilde unquoted so the shell expands it", () => {
            expect(quoteRemotePath("~")).toBe(`~''`);
        });

        it("should leave a leading tilde unquoted and quote the rest of the path", () => {
            expect(quoteRemotePath("~/some/path")).toBe("~/some/path");
        });

        it("should leave a leading tilde unquoted and quote a path containing spaces", () => {
            expect(quoteRemotePath("~/some path/with spaces")).toBe(`~'/some path/with spaces'`);
        });

        it("should not expand a tilde that is not at the start of the path", () => {
            expect(quoteRemotePath("/some/~user/path")).toBe(`'/some/~user/path'`);
        });

        it("should not expand a tilde used for a named user home directory", () => {
            expect(quoteRemotePath("~user/path")).toBe(`'~user/path'`);
        });

        it("should quote an absolute path containing spaces", () => {
            expect(quoteRemotePath("/u/users/some user/path")).toBe(`'/u/users/some user/path'`);
        });

        it("should not quote a path that does not require quoting", () => {
            expect(quoteRemotePath("/u/users/testuser/zowe-server")).toBe("/u/users/testuser/zowe-server");
        });
    });

    describe("uss.issueCmd", () => {
        it("should send unixCommand request and return command output", async () => {
            const writeMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };

            const response = client.uss.issueCmd({ commandText: "whoami" });
            const rpcResponse: RpcResponse = {
                jsonrpc: "2.0",
                result: { success: true, data: "admin" },
                id: 1,
            };
            (client as any).processResponses(`${JSON.stringify(rpcResponse)}\n`);

            const result = await response;
            expect(result.success).toBe(true);
            expect(result.data).toBe("admin");

            const sentRequest = JSON.parse(writeMock.mock.calls[0][0]);
            expect(sentRequest.method).toBe("unixCommand");
            expect(sentRequest.params.commandText).toBe("whoami");
        });
    });

    describe("request with stream", () => {
        it("should register stream function when request contains stream", async () => {
            const mockStream = new Readable({ read() {} });
            const streamFn = () => mockStream;
            const request: CommandRequest & ReadableStreamRpc = {
                command: "ping",
                stream: streamFn,
            };
            const writeMock = vi.fn();
            const registerStreamMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mSshStream = { stdin: { write: writeMock } };
            (client as any).mStreamMgr = { registerStream: registerStreamMock };

            const response = client.request(request);

            const mappedRequest = (client as any).mRequestMap.get(1);
            expect(mappedRequest).toBeDefined();
            expect(mappedRequest.command).toBe(request);

            (client as any).processResponses(`${JSON.stringify(rpcResponseGood)}\n`);
            await response;

            expect(registerStreamMock).toHaveBeenCalledTimes(1);
            expect(registerStreamMock.mock.calls[0][1]).toBe(streamFn);
        });
    });

    describe("notification handling", () => {
        it("should handle RPC notifications", async () => {
            const notification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: { id: 1, pipePath: "/dev/null" },
            };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const client: ZSshClient = new (ZSshClient as any)();
            const linkStreamToPromiseMock = vi.fn();
            (client as any).mStreamMgr = { linkStreamToPromise: linkStreamToPromiseMock };
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            (client as any).mRequestMap.set(1, {
                rpc: { resolve: vi.fn(), reject: vi.fn() },
                command: { command: "test" },
                silenced: false,
            });

            fakeStdout.emit("data", `${JSON.stringify(notification)}\n`);

            expect(linkStreamToPromiseMock).toHaveBeenCalledTimes(1);
            expect(linkStreamToPromiseMock.mock.calls[0][1]).toEqual(notification);
        });

        it("should handle errors in notification processing", async () => {
            const notification = {
                jsonrpc: "2.0",
                method: "invalidMethod",
                params: { id: 1 },
            };
            const fakeStdout = new EventEmitter();
            const sshStream = { stdin: { write: vi.fn() }, stdout: fakeStdout, stderr: { on: vi.fn() } };
            const onErrorMock = vi.fn();
            const client: ZSshClient = new (ZSshClient as any)();
            (client as any).mErrHandler = onErrorMock;
            (client as any).mSshStream = sshStream;
            (client as any).getServerStatus(sshStream, readyMessage, "zo server");

            (client as any).mRequestMap.set(1, {
                rpc: { resolve: vi.fn(), reject: vi.fn() },
                command: { command: "test" },
                silenced: false,
            });

            fakeStdout.emit("data", `${JSON.stringify(notification)}\n`);

            expect(onErrorMock).toHaveBeenCalledTimes(1);
            expect(onErrorMock.mock.calls[0][0].message).toContain("unknown method invalidMethod");
        });
    });

    describe("collectAllRequests function", () => {
        it("should harvest requests, apply silence flag, and clear timeouts", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const clearTimeoutSpy = vi.spyOn(globalThis, "clearTimeout");

            const timeout1 = setTimeout(() => {}, 10000);
            const timeout2 = setTimeout(() => {}, 10000);

            const req1 = {
                command: { command: "ping" },
                rpc: { resolve: vi.fn(), reject: vi.fn() },
                silenced: false,
                timeoutId: timeout1,
            };
            const req2 = {
                command: { command: "echo" },
                rpc: { resolve: vi.fn(), reject: vi.fn() },
                silenced: false,
                timeoutId: timeout2,
            };

            (client as any).mRequestMap.set(1, req1);
            (client as any).mRequestMap.set(2, req2);

            const harvested = client.collectAllRequests(true);
            expect(harvested.size).toBe(2);
            expect(harvested.has(req1 as any)).toBe(true);
            expect(req1.silenced).toBe(true);
            expect(req2.silenced).toBe(true);
            expect(clearTimeoutSpy).toHaveBeenCalledWith(timeout1);
            expect(clearTimeoutSpy).toHaveBeenCalledWith(timeout2);
        });
    });

    describe("dispose function", () => {
        it("should reject open requests with standard message on normal dispose", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const rejectMock = vi.fn();
            (client as any).mSshClient = { end: vi.fn() };
            (client as any).mRequestMap.set(1, { rpc: { resolve: vi.fn(), reject: rejectMock } });

            // Default dispose: isRestart = false, closeOpenRequests = true
            client.dispose();

            expect(rejectMock).toHaveBeenCalledTimes(1);
            expect(rejectMock.mock.calls[0][0].message).toContain("Shutting down ZRS. No action is required.");
            expect((client as any).mRequestMap.size).toBe(0);
        });

        it("should reject open requests with restart message when isRestart is true", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const rejectMock = vi.fn();
            (client as any).mSshClient = { end: vi.fn() };
            (client as any).mRequestMap.set(1, { rpc: { resolve: vi.fn(), reject: rejectMock } });

            // Restarting, not retrying requests
            client.dispose(true);

            expect(rejectMock).toHaveBeenCalledTimes(1);
            expect(rejectMock.mock.calls[0][0].message).toContain("Restarting ZRS");
            expect((client as any).mRequestMap.size).toBe(0);
        });

        it("should NOT reject open requests when requests are silenced", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const rejectMock = vi.fn();
            (client as any).mSshClient = { end: vi.fn() };
            (client as any).mRequestMap.set(1, { rpc: { resolve: vi.fn(), reject: rejectMock }, silenced: true });

            client.dispose(true);

            expect(rejectMock).not.toHaveBeenCalled();
            expect((client as any).mRequestMap.size).toBe(0);
        });

        it("should end SSH connection", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const endMock = vi.fn();
            (client as any).mSshClient = { end: endMock };
            client[Symbol.dispose]();
            expect(endMock).toHaveBeenCalledTimes(1);
        });
    });

    describe("handleResponse with silenced requests", () => {
        it("should silently delete the request and ignore the response if silenced is true", () => {
            const client: ZSshClient = new (ZSshClient as any)();
            const resolveMock = vi.fn();
            const rejectMock = vi.fn();

            // Seed a silenced request
            (client as any).mRequestMap.set(99, {
                command: { command: "ping" },
                rpc: { resolve: resolveMock, reject: rejectMock },
                silenced: true,
            });

            const fakeResponse: RpcResponse = { jsonrpc: "2.0", id: 99, result: { success: true } };

            (client as any).handleResponse(fakeResponse);

            // Promise handlers should NOT have been called
            expect(resolveMock).not.toHaveBeenCalled();
            expect(rejectMock).not.toHaveBeenCalled();

            expect((client as any).mRequestMap.has(99)).toBe(false);
        });
    });

    describe("create function (Replay Requests)", () => {
        it("should automatically replay harvested requests upon creation", async () => {
            const fakeResolve = vi.fn();
            const fakeReject = vi.fn();
            const harvestedRequest = {
                command: { command: "harvested-command" },
                rpc: { resolve: fakeResolve, reject: fakeReject },
                silenced: true,
            };

            const requestSpy = vi.spyOn(ZSshClient.prototype, "request").mockResolvedValue({ success: true } as any);

            setupMockSshClient();

            await ZSshClient.create(new SshSession(fakeSession), {
                requests: new Set([harvestedRequest] as any),
            });

            expect(requestSpy).toHaveBeenCalledWith(harvestedRequest.command);

            expect(fakeResolve).toHaveBeenCalledWith({ success: true });
            expect(fakeReject).not.toHaveBeenCalled();
        });
    });
});

describe("ZSshUtils", () => {
    describe("uninstall server", () => {
        const serverPath = "/faketmp/fakeserver";
        const fakeSshSession = new SshSession(fakeSession);
        it("should return code 0 when uninstalling server succeeds", async () => {
            const fakeLogger = { debug: vi.fn(), error: vi.fn() };
            vi.spyOn(Logger, "getAppLogger").mockReturnValue(fakeLogger as any);
            vi.spyOn(ZSshUtils as any, "sftp").mockImplementation(
                async (_session: any, callback: (sftp: any, ssh: any) => Promise<void>) => {
                    const sshMock = {
                        execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "deleted", stderr: "" }),
                    };
                    return callback({}, sshMock);
                },
            );
            await ZSshUtils.uninstallServer(fakeSshSession, serverPath);
            expect(fakeLogger.debug).toHaveBeenCalledWith(`Deleted directory ${serverPath} with response: deleted`);
            expect(fakeLogger.error).not.toHaveBeenCalled();
        });

        it("should return code 1 when uninstalling server fails", async () => {
            const fakeLogger = { debug: vi.fn(), error: vi.fn() };
            vi.spyOn(Logger, "getAppLogger").mockReturnValue(fakeLogger as any);
            vi.spyOn(ZSshUtils as any, "sftp").mockImplementation(
                async (_session: any, callback: (sftp: any, ssh: any) => Promise<void>) => {
                    const sshMock = {
                        execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: "" }),
                    };
                    return callback({}, sshMock);
                },
            );

            await expect(ZSshUtils.uninstallServer(fakeSshSession, serverPath)).rejects.toThrow();
            expect(fakeLogger.error).toHaveBeenCalled();
        });
    });

    describe("isPrivateKeyAuthFailure", () => {
        it("should return true for common private key failure patterns", () => {
            const testCases = [
                "All configured authentication methods failed",
                "Cannot parse privateKey: Malformed OpenSSH private key",
                "but no passphrase given",
                "integrity check failed",
                "Permission denied (publickey,password)",
                "Permission denied (publickey)",
                "Authentication failed",
                "Invalid private key",
                "privateKey value does not contain a (valid) private key",
                "Cannot parse privateKey",
            ];

            testCases.forEach((errorMessage) => {
                expect(ZSshUtils.isPrivateKeyAuthFailure(errorMessage, true)).toBe(true);
            });
        });

        it("should return false for non-private key error messages", () => {
            const testCases = [
                "Connection timed out",
                "Network is unreachable",
                "Connection refused",
                "Host key verification failed",
                "Some other random error",
            ];

            testCases.forEach((errorMessage) => {
                expect(ZSshUtils.isPrivateKeyAuthFailure(errorMessage, true)).toBe(false);
            });
        });

        it("should return false when hasPrivateKey is explicitly false", () => {
            const privateKeyErrorMessage = "All configured authentication methods failed";
            expect(ZSshUtils.isPrivateKeyAuthFailure(privateKeyErrorMessage, false)).toBe(false);
        });

        it("should return true when hasPrivateKey is undefined but error matches pattern", () => {
            const privateKeyErrorMessage = "All configured authentication methods failed";
            expect(ZSshUtils.isPrivateKeyAuthFailure(privateKeyErrorMessage)).toBe(true);
        });

        it("should handle partial matches in error messages", () => {
            const errorMessage =
                "SSH Error: All configured authentication methods failed. Please check your credentials.";
            expect(ZSshUtils.isPrivateKeyAuthFailure(errorMessage, true)).toBe(true);
        });
    });
});
