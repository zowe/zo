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

import { Socket } from "node:net";
import { EventEmitter, PassThrough } from "node:stream";
import { ConfigUtils, type IHandlerParameters, ImperativeConfig, ProfileInfo } from "@zowe/imperative";
import { type CommandResponse, SshClientCache, ZSshUtils } from "@zowe/zowex-for-zowe-sdk";
import { NodeSSH } from "node-ssh";
import { Client, type ClientChannel } from "ssh2";
import SetupHandler, { CliPromptApi } from "../src/config/setup/Setup.handler";
import { SshBaseHandler } from "../src/SshBaseHandler";
import InstallHandler from "../src/server/install/Install.handler";
import UninstallHandler from "../src/server/uninstall/Uninstall.handler";

class TestHandler extends SshBaseHandler {
    public async processWithClient(): Promise<CommandResponse> {
        return { success: true };
    }
}

function handlerParameters(): IHandlerParameters {
    return {
        arguments: { host: "example.com", user: "admin", password: "otp", serverPath: "/opt/zo", responseTimeout: 30 },
        response: {
            console: { log: vi.fn(), error: vi.fn(), prompt: vi.fn() },
            data: { setObj: vi.fn() },
            progress: { endBar: vi.fn(), startBar: vi.fn() },
        },
    } as unknown as IHandlerParameters;
}

function mockSsh() {
    const authenticate = vi.fn();
    const channels: Array<{ close: ReturnType<typeof vi.fn> }> = [];
    vi.spyOn(NodeSSH.prototype, "connect").mockImplementation(async function () {
        authenticate();
        this.connection = new Client();
        return this;
    });
    vi.spyOn(Client.prototype, "connect").mockImplementation(function () {
        authenticate();
        this.emit("ready");
        return this;
    });
    const end = vi.spyOn(Client.prototype, "end").mockImplementation(function () {
        this.emit("close");
        return this;
    });
    vi.spyOn(Client.prototype, "exec").mockImplementation(function (_command, callback) {
        const channel = Object.assign(new EventEmitter(), {
            stdin: new PassThrough(),
            stdout: new PassThrough(),
            stderr: new PassThrough(),
            close: vi.fn(() => channel.emit("close")),
        });
        channels.push(channel);
        callback(undefined, channel as unknown as ClientChannel);
        queueMicrotask(() => channel.stdout.write(JSON.stringify({ status: "ready", data: { version: "1.1.0" } })));
        return this;
    });
    return { authenticate, channels, end };
}

describe("CLI SSH connection lifetime", () => {
    const socket = new Socket();

    afterEach(() => {
        SshClientCache.inst.dispose();
        vi.useRealTimers();
    });

    it("reuses authentication across daemon commands while disposing each server channel", async () => {
        vi.useFakeTimers();
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue({ stream: socket });
        const { authenticate, channels, end } = mockSsh();
        const params = handlerParameters();

        await new TestHandler().process(params);
        await new TestHandler().process(params);

        expect(authenticate).toHaveBeenCalledOnce();
        expect(channels).toHaveLength(2);
        for (const channel of channels) expect(channel.close).toHaveBeenCalledOnce();
        expect(end).not.toHaveBeenCalled();
        expect(params.response.data.setObj).toHaveBeenCalledTimes(2);

        await vi.advanceTimersByTimeAsync(SshClientCache.TTL_MS);
        expect(end).toHaveBeenCalledOnce();
    });

    it.each([undefined, {}])("closes connections after each command outside daemon execution: %j", async (context) => {
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue(context!);
        const { authenticate, end } = mockSsh();

        await new TestHandler().process(handlerParameters());
        await new TestHandler().process(handlerParameters());

        expect(authenticate).toHaveBeenCalledTimes(2);
        expect(end).toHaveBeenCalledTimes(2);
    });

    it("retains a daemon connection when an RPC command fails", async () => {
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue({ stream: socket });
        const { authenticate, end } = mockSsh();
        const failure = new Error("Dataset not found");
        const handler = new TestHandler();
        vi.spyOn(handler, "processWithClient").mockRejectedValue(failure);

        await expect(handler.process(handlerParameters())).rejects.toBe(failure);
        await new TestHandler().process(handlerParameters());

        expect(authenticate).toHaveBeenCalledOnce();
        expect(end).not.toHaveBeenCalled();
    });

    it("closes connections when a standalone RPC command fails", async () => {
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue(undefined!);
        const { end } = mockSsh();
        const failure = new Error("Dataset not found");
        const handler = new TestHandler();
        vi.spyOn(handler, "processWithClient").mockRejectedValue(failure);

        await expect(handler.process(handlerParameters())).rejects.toBe(failure);
        expect(end).toHaveBeenCalledOnce();
    });

    it.each([
        { install: true, daemon: true },
        { install: true, daemon: false },
        { install: false, daemon: true },
        { install: false, daemon: false },
    ])("uses the same cache policy for server helpers: %j", async ({ install, daemon }) => {
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue(
            daemon ? { stream: socket } : undefined!,
        );
        vi.spyOn(ConfigUtils, "getActiveProfileName").mockReturnValue("ssh");
        const helper = install
            ? vi.spyOn(ZSshUtils, "installServer").mockResolvedValue(true)
            : vi.spyOn(ZSshUtils, "uninstallServer").mockResolvedValue(undefined);
        const handler = install ? new InstallHandler() : new UninstallHandler();
        await handler.process(handlerParameters());
        expect(helper).toHaveBeenCalledWith(
            expect.anything(),
            "/opt/zo",
            expect.objectContaining({ skipCache: !daemon }),
        );
    });

    it.each([false, true])("uses the same cache policy for setup validation (daemon: %s)", async (daemon) => {
        vi.spyOn(ImperativeConfig.instance, "daemonContext", "get").mockReturnValue(
            daemon ? { stream: socket } : undefined!,
        );
        vi.spyOn(ProfileInfo.prototype, "readProfilesFromDisk").mockResolvedValue(undefined);
        vi.spyOn(ProfileInfo.prototype, "getAllProfiles").mockReturnValue([]);
        const prompt = vi.spyOn(CliPromptApi.prototype, "promptForProfile").mockResolvedValue(undefined);
        await new SetupHandler().process(handlerParameters());
        expect(prompt).toHaveBeenCalledWith(undefined, expect.objectContaining({ skipCache: !daemon }));
    });
});
