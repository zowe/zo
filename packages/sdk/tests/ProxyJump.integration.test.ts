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

import * as fs from "node:fs";
import { homedir, tmpdir } from "node:os";
import { join } from "node:path";
import { Duplex, PassThrough } from "node:stream";
import { ConfigSchema, Logger, ProfileInfo } from "@zowe/imperative";
import { ZosUssProfile } from "@zowe/zos-uss-for-zowe-sdk";
import { Client, type ConnectConfig, type Connection, Server, utils } from "ssh2";
import { SshConfigUtils } from "../src/SshConfigUtils";
import { SessionContext } from "../src/utils/SessionContext";
import { ZSshClient } from "../src/ZSshClient";
import { ZSshUtils } from "../src/ZSshUtils";

vi.mock("node:fs", async (importOriginal) => {
    const actual = await importOriginal<typeof import("node:fs")>();
    return { ...actual, readFileSync: vi.fn(actual.readFileSync) };
});

// The SSH protocol, authentication, forwarding, exec, SFTP, and RPC are real.
// Only TCP sockets and z/OS commands are replaced by local in-memory peers.
describe("ProxyJump with imported and manually configured profiles", () => {
    let directory: string;
    let sshConfig: string;
    let roots: string[];
    let uploaded: Map<number, Buffer>;
    let peers: Set<Connection>;
    let sockets: Duplex[];
    let clients: Client[];
    let servers: Map<string, Server>;
    const hostKey = utils.generateKeyPairSync("ed25519").private;

    function dial(host: string): Duplex {
        const a = new PassThrough();
        const b = new PassThrough();
        const client = Duplex.from({ readable: a, writable: b });
        const server = Duplex.from({ readable: b, writable: a });
        client.on("error", () => {});
        server.on("error", () => {});
        sockets.push(client, server);
        const endpoint = servers.get(host);
        if (!endpoint) throw new Error(`Unknown test endpoint: ${host}`);
        endpoint.injectSocket(server);
        return client;
    }

    function makeServer(host: string, user: string): void {
        const server = new Server({ hostKeys: [hostKey] }, (connection) => {
            peers.add(connection);
            connection.on("error", () => {});
            connection.on("authentication", (context) => {
                if (
                    context.username === user &&
                    (context.method === "publickey" ||
                        (context.method === "password" && context.password === "target-password"))
                )
                    context.accept();
                else context.reject(["publickey", "password"]);
            });
            connection.on("ready", () => {
                connection.on("tcpip", (accept, reject, info) => {
                    if (!servers.has(info.destIP)) return reject();
                    const channel = accept();
                    const socket = dial(info.destIP);
                    channel.on("error", () => socket.destroy());
                    socket.pipe(channel).pipe(socket);
                });
                connection.on("session", (accept) => {
                    const session = accept();
                    session.on("exec", (acceptExec, _reject, info) => {
                        const channel = acceptExec();
                        if (/\bzo server\b/.test(info.command)) {
                            channel.write(`${JSON.stringify({ status: "ready", data: { version: "1.0.2" } })}\n`);
                            let pending = "";
                            channel.on("data", (chunk) => {
                                pending += chunk;
                                let index = pending.indexOf("\n");
                                while (index >= 0) {
                                    const request = JSON.parse(pending.slice(0, index));
                                    pending = pending.slice(index + 1);
                                    index = pending.indexOf("\n");
                                    channel.write(
                                        `${JSON.stringify({ jsonrpc: "2.0", id: request.id, result: { success: true } })}\n`,
                                    );
                                }
                            });
                            return;
                        }
                        if (info.command.startsWith("df "))
                            channel.write(
                                "Mounted on Filesystem Avail/Total Files Status\n/u (TEST.ZFS) 100/200 1 Available\n",
                            );
                        else if (info.command.includes("--version")) channel.write("1.0.2\n");
                        channel.exit(info.command.startsWith("test -e") ? 1 : 0);
                        channel.end();
                    });
                    session.on("sftp", (acceptSftp) => {
                        const sftp = acceptSftp();
                        sftp.on("OPEN", (id) => sftp.handle(id, Buffer.from("file")));
                        sftp.on("WRITE", (id, _handle, offset, bytes) => {
                            uploaded.set(offset, Buffer.from(bytes));
                            sftp.status(id, 0);
                        });
                        sftp.on("CLOSE", (id) => sftp.status(id, 0));
                        sftp.on("REMOVE", (id) => sftp.status(id, 0));
                        sftp.on("RMDIR", (id) => sftp.status(id, 0));
                    });
                });
            });
        });
        servers.set(host, server);
    }

    beforeEach(async () => {
        directory = fs.mkdtempSync(join(tmpdir(), "zo-proxyjump-"));
        roots = [];
        uploaded = new Map();
        peers = new Set();
        sockets = [];
        clients = [];
        servers = new Map();
        const keyFile = join(directory, "jump_key");
        fs.writeFileSync(keyFile, utils.generateKeyPairSync("ed25519").private);
        fs.writeFileSync(join(directory, "server.pax.Z"), Buffer.from(Array.from({ length: 256 }, (_, i) => i)));
        sshConfig =
            `Host target.internal\n HostName target.internal\n User target\n ProxyJump bastion\n` +
            `Host bastion\n HostName jump.internal\n User jump\n IdentityFile ${keyFile}\n ProxyJump hidden\n` +
            `Host override\n HostName alternate.internal\n User alternate\n IdentityFile ${keyFile}\n ProxyJump hidden\n`;
        const read = (await vi.importActual<typeof import("node:fs")>("node:fs")).readFileSync;
        vi.spyOn(fs, "readFileSync").mockImplementation((file, ...args) => {
            if (String(file) === join(homedir(), ".ssh", "config")) return sshConfig;
            return read(file, ...(args as [never])) as never;
        });
        const connect = Client.prototype.connect;
        vi.spyOn(Client.prototype, "connect").mockImplementation(function (config: ConnectConfig) {
            clients.push(this);
            if (!config.sock) roots.push(config.host!);
            return connect.call(this, { ...config, sock: config.sock ?? dial(config.host!) });
        });
        const logger = new Proxy({} as Logger, { get: () => vi.fn() });
        vi.spyOn(Logger, "getAppLogger").mockReturnValue(logger);
        makeServer("target.internal", "target");
        makeServer("jump.internal", "jump");
        makeServer("alternate.internal", "alternate");
    });

    afterEach(async () => {
        for (const client of clients) client.on("error", () => {});
        for (const peer of peers) peer.end();
        for (const socket of sockets) socket.destroy();
        for (const server of servers.values()) server.close();
        vi.restoreAllMocks();
        fs.rmSync(directory, { recursive: true, force: true });
        await new Promise((resolve) => setImmediate(resolve));
    });

    async function readTeamConfig(proxyJump?: string): Promise<ReturnType<typeof ZSshUtils.buildSession>> {
        const properties = { host: "target.internal", user: "target", password: "target-password", proxyJump };
        fs.writeFileSync(
            join(directory, "zowe.schema.json"),
            JSON.stringify(ConfigSchema.buildSchema([ZosUssProfile])),
        );
        fs.writeFileSync(
            join(directory, "zowe.config.json"),
            JSON.stringify({
                $schema: "./zowe.schema.json",
                profiles: { test: { type: "ssh", properties } },
                defaults: { ssh: "test" },
                autoStore: false,
            }),
        );
        const profiles = new ProfileInfo("zowe");
        await profiles.readProfilesFromDisk({ homeDir: directory, projectDir: directory });
        const attributes = profiles.getAllProfiles("ssh").find((profile) => profile.profName === "test")!;
        const merged = profiles.mergeArgsForProfile(attributes, { getSecureVals: false });
        return ZSshUtils.buildSession(Object.fromEntries(merged.knownArgs.map((arg) => [arg.argName, arg.argValue])));
    }

    it.each([
        [undefined, "target.internal"],
        ["none", "target.internal"],
        ["bastion", "jump.internal"],
        ["override", "alternate.internal"],
        ["bastion,override", "jump.internal"],
    ])("loads manual team config proxyJump=%s and uses only the explicit route", async (proxyJump, expectedRoot) => {
        using context = new SessionContext(await readTeamConfig(proxyJump));
        const ssh = await context.getSsh();
        expect((await ssh.execCommand("exit")).code).toBe(0);
        expect(roots).toEqual([expectedRoot]);
        expect(vi.mocked(Client.prototype.connect).mock.calls.map(([config]) => config.host)).not.toContain("hidden");
    });

    it("imports the jump explicitly, deploys binary bytes, and starts RPC through it", async () => {
        const imported = (await SshConfigUtils.migrateSshConfig()).find((config) => config.name === "target.internal")!;
        expect(imported.proxyJump).toBe("bastion");
        const session = await readTeamConfig(imported.proxyJump);
        vi.spyOn(ZSshUtils as unknown as { getBinDir: () => string }, "getBinDir").mockReturnValue(directory);
        using context = new SessionContext(session);
        expect(await ZSshUtils.installServer(context, "/fake/proxyjump")).toBe(true);
        const bytes = Buffer.concat([...uploaded].sort(([a], [b]) => a - b).map(([, bytes]) => bytes));
        expect(bytes).toEqual(fs.readFileSync(join(directory, "server.pax.Z")));
        using client = await ZSshClient.create(session);
        expect(await client.request({ command: "ping" } as never)).toEqual({ success: true });
        expect(roots).toEqual(["jump.internal", "jump.internal"]);
    });
});
