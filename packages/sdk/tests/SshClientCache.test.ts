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
import { PassThrough } from "node:stream";
import { DeferredPromise, ImperativeError, type ProfileInfo } from "@zowe/imperative";
import { SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import { NodeSSH } from "node-ssh";
import type { SFTPWrapper } from "ssh2";
import { Client } from "ssh2";
import type { MockInstance } from "vitest";
import { AbstractConfigManager } from "../src/AbstractConfigManager";
import type { ProgressCallback } from "../src/doc";
import { SshClientCache } from "../src/SshClientCache";
import { SshConfigUtils } from "../src/SshConfigUtils";
import * as sshRs from "../src/ssh-rs";
import { ZSshClient } from "../src/ZSshClient";
import { ZSshUtils } from "../src/ZSshUtils";

function session(overrides = {}): SshSession {
    return new SshSession({ hostname: "example.com", user: "admin", password: "otp", ...overrides });
}

function serverChannel() {
    const channel = Object.assign(new EventEmitter(), {
        stdin: new PassThrough(),
        stdout: new PassThrough(),
        stderr: new PassThrough(),
        close: vi.fn(() => channel.emit("close")),
    });
    return channel;
}

function testProfileInfo(): ProfileInfo {
    return {
        getAllProfiles: () => [{ profName: "ssh" }],
        mergeArgsForProfile: () => ({
            knownArgs: [
                { argName: "host", argValue: "example.com" },
                { argName: "user", argValue: "admin" },
                { argName: "password", argValue: "otp" },
            ],
        }),
    } as unknown as ProfileInfo;
}

class TestPrompt extends AbstractConfigManager {
    protected showMessage(): void {}
    protected async showInputBox(): Promise<undefined> {
        return undefined;
    }
    protected async withProgress<T>(_message: string, task: (progress: ProgressCallback) => Promise<T>): Promise<T> {
        return task(() => {});
    }
    protected async showMenu(): Promise<string> {
        return "ssh";
    }
    protected async showCustomMenu(): Promise<undefined> {
        return undefined;
    }
    protected getCurrentDir(): undefined {
        return undefined;
    }
    protected getProfileSchemas(): [] {
        return [];
    }
    protected async showPrivateKeyWarning(): Promise<boolean> {
        return false;
    }
    protected getClientSetting<T>(): T | undefined {
        return undefined;
    }
    protected showStatusBar(): undefined {
        return undefined;
    }
    protected storeServerPath(): void {}
}

describe("SshClientCache", () => {
    const cache = SshClientCache.inst;
    let connect: MockInstance<NodeSSH["connect"]>;
    let dispose: MockInstance<NodeSSH["dispose"]>;

    beforeEach(() => {
        vi.useFakeTimers();
        connect = vi.spyOn(NodeSSH.prototype, "connect").mockImplementation(async function () {
            this.connection = new Client();
            return this;
        });
        dispose = vi.spyOn(NodeSSH.prototype, "dispose").mockImplementation(function () {
            const connection = this.connection;
            this.connection = null;
            connection?.emit("close");
        });
        vi.spyOn(ZSshUtils, "checkIfOutdated").mockReturnValue(false);
    });

    afterEach(() => {
        cache.dispose();
        vi.useRealTimers();
    });

    it("reuses equivalent sessions and coalesces concurrent authentication", async () => {
        const gate = new DeferredPromise<void>();
        connect.mockImplementation(async function () {
            await gate.promise;
            this.connection = new Client();
            return this;
        });
        const first = cache.acquire(session());
        const second = cache.acquire(session({ port: 22 }));
        expect(connect).toHaveBeenCalledOnce();
        let ready = false;
        void second.then(() => {
            ready = true;
        });
        await Promise.resolve();
        expect(ready).toBe(false);
        gate.resolve();
        const leases = await Promise.all([first, second]);
        expect(leases[0].ssh).toBe(leases[1].ssh);
        for (const lease of leases) lease[Symbol.dispose]();
    });

    it.each([{ hostname: "other.example.com" }, { port: 2222 }, { user: "other" }])(
        "separates connection endpoints: %j",
        (overrides) => {
            expect(cache.getKey(session(overrides))).not.toBe(cache.getKey(session()));
            expect(cache.getKey(session())).not.toContain("otp");
        },
    );

    it("uses readable URI keys with server paths and excludes authentication secrets", () => {
        expect(cache.getKey(session(), "/opt/zo")).toBe("ssh://admin@example.com:22/opt/zo");
        expect(cache.getKey(session({ hostname: "::1", port: 2222 }), "/opt/zo")).toBe("ssh://admin@[::1]:2222/opt/zo");
        expect(cache.getKey(session(), "/other/zo")).not.toBe(cache.getKey(session(), "/opt/zo"));
        expect(cache.getKey(session({ password: "updated", privateKey: "/key" }))).toBe(cache.getKey(session()));
    });

    it("matches agent validation with a profile containing unused fallback credentials", () => {
        const validated = session({ identityAgent: "/agent", privateKey: undefined, password: undefined });
        const profile = session({ identityAgent: "/agent", privateKey: "/key", keyPassphrase: "unused" });
        expect(cache.getKey(validated)).toBe(cache.getKey(profile));
    });

    it("expires ten minutes after the last user releases the transport", async () => {
        const first = await cache.acquire(session());
        const second = await cache.acquire(session());
        first[Symbol.dispose]();
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS * 2);
        expect(dispose).not.toHaveBeenCalled();
        second[Symbol.dispose]();
        second[Symbol.dispose]();
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS - 1);
        expect(dispose).not.toHaveBeenCalled();
        await vi.advanceTimersByTimeAsync(1);
        expect(dispose).toHaveBeenCalledOnce();
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it("resets idle expiry when a connection is reused", async () => {
        const first = await cache.acquire(session());
        first[Symbol.dispose]();
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS - 1);
        const second = await cache.acquire(session());
        second[Symbol.dispose]();
        await vi.advanceTimersByTimeAsync(1);
        expect(dispose).not.toHaveBeenCalled();
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS - 1);
        expect(dispose).toHaveBeenCalledOnce();
    });

    it("evicts failed handshakes so a retry can authenticate", async () => {
        connect.mockRejectedValueOnce(new Error("authentication failed"));
        await expect(cache.acquire(session())).rejects.toThrow("authentication failed");
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
        expect(dispose).toHaveBeenCalledOnce();
    });

    it("does not resurrect a pending connection after invalidation", async () => {
        const gate = new DeferredPromise<void>();
        connect.mockImplementationOnce(async function () {
            await gate.promise;
            this.connection = new Client();
            return this;
        });
        const acquiring = cache.acquire(session());
        const result = expect(acquiring).rejects.toThrow("invalidated");
        cache.invalidate(cache.getKey(session()));
        gate.resolve();
        await result;
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it("evicts dropped sockets and safely handles errors while idle", async () => {
        const first = await cache.acquire(session());
        first[Symbol.dispose]();
        expect(() => first.ssh.connection!.emit("error", new Error("socket failed"))).not.toThrow();
        await vi.advanceTimersByTimeAsync(0);
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it("reuses utility authentication for server startup and exposes its invalidation key", async () => {
        vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({ code: 0, signal: null, stdout: "", stderr: "" });
        const channel = serverChannel();
        vi.spyOn(ZSshClient.prototype as any, "execAsync").mockResolvedValue(channel);
        await ZSshUtils.lacksWriteAccess(session(), "/server");
        const client = await ZSshClient.create(session());
        expect(connect).toHaveBeenCalledOnce();
        expect(client.cacheKey).toBe(cache.getKey(session()));
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS * 2);
        expect(dispose).not.toHaveBeenCalled();
        cache.invalidate(client.cacheKey);
        expect(dispose).toHaveBeenCalledOnce();
        await expect(client.request({ command: "ping" })).rejects.toMatchObject({ errorCode: "ECONNCLOSED" });
    });

    it("closes only its server channel on disposal and removes its transport listeners", async () => {
        const firstChannel = serverChannel();
        const secondChannel = serverChannel();
        vi.spyOn(ZSshClient.prototype as any, "execAsync")
            .mockResolvedValueOnce(firstChannel)
            .mockResolvedValueOnce(secondChannel);
        const first = await ZSshClient.create(session());
        const second = await ZSshClient.create(session());
        const lease = await cache.acquire(session());
        const transport = lease.ssh.connection!;
        const listeners = transport.listenerCount("error");
        first.dispose();
        expect(firstChannel.close).toHaveBeenCalledOnce();
        expect(secondChannel.close).not.toHaveBeenCalled();
        expect(transport.listenerCount("error")).toBe(listeners - 1);
        expect(dispose).not.toHaveBeenCalled();
        second.dispose();
        lease[Symbol.dispose]();
        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS);
        expect(dispose).toHaveBeenCalledOnce();
    });

    it("preserves authentication after a missing-server error for deployment and relaunch", async () => {
        vi.spyOn(ZSshClient.prototype as any, "execAsync")
            .mockRejectedValueOnce(new ImperativeError({ msg: "missing", errorCode: "ENOTFOUND" }))
            .mockResolvedValueOnce(serverChannel());
        await expect(ZSshClient.create(session())).rejects.toMatchObject({ errorCode: "ENOTFOUND" });
        const lease = await cache.acquire(session());
        const client = await ZSshClient.create(session());
        expect(connect).toHaveBeenCalledOnce();
        expect(dispose).not.toHaveBeenCalled();
        client.dispose();
        lease[Symbol.dispose]();
    });

    it("skipCache uses a dedicated connection without consuming a cached one", async () => {
        using lease = await cache.acquire(session());
        vi.spyOn(ZSshClient.prototype as any, "execAsync").mockResolvedValue(serverChannel());
        vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
            this.emit("ready");
            return this;
        });
        const end = vi.spyOn(Client.prototype, "end").mockReturnThis();
        const client = await ZSshClient.create(session(), { skipCache: true });
        expect(client.cacheKey).toBeUndefined();
        client.dispose();
        expect(end).toHaveBeenCalledOnce();
        expect(lease.ssh.isConnected()).toBe(true);
        expect(dispose).not.toHaveBeenCalled();
    });

    it("native SSH startup opens a separate connection from the cached SFTP transport", async () => {
        using lease = await cache.acquire(session());
        const nativeTransport = new Client();
        const create = vi.spyOn(sshRs, "createClient").mockReturnValue(nativeTransport);
        vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
            this.emit("ready");
            return this;
        });
        const end = vi.spyOn(nativeTransport, "end").mockReturnThis();
        vi.spyOn(ZSshClient.prototype as any, "execAsync").mockResolvedValue(serverChannel());
        const client = await ZSshClient.create(session(), { useNativeSsh: true });
        expect(create).toHaveBeenCalledWith(true);
        expect(nativeTransport).not.toBe(lease.ssh.connection);
        expect(client.cacheKey).toBeUndefined();
        expect(connect).toHaveBeenCalledOnce();
        client.dispose();
        expect(end).toHaveBeenCalledOnce();
        expect(lease.ssh.isConnected()).toBe(true);
    });

    it("invalidates the cached transport on native handshake errors regardless of the error text", async () => {
        using _lease = await cache.acquire(session());
        const nativeTransport = new Client();
        vi.spyOn(sshRs, "createClient").mockReturnValue(nativeTransport);
        vi.spyOn(nativeTransport, "connect").mockImplementation(function () {
            this.emit("error", new Error("unclassified native failure"));
            return this;
        });
        vi.spyOn(nativeTransport, "end").mockReturnThis();
        await expect(ZSshClient.create(session(), { useNativeSsh: true })).rejects.toThrow(
            "unclassified native failure",
        );
        expect(dispose).toHaveBeenCalledOnce();
    });

    it("serializes concurrent profile launches and reuses the cached server client", async () => {
        vi.spyOn(ZSshClient.prototype as any, "execAsync").mockResolvedValue(serverChannel());
        const profile = { name: "ssh", type: "ssh", profile: { host: "example.com", user: "admin", password: "otp" } };
        const clients = await Promise.all([
            cache.connect(profile as any),
            cache.connect(profile as any),
            cache.connect(profile as any),
        ]);
        expect(clients[0]).toBe(clients[1]);
        expect(clients[1]).toBe(clients[2]);
        expect(connect).toHaveBeenCalledOnce();
        cache.invalidate(clients[0].cacheKey);
        const next = await cache.connect(profile as any);
        expect(next).not.toBe(clients[0]);
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it.each([
        { error: new Error("Unclassified failure"), socketError: true, expires: false },
        {
            error: Object.assign(new Error("Request failed"), { code: "ECONNRESET" }),
            socketError: false,
            expires: false,
        },
        {
            error: new ImperativeError({ msg: "Password expired", errorCode: "EPASSWD_EXPIRED" }),
            socketError: false,
            expires: true,
        },
    ])(
        "uses error source to decide whether a utility connection is invalidated: %j",
        async ({ error, socketError, expires }) => {
            vi.spyOn(NodeSSH.prototype, "execCommand").mockImplementation(async function () {
                if (socketError) this.connection!.emit("error", error);
                throw error;
            });
            await expect(ZSshUtils.lacksWriteAccess(session(), "/server")).rejects.toBe(error);
            expect(dispose).toHaveBeenCalledTimes(socketError || expires ? 1 : 0);
            using _next = await cache.acquire(session());
            expect(connect).toHaveBeenCalledTimes(socketError || expires ? 2 : 1);
        },
    );

    it.each([
        { message: "ECONNRESET: request failed", stderr: false },
        { message: "FOTS1669: remote task failed", stderr: true },
    ])("retains the connection when an RPC error contains connection-looking text: %j", async ({ message, stderr }) => {
        const channel = serverChannel();
        vi.spyOn(Client.prototype, "exec").mockImplementation(function (_command, callback) {
            callback(undefined, channel as unknown as import("ssh2").ClientChannel);
            queueMicrotask(() => channel.stdout.write(JSON.stringify({ status: "ready", data: { version: "1.1.0" } })));
            return this;
        });
        channel.stdin.on("data", (payload: Buffer) => {
            const { id } = JSON.parse(payload.toString());
            queueMicrotask(() =>
                (stderr ? channel.stderr : channel.stdout).write(
                    `${JSON.stringify({
                        jsonrpc: "2.0",
                        id,
                        error: { code: -32000, message },
                    })}\n`,
                ),
            );
        });
        using client = await ZSshClient.create(session());
        await expect(client.request({ command: "ping" })).rejects.toMatchObject({ errorCode: "-32000" });
        expect(dispose).not.toHaveBeenCalled();
        using _lease = await cache.acquire(session());
        expect(connect).toHaveBeenCalledOnce();
    });

    it.each(["FOTS1668", "FOTS1669"])("invalidates explicit SSH password expiry during startup: %s", async (code) => {
        const channel = serverChannel();
        vi.spyOn(Client.prototype, "exec").mockImplementation(function (_command, callback) {
            callback(undefined, channel as unknown as import("ssh2").ClientChannel);
            queueMicrotask(() => channel.stderr.write(`${code} Password expired`));
            return this;
        });
        await expect(ZSshClient.create(session())).rejects.toMatchObject({ errorCode: "EPASSWD_EXPIRED" });
        expect(dispose).toHaveBeenCalledOnce();
    });

    it("invalidates expired passwords even when the deployment callback consumes the error", async () => {
        vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({
            code: 1,
            signal: null,
            stdout: "",
            stderr: "FOTS1669",
        });
        const sftp = { end: vi.fn() };
        vi.spyOn(NodeSSH.prototype, "requestSFTP").mockResolvedValue(sftp as unknown as SFTPWrapper);
        const onError = vi.fn().mockResolvedValue(false);
        expect(await ZSshUtils.installServer(session(), "/server", { onError })).toBe(false);
        expect(onError).toHaveBeenCalledWith(expect.objectContaining({ errorCode: "EPASSWD_EXPIRED" }), "deploy");
        expect(dispose).toHaveBeenCalledOnce();
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it.each([
        { code: "ECONNRESET", shouldInvalidate: true },
        { code: 3, shouldInvalidate: false },
    ])(
        "uses SSH events rather than SFTP error codes to invalidate consumed upload errors: %j",
        async ({ code, shouldInvalidate }) => {
            vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({
                code: 0,
                signal: null,
                stdout: "",
                stderr: "",
            });
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 100, stderr: "" });
            using lease = await cache.acquire(session());
            const uploadError = Object.assign(new Error("Upload interrupted"), { code });
            const sftp = {
                fastPut: vi.fn((_local, _remote, _options, callback) => {
                    if (shouldInvalidate) lease.ssh.connection!.emit("error", uploadError);
                    callback(uploadError);
                }),
                end: vi.fn(),
            };
            vi.spyOn(NodeSSH.prototype, "requestSFTP").mockResolvedValue(sftp as unknown as SFTPWrapper);
            const onError = vi.fn(async () => {
                expect(dispose).toHaveBeenCalledTimes(shouldInvalidate ? 1 : 0);
                return false;
            });
            const options = { onError };
            expect(await ZSshUtils.installServer(session(), "/server", options)).toBe(false);
            expect(onError).toHaveBeenCalledWith(expect.objectContaining({ errorCode: "EDEPLOYFAIL" }), "upload");
            expect(onError.mock.contexts[0]).toBe(options);
            using _next = await cache.acquire(session());
            expect(connect).toHaveBeenCalledTimes(shouldInvalidate ? 2 : 1);
        },
    );

    it("can validate a profile without publishing its connection to the cache", async () => {
        vi.spyOn(SshConfigUtils, "migrateSshConfig").mockResolvedValue([]);
        vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({ code: 0, signal: null, stdout: "", stderr: "" });
        const profile = await new TestPrompt(testProfileInfo()).promptForProfile(undefined, {
            setExistingProfile: false,
            disableCreateNewProfile: true,
            skipCache: true,
        });
        expect(profile).toBeDefined();
        expect(dispose).toHaveBeenCalledOnce();
        using _next = await cache.acquire(session());
        expect(connect).toHaveBeenCalledTimes(2);
    });

    it.each([false, true])(
        "dedicated SFTP deployment closes its own connection and preserves an existing cache entry (expired: %s)",
        async (expired) => {
            using existing = await cache.acquire(session());
            vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({
                code: expired ? 1 : 0,
                signal: null,
                stdout: "",
                stderr: expired ? "FOTS1669" : "",
            });
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 100, stderr: "" });
            const sftp = {
                fastPut: vi.fn((_local, _remote, _options, callback) => callback()),
                unlink: vi.fn((_path, callback) => callback()),
                end: vi.fn(),
            };
            vi.spyOn(NodeSSH.prototype, "requestSFTP").mockResolvedValue(sftp as unknown as SFTPWrapper);
            expect(
                await ZSshUtils.installServer(session(), "/server", {
                    skipCache: true,
                    onError: vi.fn().mockResolvedValue(false),
                }),
            ).toBe(!expired);
            expect(dispose).toHaveBeenCalledOnce();
            expect(existing.ssh.isConnected()).toBe(true);
            using next = await cache.acquire(session());
            expect(next.ssh).toBe(existing.ssh);
            expect(connect).toHaveBeenCalledTimes(2);
        },
    );

    it("uses one authentication for profile validation, SFTP installation, and server relaunch", async () => {
        const profileInfo = testProfileInfo();
        vi.spyOn(SshConfigUtils, "migrateSshConfig").mockResolvedValue([]);
        vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({
            code: 0,
            signal: null,
            stdout: "",
            stderr: "",
        });
        vi.spyOn(ZSshUtils, "detectServerOnPath").mockResolvedValue({ hasExecutePermission: false });
        vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 100, stderr: "" });
        const sftp = {
            fastPut: vi.fn((_local, _remote, _options, callback) => callback()),
            unlink: vi.fn((_remote, callback) => callback()),
            end: vi.fn(),
        };
        vi.spyOn(NodeSSH.prototype, "requestSFTP").mockResolvedValue(sftp as unknown as SFTPWrapper);
        vi.spyOn(ZSshClient.prototype as any, "execAsync")
            .mockRejectedValueOnce(new ImperativeError({ msg: "missing", errorCode: "ENOTFOUND" }))
            .mockResolvedValueOnce(serverChannel());
        const profile = await new TestPrompt(profileInfo).promptForProfile(undefined, {
            setExistingProfile: false,
            disableCreateNewProfile: true,
        });
        expect(profile).toBeDefined();
        const client = await cache.connect(profile!);
        expect(client.cacheKey).toBe(cache.getKey(session()));
        expect(connect).toHaveBeenCalledOnce();
        expect(sftp.fastPut).toHaveBeenCalledOnce();
        expect(sftp.end).toHaveBeenCalledOnce();
        expect(dispose).not.toHaveBeenCalled();
    });
});
