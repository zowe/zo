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

import { PassThrough } from "node:stream";
import { Client, type ClientCallback } from "ssh2";
import { SshProxyConnection } from "../src/SshProxyConnection";

vi.mock("ssh2");

describe("SshProxyConnection", () => {
    const target = { host: "target.internal", username: "target", password: "target-password" };
    const hop = { host: "bastion", username: "jump", password: "jump-password" };
    let channels: PassThrough[];
    let clients: Client[];

    beforeEach(() => {
        channels = [];
        clients = [];
        vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
            clients.push(this);
            queueMicrotask(() => this.emit("ready"));
        });
        vi.spyOn(Client.prototype, "forwardOut").mockImplementation(
            (_src, _port, _host, _destPort, callback: ClientCallback) => {
                const channel = new PassThrough();
                channels.push(channel);
                callback(undefined, channel as never);
            },
        );
    });

    it("leaves direct connections direct", async () => {
        using proxy = new SshProxyConnection();
        expect(await proxy.open(target)).toEqual({ ...target, sock: undefined });
        expect(Client.prototype.connect).not.toHaveBeenCalled();
    });

    it("connects each explicit hop using its own credentials and forwards target DNS to the jump", async () => {
        using proxy = new SshProxyConnection();
        const config = await proxy.open({ ...target, jumpHosts: [hop, { host: "inner", username: "inner-user" }] });
        expect(Client.prototype.connect).toHaveBeenNthCalledWith(1, expect.objectContaining(hop));
        expect(Client.prototype.connect).toHaveBeenNthCalledWith(
            2,
            expect.objectContaining({ host: "inner", sock: channels[0], username: "inner-user" }),
        );
        expect(Client.prototype.forwardOut).toHaveBeenNthCalledWith(
            2,
            "127.0.0.1",
            0,
            "target.internal",
            22,
            expect.any(Function),
        );
        expect(config).toEqual({ ...target, sock: channels[1] });
    });

    it("closes every channel and jump exactly once on disposal", async () => {
        const end = vi.spyOn(Client.prototype, "end");
        const proxy = new SshProxyConnection();
        await proxy.open({ ...target, jumpHosts: [hop, hop] });
        proxy[Symbol.dispose]();
        proxy[Symbol.dispose]();
        expect(end).toHaveBeenCalledTimes(2);
        expect(channels.every((channel) => channel.destroyed)).toBe(true);
    });

    it("cleans up and rejects when a jump cannot authenticate", async () => {
        vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
            this.emit("error", new Error("auth failed"));
        });
        const end = vi.spyOn(Client.prototype, "end");
        await expect(new SshProxyConnection().open({ ...target, jumpHosts: [hop] })).rejects.toThrow("auth failed");
        expect(end).toHaveBeenCalledTimes(1);
    });

    it("reports denied forwarding and cleans up without falling back to direct", async () => {
        vi.mocked(Client.prototype.forwardOut).mockImplementation((_a, _b, _c, _d, callback) =>
            callback(new Error("denied"), undefined as never),
        );
        const end = vi.spyOn(Client.prototype, "end");
        await expect(new SshProxyConnection().open({ ...target, jumpHosts: [hop] })).rejects.toThrow(
            "could not forward",
        );
        expect(end).toHaveBeenCalledTimes(1);
        expect(Client.prototype.connect).toHaveBeenCalledTimes(1);
    });

    it("bounds a stalled forwarding request", async () => {
        vi.useFakeTimers();
        try {
            vi.mocked(Client.prototype.forwardOut).mockImplementation(() => {});
            const promise = new SshProxyConnection().open({ ...target, jumpHosts: [{ ...hop, readyTimeout: 20 }] });
            const rejected = expect(promise).rejects.toThrow("Timed out forwarding");
            await vi.advanceTimersByTimeAsync(21);
            await rejected;
        } finally {
            vi.useRealTimers();
        }
    });

    it("closes the chain when the target socket closes", async () => {
        const end = vi.spyOn(Client.prototype, "end");
        await new SshProxyConnection().open({ ...target, jumpHosts: [hop] });
        channels[0].emit("close");
        expect(end).toHaveBeenCalledTimes(1);
    });

    it("rejects a hop closing before it is ready", async () => {
        vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
            this.emit("close");
        });
        await expect(new SshProxyConnection().open({ ...target, jumpHosts: [hop] })).rejects.toThrow("closed");
    });

    it("rejects a closed jump while waiting for its forwarding channel", async () => {
        vi.mocked(Client.prototype.forwardOut).mockImplementation(function () {
            this.emit("close");
        });
        await expect(new SshProxyConnection().open({ ...target, jumpHosts: [hop] })).rejects.toThrow(
            "closed while forwarding",
        );
    });

    it("destroys a forwarding channel arriving after disposal", async () => {
        let complete: ClientCallback;
        const requested = new Promise<void>((resolve) => {
            vi.mocked(Client.prototype.forwardOut).mockImplementation((_a, _b, _c, _d, callback) => {
                complete = callback;
                resolve();
            });
        });
        const proxy = new SshProxyConnection();
        const opening = proxy.open({ ...target, jumpHosts: [hop] });
        const rejected = expect(opening).rejects.toThrow("disposed");
        await requested;
        proxy[Symbol.dispose]();
        const late = new PassThrough();
        complete!(undefined, late as never);
        await rejected;
        expect(late.destroyed).toBe(true);
    });
});
