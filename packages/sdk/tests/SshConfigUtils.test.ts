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

import { readdirSync, readFileSync } from "node:fs";
import { join, normalize, resolve } from "node:path";
import { SshConfigUtils } from "../src/SshConfigUtils";

vi.mock("node:fs", () => ({
    accessSync: vi.fn(),
    readFileSync: vi.fn(),
    readdirSync: vi.fn(),
    constants: {
        R_OK: vi.fn(),
    },
}));
vi.mock("node:os", () => ({
    homedir: vi.fn(() => "/home/dir"),
}));

describe("findPrivateKeys", () => {
    it("should find private keys in home directory", async () => {
        const homeDir = "/home/dir";
        const expected = [
            resolve(join(homeDir, ".ssh", "id_ed25519")),
            resolve(join(homeDir, ".ssh", "id_rsa")),
            resolve(join(homeDir, ".ssh", "id_ecdsa")),
            resolve(join(homeDir, ".ssh", "id_dsa")),
        ];
        expect(await SshConfigUtils.findPrivateKeys()).toStrictEqual(expected);
    });

    it("should ignore keys that are not readable/accessible", async () => {
        const { accessSync } = await import("node:fs");
        const mockAccessSync = accessSync as ReturnType<typeof vi.fn>;
        mockAccessSync.mockImplementation((path: string) => {
            if (path.endsWith("id_rsa")) {
                return; // success
            }
            throw new Error("not accessible");
        });

        const homeDir = "/home/dir";
        const expected = [resolve(join(homeDir, ".ssh", "id_rsa"))];
        expect(await SshConfigUtils.findPrivateKeys()).toStrictEqual(expected);
    });
});

describe("migrateSshConfig", () => {
    const mockReadFileSync = readFileSync as ReturnType<typeof vi.fn>;

    beforeEach(() => {
        vi.clearAllMocks();
    });

    it("should return empty array when config file does not exist", async () => {
        mockReadFileSync.mockImplementation(() => {
            throw new Error("File not found");
        });

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toStrictEqual([]);
    });

    it("should parse a basic SSH config with hostname and user", async () => {
        const configContent = `
Host myserver
    HostName example.com
    User myuser
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0]).toMatchObject({
            name: "myserver",
            hostname: "example.com",
            user: "myuser",
        });
    });

    it("should parse SSH config with all supported fields", async () => {
        const configContent = `
Host production
    HostName prod.example.com
    Port 2222
    User admin
    IdentityFile ~/.ssh/prod_key
    ConnectTimeout 30
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0]).toMatchObject({
            name: "production",
            hostname: "prod.example.com",
            port: 2222,
            user: "admin",
            privateKey: normalize(join("/home/dir", ".ssh", "prod_key")),
            handshakeTimeout: 30000,
        });
    });

    it("should handle multiple host configurations", async () => {
        const configContent = `
Host server1
    HostName host1.example.com
    User user1

Host server2
    HostName host2.example.com
    User user2
    Port 22222
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(2);
        expect(result[0].name).toBe("server1");
        expect(result[1].name).toBe("server2");
        expect(result[1].port).toBe(22222);
    });

    it("should take first name when Host has multiple values", async () => {
        const configContent = `
Host alias1 alias2 alias3
    HostName example.com
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0].name).toBe("alias1");
    });

    it("should ignore unsupported SSH config parameters", async () => {
        const configContent = `
Host server
    HostName example.com
    ForwardAgent yes
    ProxyJump bastion
    ServerAliveInterval 60
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0]).toMatchObject({
            name: "server",
            hostname: "example.com",
        });
        expect(result[0]).not.toHaveProperty("forwardAgent");
        expect(result[0].proxyJump).toBe("bastion");
    });

    it("should handle config with only Host directive and no parameters", async () => {
        const configContent = `
Host emptyhost
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0]).toMatchObject({
            name: "emptyhost",
        });
    });

    it("should handle empty config file", async () => {
        mockReadFileSync.mockReturnValue("");

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toStrictEqual([]);
    });

    it("should handle config with comments and whitespace", async () => {
        const configContent = `
# This is a comment
Host myserver
    # Another comment
    HostName example.com
    User myuser
    # More comments
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0]).toMatchObject({
            name: "myserver",
            hostname: "example.com",
            user: "myuser",
        });
    });

    it("should convert ConnectTimeout from seconds to milliseconds", async () => {
        const configContent = `
Host server
    HostName example.com
    ConnectTimeout 45
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0].handshakeTimeout).toBe(45000);
    });

    it("should skip wildcard hosts containing * or ?", async () => {
        const configContent = `
Host *
    HostName generic.com
Host my?server
    HostName specific.com
Host normal
    HostName normal.com
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0].name).toBe("normal");
        expect(result[0].hostname).toBe("generic.com"); // OpenSSH uses the first obtained value.
    });

    it("should parse IdentityFile using non-~ (absolute) path", async () => {
        const configContent = `
Host server
    HostName example.com
    IdentityFile /var/lib/ssh/key
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result).toHaveLength(1);
        expect(result[0].privateKey).toBe(normalize("/var/lib/ssh/key"));
    });

    it("should parse IdentityAgent using a literal socket path", async () => {
        const configContent = `
Host server
    HostName example.com
    IdentityAgent /run/user/1000/ssh-agent.sock
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0].identityAgent).toBe(normalize("/run/user/1000/ssh-agent.sock"));
    });

    it("should expand ~ in IdentityAgent", async () => {
        const configContent = `
Host server
    HostName example.com
    IdentityAgent ~/.ssh/agent.sock
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0].identityAgent).toBe(normalize(join("/home/dir", ".ssh", "agent.sock")));
    });

    it("should preserve SSH_AUTH_SOCK as a special value when set", async () => {
        mockReadFileSync.mockReturnValue(`
Host server
    HostName example.com
    IdentityAgent SSH_AUTH_SOCK
`);
        vi.stubEnv("SSH_AUTH_SOCK", "/tmp/ssh-agent.sock");

        try {
            const result = await SshConfigUtils.migrateSshConfig();
            expect(result[0].identityAgent).toBe("SSH_AUTH_SOCK");
        } finally {
            vi.unstubAllEnvs();
        }
    });

    it("should preserve SSH_AUTH_SOCK as a special value when unset", async () => {
        mockReadFileSync.mockReturnValue(`
Host server
    HostName example.com
    IdentityAgent SSH_AUTH_SOCK
`);
        vi.stubEnv("SSH_AUTH_SOCK", undefined);

        try {
            const result = await SshConfigUtils.migrateSshConfig();
            expect(result[0].identityAgent).toBe("SSH_AUTH_SOCK");
        } finally {
            vi.unstubAllEnvs();
        }
    });

    it("should leave agent unset when IdentityAgent is none", async () => {
        const configContent = `
Host server
    HostName example.com
    IdentityAgent none
`;
        mockReadFileSync.mockReturnValue(configContent);

        const result = await SshConfigUtils.migrateSshConfig();
        expect(result[0]).not.toHaveProperty("identityAgent");
    });
});

