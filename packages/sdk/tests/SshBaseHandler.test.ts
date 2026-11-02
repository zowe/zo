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
import * as fs from "node:fs";
import type { IHandlerParameters } from "@zowe/imperative";
import { SshKnownHosts, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import type { ConnectConfig, HostVerifier } from "ssh2";
import { SshBaseHandler } from "../../cli/src/SshBaseHandler";
import { SshAuthenticationError, ZSshClient, ZSshUtils } from "../src";
import * as sshRs from "../src/ssh-rs";

vi.mock("@zowe/zowex-for-zowe-sdk", () => import("../src"));
vi.mock("node:fs", { spy: true });

class TestHandler extends SshBaseHandler {
    public processWithClient = vi.fn().mockResolvedValue({ success: true });
}

describe("SshBaseHandler password fallback", () => {
    const key = Buffer.from("server-public-key");
    const authFailure = new SshAuthenticationError(new Error("All configured authentication methods failed"));
    const createClient = ZSshClient.create;
    let handler: TestHandler;
    let params: IHandlerParameters;

    beforeEach(() => {
        handler = new TestHandler();
        params = {
            arguments: { host: "example.com", user: "testuser", privateKey: "/test/key" },
            response: {
                console: { log: vi.fn(), error: vi.fn(), prompt: vi.fn().mockResolvedValue("password") },
                progress: { endBar: vi.fn() },
                data: { setObj: vi.fn() },
            },
        } as unknown as IHandlerParameters;
        vi.spyOn(fs, "readFileSync").mockReturnValue("test-private-key");
        vi.spyOn(SshKnownHosts, "verify").mockReturnValue({ status: "trusted" });
        vi.spyOn(ZSshClient, "create").mockImplementation(async (session) => {
            const config = ZSshUtils.buildSshConfig(session);
            const trusted = await new Promise<boolean>((resolve) =>
                (config.hostVerifier as HostVerifier)(key, resolve),
            );
            if (!trusted || session.ISshSession.privateKey) {
                throw authFailure;
            }
            return { [Symbol.dispose]: vi.fn() } as unknown as ZSshClient;
        });
    });

    it("does not prompt on authentication-shaped errors before host verification", async () => {
        vi.mocked(ZSshClient.create).mockRejectedValue(authFailure);
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).not.toHaveBeenCalled();
        expect(ZSshClient.create).toHaveBeenCalledTimes(1);
    });

    it.each([
        { handshake: false, level: undefined },
        { handshake: false, level: "client-authentication" },
        { handshake: true, level: undefined },
        { handshake: true, level: "handshake" },
    ])("does not prompt on a disconnect with handshake=$handshake and level=$level", async ({ handshake, level }) => {
        const transport = Object.assign(new EventEmitter(), {
            connect: vi.fn((config: ConnectConfig) => {
                (config.hostVerifier as HostVerifier)(key, (trusted) => {
                    expect(trusted).toBe(true);
                    if (handshake) {
                        transport.emit("handshake");
                    }
                    transport.emit(
                        "error",
                        Object.assign(new Error("All configured authentication methods failed"), { level }),
                    );
                });
            }),
            exec: vi.fn(),
            end: vi.fn(),
        });
        vi.spyOn(sshRs, "createClient").mockReturnValue(transport);
        vi.mocked(ZSshClient.create).mockImplementationOnce(createClient);

        await expect(handler.process(params)).rejects.toThrow("All configured authentication methods failed");
        expect(params.response.console.prompt).not.toHaveBeenCalled();
        expect(ZSshClient.create).toHaveBeenCalledTimes(1);
        expect(transport.end).toHaveBeenCalledTimes(1);
    });

    it("offers fallback for a transport authentication error only after the trusted handshake completes", async () => {
        const transport = Object.assign(new EventEmitter(), {
            connect: vi.fn((config: ConnectConfig) => {
                (config.hostVerifier as HostVerifier)(key, (trusted) => {
                    expect(trusted).toBe(true);
                    transport.emit("handshake");
                    transport.emit(
                        "error",
                        Object.assign(new Error("All configured authentication methods failed"), {
                            level: "client-authentication",
                        }),
                    );
                });
            }),
            exec: vi.fn(),
            end: vi.fn(),
        });
        vi.spyOn(sshRs, "createClient").mockReturnValue(transport);
        vi.mocked(ZSshClient.create).mockImplementationOnce(createClient);

        await handler.process(params);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
        expect(ZSshClient.create).toHaveBeenCalledTimes(3);
        expect(handler.processWithClient).toHaveBeenCalledTimes(1);
    });

    it.each(["unknown", "changed", "revoked"] as const)("does not prompt for %s server keys", async (status) => {
        vi.mocked(SshKnownHosts.verify).mockReturnValue({ status });
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).not.toHaveBeenCalled();
        expect(ZSshClient.create).toHaveBeenCalledTimes(1);
    });

    it("does not downgrade when verification is explicitly disabled", async () => {
        params.arguments.insecure = true;
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).not.toHaveBeenCalled();
    });

    it("offers password authentication only after validating the server key", async () => {
        await handler.process(params);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
        expect(ZSshClient.create).toHaveBeenCalledTimes(3);
        expect(handler.processWithClient).toHaveBeenCalledTimes(1);
        expect(params.response.data.setObj).toHaveBeenCalledWith({ success: true });
    });

    it("preserves pins and runtime trust hooks when creating password sessions", async () => {
        const session = new SshSession({
            hostname: "example.com",
            user: "testuser",
            privateKey: "/test/key",
            hostKey: key.toString("base64"),
        });
        session.hostKeyVerifier = vi.fn().mockResolvedValue(false);
        vi.spyOn(ZSshUtils, "buildSession").mockReturnValue(session);
        await handler.process(params);
        for (const [passwordSession] of vi.mocked(ZSshClient.create).mock.calls.slice(1)) {
            expect(passwordSession.ISshSession.hostKey).toBe(key.toString("base64"));
            expect(passwordSession.hostKeyVerifier).toBe(session.hostKeyVerifier);
            expect(passwordSession.ISshSession.privateKey).toBeUndefined();
            expect(passwordSession.ISshSession.identityAgent).toBeUndefined();
        }
    });

    it("stops password retries if host validation fails on the next connection", async () => {
        vi.mocked(SshKnownHosts.verify)
            .mockReturnValueOnce({ status: "trusted" })
            .mockReturnValue({ status: "changed" });
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
        expect(ZSshClient.create).toHaveBeenCalledTimes(2);
    });

    it("does not repeat the password prompt for an unclassified error after accepting the key", async () => {
        const disconnect = new Error("Permission denied (publickey)");
        const defaultCreate = vi.mocked(ZSshClient.create).getMockImplementation()!;
        vi.mocked(ZSshClient.create)
            .mockImplementationOnce(defaultCreate)
            .mockImplementationOnce(async (session) => {
                await defaultCreate(session);
                throw disconnect;
            });

        await expect(handler.process(params)).rejects.toBe(disconnect);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
        expect(ZSshClient.create).toHaveBeenCalledTimes(2);
    });

    it("does not offer fallback again if the password connection's host key changes", async () => {
        vi.mocked(SshKnownHosts.verify)
            .mockReturnValueOnce({ status: "trusted" })
            .mockReturnValueOnce({ status: "trusted" })
            .mockReturnValue({ status: "changed" });
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
        expect(ZSshClient.create).toHaveBeenCalledTimes(3);
    });

    it("does not offer fallback again when the user cancels", async () => {
        vi.mocked(params.response.console.prompt).mockResolvedValue("");
        await expect(handler.process(params)).rejects.toBe(authFailure);
        expect(params.response.console.prompt).toHaveBeenCalledTimes(1);
    });
});
