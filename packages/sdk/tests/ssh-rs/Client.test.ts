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

import { KeyPair, SSHClient, SshTransport } from "russh";
import { Subject } from "rxjs";
import { beforeEach, describe, expect, it, vi } from "vitest";
import { Client } from "../../src/ssh-rs/Client";
import { ClientChannel } from "../../src/ssh-rs/ClientChannel";

vi.mock("russh", () => {
    return {
        SSHClient: {
            connect: vi.fn(),
        },
        SshTransport: {
            newSocket: vi.fn(),
        },
        KeyPair: {
            parse: vi.fn(),
        },
    };
});

describe("Client", () => {
    let mockTransport: any;
    let mockUnauthClient: any;
    let mockAuthClient: any;

    beforeEach(() => {
        vi.clearAllMocks();

        mockTransport = {};
        vi.spyOn(SshTransport, "newSocket").mockResolvedValue(mockTransport);

        mockUnauthClient = {
            disconnect$: new Subject<void>(),
            disconnect: vi.fn().mockResolvedValue(undefined),
            authenticateWithKeyPair: vi.fn(),
            authenticateWithPassword: vi.fn(),
            authenticateWithAgent: vi.fn(),
        };
        vi.spyOn(SSHClient, "connect").mockResolvedValue(mockUnauthClient);

        mockAuthClient = {
            disconnect: vi.fn().mockResolvedValue(undefined),
            openSessionChannel: vi.fn().mockResolvedValue({ id: 1 }),
            activateChannel: vi.fn().mockResolvedValue({
                id: 1,
                requestExec: vi.fn().mockResolvedValue(undefined),
                data$: new Subject(),
                extendedData$: new Subject(),
                eof$: new Subject(),
                closed$: new Subject(),
            }),
        };
    });

    const createAndConnectClient = async (options?: any) => {
        const client = new Client();
        mockUnauthClient.authenticateWithPassword.mockResolvedValue(mockAuthClient);
        await new Promise<void>((resolve) => {
            client.on("ready", resolve);
            client.connect({ password: "pwd", ...options });
        });
        return client;
    };

    it("should throw error if attempting to connect a closed client", () => {
        const client = new Client();
        (client as any).closed = true;
        expect(() => client.connect({})).toThrow("Cannot reuse a closed SSH client — create a new instance");
    });

    it("should connect using password authentication successfully", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithPassword.mockResolvedValue(mockAuthClient);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        const debugMock = vi.fn();
        client.connect({
            host: "test-host",
            port: 22,
            username: "user",
            password: "pwd",
            debug: debugMock,
            readyTimeout: 15000,
            keepaliveInterval: 5000,
        });

        await readyPromise;
        expect(SshTransport.newSocket).toHaveBeenCalledWith("test-host:22");
        expect(SSHClient.connect).toHaveBeenCalled();
        expect(mockUnauthClient.authenticateWithPassword).toHaveBeenCalledWith("user", "pwd");

        // Execute and verify the host key check callback for full coverage
        const connectCall = vi.mocked(SSHClient.connect).mock.calls[0];
        const verifyCallback = connectCall[1];
        const mockKey = {
            algorithm: () => "ssh-ed25519",
            fingerprint: () => "SHA256:fingerprint_data",
        };
        const result = await verifyCallback(mockKey as any);
        expect(result).toBe(true);
        expect(debugMock).toHaveBeenCalledWith("russh: server key ssh-ed25519 fingerprint=SHA256:fingerprint_data");
    });

    it("should connect using privateKey successfully (string and buffer, with passphrase)", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithKeyPair.mockResolvedValue(mockAuthClient);
        const mockKeyPair = {};
        vi.spyOn(KeyPair, "parse").mockResolvedValue(mockKeyPair as any);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        client.connect({
            host: "test-host",
            username: "user",
            privateKey: "private-key-string",
            passphrase: "kp",
        });

        await readyPromise;
        expect(KeyPair.parse).toHaveBeenCalledWith("private-key-string", "kp");
        expect(mockUnauthClient.authenticateWithKeyPair).toHaveBeenCalledWith("user", mockKeyPair, null);

        // Test with privateKey as Buffer
        const clientBuf = new Client();
        const readyPromiseBuf = new Promise<void>((resolve) => {
            clientBuf.on("ready", resolve);
        });

        clientBuf.connect({
            privateKey: Buffer.from("private-key-buf"),
        });

        await readyPromiseBuf;
        expect(KeyPair.parse).toHaveBeenLastCalledWith("private-key-buf", undefined);
    });

    it("should handle error during connection handshake", async () => {
        const client = new Client();
        const testErr = new Error("handshake failed");
        vi.spyOn(SshTransport, "newSocket").mockRejectedValue(testErr);

        const errorPromise = new Promise<any>((resolve) => {
            client.on("error", resolve);
        });

        client.connect({});

        const err = await errorPromise;
        expect(err).toBe(testErr);
    });

    it("should pass keepaliveCountMax to SSHClient.connect", async () => {
        await createAndConnectClient({ keepaliveInterval: 5000, keepaliveCountMax: 5 });
        const configArg = vi.mocked(SSHClient.connect).mock.calls[0][2];
        expect(configArg).toMatchObject({ keepaliveIntervalSeconds: 5, keepaliveCountMax: 5 });
    });

    it("should emit error when the TCP connection times out", async () => {
        const client = new Client();
        vi.mocked(SshTransport.newSocket).mockReturnValue(new Promise(() => {}));

        const errorPromise = new Promise<any>((resolve) => {
            client.on("error", resolve);
        });

        client.connect({ host: "test-host", readyTimeout: 25 });

        const err = await errorPromise;
        expect(err.message).toContain("Timed out opening TCP connection to test-host:22");
    });

    it("should handle authentication failure and clean up", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithPassword.mockResolvedValue({
            remainingMethods: ["keyboard-interactive"],
        }); // AuthFailure format

        const errorPromise = new Promise<any>((resolve) => {
            client.on("error", resolve);
        });

        client.connect({
            password: "pwd",
        });

        const err = await errorPromise;
        expect(err.message).toContain("Password authentication failed. Remaining methods: keyboard-interactive");
        expect(mockUnauthClient.disconnect).toHaveBeenCalled();
    });

    it("should handle missing authentication credentials", async () => {
        const client = new Client();

        const errorPromise = new Promise<any>((resolve) => {
            client.on("error", resolve);
        });

        client.connect({});

        const err = await errorPromise;
        expect(err.message).toContain("No authentication method available: provide agent, privateKey, or password");
    });

    it("should connect using a unix-socket agent successfully", async () => {
        // resolveAgentSpec branches on process.platform, so pin it to a POSIX value here —
        // otherwise this test would fail whenever it actually runs on Windows.
        const platformSpy = vi.spyOn(process, "platform", "get").mockReturnValue("linux");
        const client = new Client();
        mockUnauthClient.authenticateWithAgent.mockResolvedValue(mockAuthClient);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        client.connect({
            username: "user",
            agent: "/tmp/ssh-agent.sock",
        });

        await readyPromise;
        expect(mockUnauthClient.authenticateWithAgent).toHaveBeenCalledWith("user", {
            kind: "unix-socket",
            path: "/tmp/ssh-agent.sock",
        });
        platformSpy.mockRestore();
    });

    it("should connect using pageant agent successfully", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithAgent.mockResolvedValue(mockAuthClient);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        client.connect({ agent: "pageant" });

        await readyPromise;
        expect(mockUnauthClient.authenticateWithAgent).toHaveBeenCalledWith("root", { kind: "pageant" });
    });

    it("should resolve a non-pageant agent string as a named pipe on Windows", async () => {
        const platformSpy = vi.spyOn(process, "platform", "get").mockReturnValue("win32");
        const client = new Client();
        mockUnauthClient.authenticateWithAgent.mockResolvedValue(mockAuthClient);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        client.connect({ agent: "\\\\.\\pipe\\openssh-ssh-agent" });

        await readyPromise;
        expect(mockUnauthClient.authenticateWithAgent).toHaveBeenCalledWith("root", {
            kind: "named-pipe",
            path: "\\\\.\\pipe\\openssh-ssh-agent",
        });
        platformSpy.mockRestore();
    });

    it("should prefer agent authentication over privateKey and password when configured", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithAgent.mockResolvedValue(mockAuthClient);

        const readyPromise = new Promise<void>((resolve) => {
            client.on("ready", resolve);
        });

        client.connect({
            agent: "/tmp/ssh-agent.sock",
            privateKey: "should-be-ignored",
            password: "should-be-ignored",
        });

        await readyPromise;
        expect(mockUnauthClient.authenticateWithAgent).toHaveBeenCalled();
        expect(mockUnauthClient.authenticateWithKeyPair).not.toHaveBeenCalled();
        expect(mockUnauthClient.authenticateWithPassword).not.toHaveBeenCalled();
    });

    it("should handle agent authentication failure and clean up", async () => {
        const client = new Client();
        mockUnauthClient.authenticateWithAgent.mockResolvedValue({
            remainingMethods: ["password"],
        }); // AuthFailure format

        const errorPromise = new Promise<any>((resolve) => {
            client.on("error", resolve);
        });

        client.connect({ agent: "/tmp/ssh-agent.sock" });

        const err = await errorPromise;
        expect(err.message).toContain("Agent authentication failed. Remaining methods: password");
        expect(mockUnauthClient.disconnect).toHaveBeenCalled();
    });

    it("should handle exec commands correctly when connected", async () => {
        const client = await createAndConnectClient();

        // Test exec
        const execPromise = new Promise<ClientChannel>((resolve, reject) => {
            client.exec("whoami", (err, ch) => (err ? reject(err) : resolve(ch)));
        });

        const channel = await execPromise;
        expect(channel).toBeInstanceOf(ClientChannel);
        expect(mockAuthClient.openSessionChannel).toHaveBeenCalled();
        expect(mockAuthClient.activateChannel).toHaveBeenCalled();
    });

    it("should handle exec commands error when not connected", () => {
        const client = new Client();
        client.exec("whoami", (err, ch) => {
            expect(err.message).toBe("Not connected");
            expect(ch).toBeUndefined();
        });
    });

    it("should handle exec errors in connect client", async () => {
        const client = await createAndConnectClient();

        const testErr = new Error("exec fail");
        mockAuthClient.activateChannel.mockRejectedValueOnce(testErr);

        await new Promise<void>((resolve) => {
            client.exec("whoami", (err, ch) => {
                expect(err).toBe(testErr);
                expect(ch).toBeUndefined();
                resolve();
            });
        });
    });

    it("should disconnect and emit close on end()", async () => {
        const client = await createAndConnectClient();

        const closePromise = new Promise<void>((resolve) => {
            client.on("close", resolve);
        });

        client.end();
        await closePromise;

        expect(mockAuthClient.disconnect).toHaveBeenCalled();

        // Directly call emitClose a second time to cover the early return "if (this.closed) return;" branch
        const emitCloseSpy = vi.spyOn(client as any, "emitClose");
        (client as any).emitClose();
        expect(emitCloseSpy).toHaveReturned();
    });

    it("should disconnect and emit close on disconnect$ event from server", async () => {
        const client = await createAndConnectClient();

        const closePromise = new Promise<void>((resolve) => {
            client.on("close", resolve);
        });

        // Emit disconnect from server
        mockUnauthClient.disconnect$.next();
        await closePromise;
    });
});