describe("explicit jump hosts", () => {
    beforeEach(() => {
        vi.mocked(readFileSync).mockReturnValue(`
Host target
    HostName target.example.com
    ProxyJump inherited
Host bastion
    HostName bastion.example.com
    User jumpuser
    Port 2222
    IdentityFile ~/.ssh/jump_key
    ProxyJump hidden-hop
Host *
    ConnectTimeout 5
`);
    });

    it("does not read SSH config for omitted or disabled routing", () => {
        vi.mocked(readFileSync).mockClear();
        expect(SshConfigUtils.resolveJumpHosts()).toEqual([]);
        expect(SshConfigUtils.resolveJumpHosts("none")).toEqual([]);
        expect(SshConfigUtils.resolveJumpHosts(" NONE ")).toEqual([]);
        expect(readFileSync).not.toHaveBeenCalled();
    });

    it("resolves alias attributes without following the alias's own ProxyJump", () => {
        expect(SshConfigUtils.resolveJumpHosts("bastion")).toEqual([
            {
                hostname: "bastion.example.com",
                user: "jumpuser",
                port: 2222,
                privateKey: normalize("/home/dir/.ssh/jump_key"),
                handshakeTimeout: 5000,
            },
        ]);
    });

    it("lets explicit user and port override SSH alias settings", () => {
        const [hop] = SshConfigUtils.resolveJumpHosts("override@bastion:2200");
        expect(hop).toMatchObject({ hostname: "bastion.example.com", user: "override", port: 2200 });
    });

    it("preserves explicit hop ordering and supports bracketed IPv6 and SSH URIs", () => {
        const hops = SshConfigUtils.resolveJumpHosts("bastion,other@[::1]:2223,ssh://final@last:2224");
        expect(hops.map(({ hostname, user, port }) => ({ hostname, user, port }))).toEqual([
            { hostname: "bastion.example.com", user: "jumpuser", port: 2222 },
            { hostname: "::1", user: "other", port: 2223 },
            { hostname: "last", user: "final", port: 2224 },
        ]);
    });

    it.each([
        "",
        " ",
        "bastion,",
        "-option",
        "user@bad host",
        "host:0",
        "host:65536",
        "ssh://user:password@host",
        "ssh://host/path",
    ])("rejects malformed route %s", (route) => {
        expect(() => SshConfigUtils.resolveJumpHosts(route)).toThrow();
    });

    it("rejects non-string route values", () => {
        expect(() => SshConfigUtils.resolveJumpHosts(true as unknown as string)).toThrow("proxyJump must be");
    });

    it("imports an alias without HostName and explicitly copies its jump", async () => {
        vi.mocked(readFileSync).mockReturnValue("Host target\n ProxyJump bastion\n User targetuser\n");
        expect(await SshConfigUtils.migrateSshConfig()).toEqual([
            { name: "target", hostname: "target", user: "targetuser", proxyJump: "bastion" },
        ]);
    });

    it("expands Include files, applies wildcard defaults, and handles directive casing", async () => {
        vi.mocked(readFileSync).mockImplementation((file) => {
            if (String(file).endsWith("config"))
                return 'Include "conf.d/*.conf" # comment\nHost *\n connecttimeout 8\n';
            if (String(file).endsWith("target.conf"))
                return "host target\n hostname target.example.com\n proxyjump bastion\n";
            throw Object.assign(new Error("missing"), { code: "ENOENT" });
        });
        vi.mocked(readdirSync).mockReturnValue(["target.conf"] as never);
        expect(await SshConfigUtils.migrateSshConfig()).toEqual([
            { name: "target", hostname: "target.example.com", proxyJump: "bastion", handshakeTimeout: 8000 },
        ]);
    });

    it("rejects cyclic SSH Include files", async () => {
        vi.mocked(readFileSync).mockReturnValue("Include config\n");
        await expect(SshConfigUtils.migrateSshConfig()).rejects.toThrow("Cyclic");
    });
});
