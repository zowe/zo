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
import { DeferredPromise, ImperativeError, Logger } from "@zowe/imperative";
import { type ISshSession, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import { NodeSSH, type SSHExecCommandResponse } from "node-ssh";
import { SshErrors } from "../src/SshErrors";
import { SessionContext } from "../src/utils";
import { ZSshUtils } from "../src/ZSshUtils";

vi.mock("../src/ZSshConstants", () => ({
    BUNDLED_SSH_SERVER_VERSION: "1.2.1",
}));

import { ZSshClient } from "../src";
import { BUNDLED_SSH_SERVER_VERSION } from "../src/ZSshConstants";

vi.mock("node:fs", { spy: true });

function setupSftpMocks(
    sftpMock: object,
    sshMock: { execCommand: ReturnType<typeof vi.fn>; withShell?: ReturnType<typeof vi.fn> },
    options?: { mockGetBinDir?: boolean; mockExistsSync?: boolean },
): void {
    vi.spyOn(ZSshUtils as any, "sftp").mockImplementation(
        async (_session: any, callback: (sftp: any, ssh: any) => Promise<any>) => callback(sftpMock, sshMock),
    );
    if (options?.mockGetBinDir !== false) {
        vi.spyOn(ZSshUtils as any, "getBinDir").mockReturnValue("/fake/bin");
    }
    if (options?.mockExistsSync !== false) {
        vi.spyOn(fs, "existsSync").mockReturnValue(true);
    }
}

describe("ZSshUtils", () => {
    it("reuses a connection only while a SessionContext is in scope", async () => {
        const connectSpy = vi.spyOn(NodeSSH.prototype, "connect").mockResolvedValue({} as any);
        const execSpy = vi
            .spyOn(NodeSSH.prototype, "execCommand")
            .mockResolvedValue({ code: 0, stdout: "", stderr: "" } as any);
        const disposeSpy = vi.spyOn(NodeSSH.prototype, "dispose").mockImplementation(() => {});
        vi.spyOn(NodeSSH.prototype, "isConnected").mockReturnValue(true);
        const session = new SshSession({ hostname: "example.com", user: "admin" });

        await ZSshUtils.lacksWriteAccess(session, "/tmp");
        await ZSshUtils.lacksWriteAccess(session, "/tmp");
        expect(connectSpy).toHaveBeenCalledTimes(2);
        expect(disposeSpy).toHaveBeenCalledTimes(2);

        {
            using context = new SessionContext(session);
            await ZSshUtils.lacksWriteAccess(context, "/tmp");
            await ZSshUtils.lacksWriteAccess(context, "/tmp");
            expect(connectSpy).toHaveBeenCalledTimes(3);
            expect(execSpy).toHaveBeenCalledTimes(8);
            expect(disposeSpy).toHaveBeenCalledTimes(2);
        }
        expect(disposeSpy).toHaveBeenCalledTimes(3);
    });

    it.each(["resolve", "reject"] as const)(
        "keeps the SSH connection open until permission checks %s",
        async (outcome) => {
            vi.spyOn(NodeSSH.prototype, "connect").mockResolvedValue(new NodeSSH());
            const disposeSpy = vi.spyOn(NodeSSH.prototype, "dispose").mockImplementation(() => {});
            const commandStarted = new DeferredPromise<void>();
            const commandResult = new DeferredPromise<SSHExecCommandResponse>();
            const response = { code: 0, stdout: "", stderr: "" };
            vi.spyOn(NodeSSH.prototype, "execCommand")
                .mockImplementationOnce(() => {
                    commandStarted.resolve();
                    return commandResult.promise;
                })
                .mockResolvedValue(response);
            const session = new SshSession({ hostname: "example.com", user: "admin" });
            const error = new Error("Permission check failed");

            const result = ZSshUtils.lacksWriteAccess(session, "/tmp");
            const resultAssertion =
                outcome === "resolve" ? expect(result).resolves.toBe(false) : expect(result).rejects.toBe(error);
            await commandStarted.promise;
            const disposedWhilePending = disposeSpy.mock.calls.length > 0;
            if (outcome === "resolve") {
                commandResult.resolve(response);
            } else {
                commandResult.reject(error);
            }
            await resultAssertion;

            expect(disposedWhilePending).toBe(false);
            expect(disposeSpy).toHaveBeenCalledOnce();
        },
    );

    describe("checkIfOutdated", () => {
        it.each([
            // compared against mocked zo binary version: 1.2.1
            {
                desc: "versions match exactly- not outdated",
                remoteVersion: BUNDLED_SSH_SERVER_VERSION,
                expected: false,
            },
            {
                desc: "remote version is lower by major version - outdated",
                remoteVersion: "0.9.1",
                expected: true,
            },
            {
                desc: "remote version is lower by minor version - outdated",
                remoteVersion: "1.1.19",
                expected: true,
            },
            {
                desc: "remote version is lower by patch version - outdated",
                remoteVersion: "1.2.0",
                expected: true,
            },
            {
                desc: "older patch version also with suffix - outdated",
                remoteVersion: `1.2.0-aefdab`,
                expected: true,
            },
            {
                desc: "remote version is not a valid semver string - outdated",
                remoteVersion: "footballbaseball",
                expected: true,
            },
            {
                desc: "remote version is undefined - outdated",
                remoteVersion: undefined,
                expected: true,
            },
            {
                desc: "matching versions with different git hash - suffix is discarded - not outdated",
                remoteVersion: `${BUNDLED_SSH_SERVER_VERSION}-aefdab`,
                expected: false,
            },
            {
                desc: "remote version is an empty string - outdated",
                remoteVersion: "",
                expected: true,
            },
            {
                desc: "remote version is newer by major version - not outdated",
                remoteVersion: "2.0.0",
                expected: false,
            },
            {
                desc: "remote version is newer by minor version - not outdated",
                remoteVersion: "1.3.0",
                expected: false,
            },
            {
                desc: "remote version is newer by patch version - not outdated",
                remoteVersion: "1.2.19",
                expected: false,
            },
            {
                desc: "remote version is newer but a prerelease - not outdated",
                remoteVersion: "1.3.0-SNAPSHOT",
                expected: false,
            },
            {
                desc: "remote version is newer with a suffix - not outdated",
                remoteVersion: "1.3.0+afeb102",
                expected: false,
            },
        ])("should return $expected for $desc", ({ remoteVersion, expected }) => {
            const isOutdated = ZSshUtils.checkIfOutdated(remoteVersion);
            expect(isOutdated).toBe(expected);
        });
    });

    describe("installServer / uninstallServer", () => {
        const fakeSession: ISshSession = {
            hostname: "example.com",
            port: 22,
            user: "admin",
            password: "pass",
        };

        beforeEach(() => {
            vi.spyOn(Logger, "getAppLogger").mockReturnValue({
                debug: vi.fn(),
                error: vi.fn(),
                warn: vi.fn(),
                info: vi.fn(),
                trace: vi.fn(),
            } as any);

            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 9999, stderr: "" });
        });

        it("should upload via SFTP and extract server", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(true);
            expect(fastPutMock).toHaveBeenCalledTimes(1);
            expect(unlinkMock).toHaveBeenCalledTimes(1);
            // The install is only complete once the binary is confirmed to load on the target
            expect(sshMock.execCommand).toHaveBeenCalledWith("./zo --version", { cwd: "./.zowe-server" });
        });

        describe("installServer binary verification", () => {
            const CEE3561S =
                "CEE3561S The external function _ZNSt5__1_e13__hash_memoryEPKvm was not found in DLL CRTEQCXE.";

            function mocksFailingVerify(verifyResult: { code: number; stdout?: string; stderr?: string }) {
                const sftpMock = {
                    fastPut: vi.fn((_l: string, _r: string, _o: any, cb: (err?: Error) => void) => cb()),
                    unlink: vi.fn((_p: string, cb: (err?: Error) => void) => cb()),
                };
                const sshMock = {
                    execCommand: vi.fn().mockImplementation(async (command: string) => {
                        if (command.includes("--version")) return { code: 0, stdout: "", stderr: "", ...verifyResult };
                        if (command.startsWith("uname")) return { code: 0, stdout: "OS/390 27.00 04", stderr: "" };
                        return { code: 0, stdout: "", stderr: "" };
                    }),
                };
                setupSftpMocks(sftpMock, sshMock);
                return sshMock;
            }

            it("throws ELERUNTIME when the binary cannot be loaded by Language Environment", async () => {
                mocksFailingVerify({ code: 1, stderr: CEE3561S });

                await expect(
                    ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server"),
                ).rejects.toMatchObject({
                    errorCode: "ELERUNTIME",
                    mDetails: expect.objectContaining({
                        msg: expect.stringContaining("Language Environment"),
                    }),
                });
            });

            it("includes the target system level in the details of a load failure", async () => {
                mocksFailingVerify({ code: 1, stderr: CEE3561S });

                await expect(
                    ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server"),
                ).rejects.toMatchObject({
                    mDetails: expect.objectContaining({
                        additionalDetails: expect.stringContaining("OS/390 27.00 04"),
                    }),
                });
            });

            it("routes a load failure through onError with the verify context", async () => {
                mocksFailingVerify({ code: 1, stderr: CEE3561S });
                const onError = vi.fn().mockResolvedValue(false);

                const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", {
                    onError,
                });

                expect(result).toBe(false);
                expect(onError).toHaveBeenCalledWith(expect.objectContaining({ errorCode: "ELERUNTIME" }), "verify");
            });

            it("reports EDEPLOYFAIL when the binary fails for a reason unrelated to the runtime", async () => {
                mocksFailingVerify({ code: 126, stderr: "EDC5111I Permission denied." });

                await expect(
                    ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server"),
                ).rejects.toMatchObject({ errorCode: "EDEPLOYFAIL" });
            });
        });

        it("should report progress during SFTP upload", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, opts: any, cb: (err?: Error) => void) => {
                if (opts?.step) {
                    opts.step(50, 50, 100);
                    opts.step(100, 50, 100);
                }
                cb();
            });
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const progressMock = vi.fn();
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", {
                onProgress: progressMock,
            });
            expect(result).toBe(true);
            expect(progressMock).toHaveBeenCalled();
            const totalProgress = progressMock.mock.calls.reduce((sum: number, call: number[]) => sum + call[0], 0);
            expect(totalProgress).toBe(100);
        });

        it("should return false when SFTP upload fails and no onError callback", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => {
                const err = new Error("upload failed");
                (err as any).code = "PERMISSION_DENIED";
                cb(err);
            });
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(false);
        });

        it("should pass a user-friendly message to onError when a step fails", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) =>
                cb(new Error("disk full")),
            );
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
            const [err] = onError.mock.calls[0] as [{ message: string; details: { additionalDetails: string } }];
            expect(err.message).toBe("Failed to upload the server binary to the remote system.");
            expect(err.details.additionalDetails).toContain("disk full");
        });

        it("should throw ImperativeError when password is expired (FOTS1668)", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const sftpMock = { fastPut: vi.fn() };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: expiredStderr, stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            await expect(ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server")).rejects.toMatchObject({
                message: SshErrors.FOTS1668.summary,
                errorCode: "EPASSWD_EXPIRED",
            });
        });

        it("should call onError with FOTS1668 summary when password is expired and onError is provided", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const sftpMock = { fastPut: vi.fn() };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: expiredStderr, stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
            const [err, context] = onError.mock.calls[0] as [{ message: string; errorCode: string }, string];
            expect(err.message).toBe(SshErrors.FOTS1668.summary);
            expect(err.errorCode).toBe("EPASSWD_EXPIRED");
            expect(context).toBe("deploy");
        });

        it("should throw ImperativeError on uninstall when password is expired", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const sftpMock = {};
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: expiredStderr, stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock, { mockGetBinDir: false, mockExistsSync: false });

            await expect(
                ZSshUtils.uninstallServer(new SshSession(fakeSession), "~/.zowe-server"),
            ).rejects.toMatchObject({
                message: SshErrors.FOTS1668.summary,
                errorCode: "EPASSWD_EXPIRED",
            });
        });

        it("should return false when mkdir fails and no onError callback is provided", async () => {
            const sftpMock = {};
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: "permission denied", stdout: "" }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(false);
        });

        it("should call onError when mkdir fails and handle retry if onError returns true", async () => {
            const sftpMock = { fastPut: vi.fn((_l, _r, _o, cb) => cb()), unlink: vi.fn((_p, cb) => cb()) };
            const sshMock = {
                execCommand: vi
                    .fn()
                    .mockResolvedValueOnce({ code: 0, stderr: "", stdout: "" })
                    .mockResolvedValueOnce({ code: 1, stderr: "temporary error", stdout: "" })
                    .mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(true);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(true);
            expect(onError).toHaveBeenCalledOnce();
            // last time is the -v check
            expect(sshMock.execCommand).toHaveBeenCalledTimes(6);
        });

        it("should call onError when mkdir fails and return false if onError returns false", async () => {
            const sftpMock = {};
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: "permanent error", stdout: "" }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should call onError when unlink fails during cleanup", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb(new Error("unlink failed")));
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(true); // cleanup is non-fatal for installServer
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should log warning when unlink fails and no onError callback is provided", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb(new Error("unlink failed")));
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(true);
        });

        it("should throw rmErr when rm -rf fails during uninstall and no onError callback is provided", async () => {
            const sftpMock = {};
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: "rm failed", stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock, { mockGetBinDir: false, mockExistsSync: false });

            await expect(ZSshUtils.uninstallServer(new SshSession(fakeSession), "~/.zowe-server")).rejects.toThrow(
                "Failed to uninstall the server from the remote system.",
            );
        });

        it("should call onError when rm -rf fails during uninstall and handle retry if onError returns true", async () => {
            const sftpMock = {};
            const sshMock = {
                execCommand: vi
                    .fn()
                    .mockResolvedValueOnce({ code: 1, stderr: "temporary error", stdout: "" })
                    .mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            setupSftpMocks(sftpMock, sshMock, { mockGetBinDir: false, mockExistsSync: false });

            const onError = vi.fn().mockResolvedValue(true);
            await ZSshUtils.uninstallServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(onError).toHaveBeenCalledOnce();
            expect(sshMock.execCommand).toHaveBeenCalledTimes(2);
        });

        it("should call onError when fastPut fails and handle retry if onError returns true", async () => {
            const fastPutMock = vi
                .fn()
                .mockImplementationOnce((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => {
                    cb(new Error("fastPut temp error"));
                })
                .mockImplementation((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => {
                    cb();
                });
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(true);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(true);
            expect(onError).toHaveBeenCalledOnce();
            expect(fastPutMock).toHaveBeenCalledTimes(2);
        });

        it("should return false when pax extraction fails and no onError callback is provided", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = {
                execCommand: vi.fn().mockImplementation(async (cmd) => {
                    if (cmd.startsWith("pax")) {
                        return { code: 1, stderr: "pax failed", stdout: "" };
                    }
                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(false);
        });

        it("should call onError when pax extraction fails and return false if onError returns false", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = {
                execCommand: vi.fn().mockImplementation(async (cmd) => {
                    if (cmd.startsWith("pax")) {
                        return { code: 1, stderr: "pax failed", stdout: "" };
                    }
                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should call onError when pax extraction fails and continue if onError returns true", async () => {
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            const sshMock = {
                execCommand: vi.fn().mockImplementation(async (cmd) => {
                    if (cmd.startsWith("pax")) {
                        return { code: 1, stderr: "pax failed", stdout: "" };
                    }
                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(true);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(true);
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should throw ImperativeError when password is expired during SFTP upload (fastPut)", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) =>
                cb(new Error(expiredStderr)),
            );
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            await expect(ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server")).rejects.toMatchObject({
                message: SshErrors.FOTS1668.summary,
                errorCode: "EPASSWD_EXPIRED",
            });
        });

        it("should return false when password is expired during upload and onError is provided", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) =>
                cb(new Error(expiredStderr)),
            );
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 0, stdout: "", stderr: "" }) };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should throw ImperativeError when password is expired during PAX extraction", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = {
                execCommand: vi.fn().mockImplementation(async (cmd) => {
                    if (cmd.startsWith("pax")) {
                        return { code: 1, stderr: expiredStderr, stdout: "" };
                    }
                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            setupSftpMocks(sftpMock, sshMock);

            await expect(ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server")).rejects.toMatchObject({
                message: SshErrors.FOTS1668.summary,
                errorCode: "EPASSWD_EXPIRED",
            });
        });

        it("should return false when password is expired during PAX extraction and onError is provided", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock };
            const sshMock = {
                execCommand: vi.fn().mockImplementation(async (cmd) => {
                    if (cmd.startsWith("pax")) {
                        return { code: 1, stderr: expiredStderr, stdout: "" };
                    }
                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            setupSftpMocks(sftpMock, sshMock);

            const onError = vi.fn().mockResolvedValue(false);
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(result).toBe(false);
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should abort on uninstall when password is expired and routeExpiredPasswordError handles it with onError returning void", async () => {
            const expiredStderr =
                "FOTS1668 WARNING: Your password has expired.\nFOTS1669 Password change required but no TTY available.";
            const sftpMock = {};
            const sshMock = { execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: expiredStderr, stdout: "" }) };
            setupSftpMocks(sftpMock, sshMock, { mockGetBinDir: false, mockExistsSync: false });

            const onError = vi.fn().mockResolvedValue(true);
            await ZSshUtils.uninstallServer(new SshSession(fakeSession), "~/.zowe-server", { onError });
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should call onError when rm -rf fails during uninstall and throw rmErr if onError returns false", async () => {
            const sftpMock = {};
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 1, stderr: "permanent error", stdout: "" }),
            };
            setupSftpMocks(sftpMock, sshMock, { mockGetBinDir: false, mockExistsSync: false });

            const onError = vi.fn().mockResolvedValue(false);
            await expect(
                ZSshUtils.uninstallServer(new SshSession(fakeSession), "~/.zowe-server", { onError }),
            ).rejects.toThrow("Failed to uninstall the server from the remote system.");
            expect(onError).toHaveBeenCalledOnce();
        });

        it("should execute real sftp helper method successfully", async () => {
            // Re-mock fs.existsSync and Logger
            vi.spyOn(fs, "existsSync").mockReturnValue(true);
            vi.spyOn(Logger, "getAppLogger").mockReturnValue({
                debug: vi.fn(),
                error: vi.fn(),
                warn: vi.fn(),
                info: vi.fn(),
                trace: vi.fn(),
            } as any);

            // We can spy on NodeSSH.prototype.connect and requestSFTP
            const connectSpy = vi.spyOn(NodeSSH.prototype, "connect").mockResolvedValue({} as any);
            const requestSftpSpy = vi.spyOn(NodeSSH.prototype, "requestSFTP").mockResolvedValue({
                fastPut: vi.fn((_l, _r, _o, cb) => cb()),
                unlink: vi.fn((_p, cb) => cb()),
            } as any);
            const execCommandSpy = vi.spyOn(NodeSSH.prototype, "execCommand").mockResolvedValue({
                code: 0,
                stdout: "",
                stderr: "",
            });
            const disposeSpy = vi.spyOn(NodeSSH.prototype, "dispose").mockImplementation(() => {});

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server");
            expect(result).toBe(true);
            expect(connectSpy).toHaveBeenCalled();
            expect(requestSftpSpy).toHaveBeenCalled();
            expect(execCommandSpy).toHaveBeenCalled();
            expect(disposeSpy).toHaveBeenCalled();
        });

        it("should cancel the deployment if insufficient space detected and the onInsufficientSpaceWarning callback returns false", async () => {
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 1, stderr: "" });
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", {
                onInsufficientSpaceWarning: async () => {
                    return false;
                },
            });
            expect(result).toBe(false);
            expect(fastPutMock).not.toHaveBeenCalled();
        });

        it("should continue the deployment if insufficient space detected and the onInsufficientSpaceWarning callback is not provided", async () => {
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 1, stderr: "" });
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", {
                onInsufficientSpaceWarning: undefined,
            });
            expect(result).toBe(true);
            expect(fastPutMock).toHaveBeenCalled();
        });

        it("should continue the deployment if insufficient space detected and the onInsufficientSpaceWarning callback returns true", async () => {
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 1, stderr: "" });
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            setupSftpMocks(sftpMock, sshMock);

            const result = await ZSshUtils.installServer(new SshSession(fakeSession), "~/.zowe-server", {
                onInsufficientSpaceWarning: async () => {
                    return true;
                },
            });
            expect(result).toBe(true);
            expect(fastPutMock).toHaveBeenCalled();
        });

        it("should attempt post-failure cleanup if a step of the deployment throws an error", async () => {
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const rmdirMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = {
                fastPut: fastPutMock,
                unlink: unlinkMock,
                rmdir: rmdirMock,
            };
            setupSftpMocks(sftpMock, sshMock);
            const expectedDeployDir = "/my/subdir/";
            // if the deploy dir did not exist before we started deploying, post-failure cleanup should try to delete it
            vi.spyOn(ZSshUtils, "getAvailableMb").mockImplementation(async (_ssh, _dir) => {
                throw new Error("Eek!");
            });
            // only the first call should return false  so that when we do the cleanup, we
            // simulate having created the deployment directory
            vi.spyOn(ZSshUtils, "pathExists")
                .mockResolvedValueOnce({ exists: false, stderr: "" })
                .mockResolvedValue({ exists: true, stderr: "" });
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), expectedDeployDir, {});
            expect(result).toBe(false);
            expect(fastPutMock).not.toHaveBeenCalled();
            expect(rmdirMock).toHaveBeenCalledWith(expectedDeployDir, expect.anything());
            // we should only attempt to delete the pax, not the binary
            expect(unlinkMock).toHaveBeenCalledTimes(1);
            expect(unlinkMock).toHaveBeenCalledWith(`${expectedDeployDir}server.pax.Z`, expect.anything());
        });

        it("should NOT attempt post-failure cleanup if a step of the deployment throws a password expired error", async () => {
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({ code: 0, stderr: "", stdout: "" }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = { fastPut: fastPutMock, unlink: unlinkMock };
            setupSftpMocks(sftpMock, sshMock);
            const expectedDeployDir = "/my/subdir/";
            // if the deploy dir did not exist before we started deploying, post-failure cleanup should try to delete it
            const passwordErr = new ImperativeError({ msg: "bad pass", errorCode: "EPASSWD_EXPIRED" });
            vi.spyOn(ZSshUtils, "getAvailableMb").mockImplementation(async (_ssh, _dir) => {
                throw passwordErr;
            });
            vi.spyOn(ZSshUtils, "pathExists")
                .mockResolvedValueOnce({ exists: false, stderr: "" })
                .mockResolvedValue({ exists: true, stderr: "" });
            await expect(ZSshUtils.installServer(new SshSession(fakeSession), expectedDeployDir, {})).rejects.toThrow(
                passwordErr,
            );
        });

        it("should attempt to delete the server binary if deployment fails after we started the extraction", async () => {
            const sshMock = {
                execCommand: vi.fn().mockImplementation((cmd, _opts) => {
                    if (cmd.indexOf("pax ") >= 0) {
                        return { code: 8, stderr: "Unable to extract pax.", stdout: "" };
                    }

                    return { code: 0, stderr: "", stdout: "" };
                }),
            };
            const fastPutMock = vi.fn((_local: string, _remote: string, _opts: any, cb: (err?: Error) => void) => cb());
            const rmdirMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const unlinkMock = vi.fn((_path: string, cb: (err?: Error) => void) => cb());
            const sftpMock = {
                fastPut: fastPutMock,
                unlink: unlinkMock,
                rmdir: rmdirMock,
            };
            setupSftpMocks(sftpMock, sshMock);
            const expectedDeployDir = "/my/subdir/";
            vi.spyOn(ZSshUtils, "getAvailableMb").mockResolvedValue({ mb: 9001, stderr: "" });

            vi.spyOn(ZSshUtils, "pathExists")
                .mockResolvedValueOnce({ exists: false, stderr: "" })
                .mockResolvedValue({ exists: true, stderr: "" })
                .mockResolvedValue({ exists: true, stderr: "" }); // post-failure binary exists check
            const result = await ZSshUtils.installServer(new SshSession(fakeSession), expectedDeployDir, {});
            expect(result).toBe(false);
            expect(fastPutMock).toHaveBeenCalled();
            expect(rmdirMock).toHaveBeenCalledWith(expectedDeployDir, expect.anything());
            expect(unlinkMock).toHaveBeenCalledTimes(2); // to delete the pax & the binary
        });
    });

    describe("getAvailableMb", () => {
        it("should parse a properly formatted 80 MB available correctly", async () => {
            const expectedMb = 80;
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({
                    code: 0,
                    stderr: "",
                    stdout:
                        " Mounted on     Filesystem                Avail/Total    Files      Status\n" +
                        `/u/users       (EXAMPLE.USER.ZFS)        ${expectedMb}/8120160 4294919164 Available`,
                }),
            };
            const result = await ZSshUtils.getAvailableMb(sshMock as NodeSSH, "/u/users");
            expect(result.mb).toEqual(expectedMb);
            expect(result.stderr).toEqual("");
        });
        it("should parse a properly formatted 80 MB available correctly even if the USS directory contains spaces and parens", async () => {
            const expectedMb = 80;
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({
                    code: 0,
                    stderr: "",
                    stdout:
                        " Mounted on     Filesystem                Avail/Total    Files      Status\n" +
                        `/u/users/space man/my (excellent) dir       (EXAMPLE.USER.ZFS)        ${expectedMb}/8120160 4294919164 Available`,
                }),
            };
            const result = await ZSshUtils.getAvailableMb(sshMock as NodeSSH, "/u/users");
            expect(result.mb).toEqual(expectedMb);
            expect(result.stderr).toEqual("");
        });
        it("should parse a properly formatted 40 MB available correctly even if the headers are in another language", async () => {
            const expectedMb = 40;
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({
                    code: 0,
                    stderr: "",
                    stdout:
                        " Directorio montado en     Sistema de archivos                Avail/Total    Archivos      Estado\n" +
                        `/u/users       (EXAMPLE.USER.ZFS)        ${expectedMb}/73828 4294919164 Available`,
                }),
            };
            const result = await ZSshUtils.getAvailableMb(sshMock as NodeSSH, "/u/users");
            expect(result.mb).toEqual(expectedMb);
            expect(result.stderr).toEqual("");
        });

        it("should return -1 MB if the df command fails to execute", async () => {
            const expectedErr = "Bonk!";
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({
                    code: 1,
                    stderr: expectedErr,
                    stdout: "",
                }),
            };
            const result = await ZSshUtils.getAvailableMb(sshMock as NodeSSH, "/u/users");
            expect(result.mb).toEqual(-1);
            expect(result.stderr).toEqual(expectedErr);
        });
        it("should return -1 MB if the df command returns an unknown format", async () => {
            const expectedErr = "Bonk!";
            const sshMock = {
                execCommand: vi.fn().mockResolvedValue({
                    code: 0,
                    stderr: expectedErr,
                    stdout: "Someone made a fake df command. Eek!",
                }),
            };
            const result = await ZSshUtils.getAvailableMb(sshMock as NodeSSH, "/u/users");
            expect(result.mb).toEqual(-1);
            expect(result.stderr).toEqual(expectedErr);
        });
    });
    describe("buildSession", () => {
        it("should preserve identityAgent when building a session from a profile", () => {
            const session = ZSshUtils.buildSession({
                host: "example.com",
                user: "testuser",
                identityAgent: "SSH_AUTH_SOCK",
            });

            expect(session.ISshSession.identityAgent).toBe("SSH_AUTH_SOCK");
        });

        it("should build session with password when private key is not provided", () => {
            const profile = {
                host: "example.com",
                port: 2222,
                user: "testuser",
                password: "mypassword",
            };
            const session = ZSshUtils.buildSession(profile);
            expect(session.ISshSession.hostname).toBe("example.com");
            expect(session.ISshSession.port).toBe(2222);
            expect(session.ISshSession.user).toBe("testuser");
            expect(session.ISshSession.password).toBe("mypassword");
            expect(session.ISshSession.privateKey).toBeUndefined();
        });

        it("should build session with private key and passphrase, and undefined password", () => {
            const profile = {
                host: "example.com",
                user: "testuser",
                privateKey: "/path/to/key",
                keyPassphrase: "passphrase",
                password: "mypassword", // should be ignored
            };
            const session = ZSshUtils.buildSession(profile);
            expect(session.ISshSession.hostname).toBe("example.com");
            expect(session.ISshSession.port).toBe(22); // default
            expect(session.ISshSession.user).toBe("testuser");
            expect(session.ISshSession.privateKey).toBe("/path/to/key");
            expect(session.ISshSession.keyPassphrase).toBe("passphrase");
            expect(session.ISshSession.password).toBeUndefined();
        });
    });

    describe("buildSshConfig", () => {
        it("should build basic config without private key", () => {
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
                password: "mypassword",
            });
            const config = ZSshUtils.buildSshConfig(session);
            expect(config.host).toBe("example.com");
            expect(config.port).toBe(22);
            expect(config.username).toBe("testuser");
            expect(config.password).toBe("mypassword");
            expect(config.privateKey).toBeUndefined();
        });

        it("should read private key file and build config", () => {
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
                privateKey: "/path/to/key",
                keyPassphrase: "passphrase",
            });
            const readFileSyncSpy = vi.spyOn(fs, "readFileSync").mockReturnValue("key-content");
            const config = ZSshUtils.buildSshConfig(session);
            expect(config.privateKey).toBe("key-content");
            expect(config.passphrase).toBe("passphrase");
            expect(readFileSyncSpy).toHaveBeenCalledWith("/path/to/key", "utf-8");
        });

        it("should resolve the current SSH_AUTH_SOCK without changing the session", () => {
            const session = new SshSession({
                hostname: "example.com",
                user: "testuser",
                identityAgent: "SSH_AUTH_SOCK",
                password: "mypassword",
                privateKey: "/path/to/key",
                keyPassphrase: "passphrase",
            });
            const readFileSyncSpy = vi.spyOn(fs, "readFileSync").mockReturnValue("key-content");
            vi.stubEnv("SSH_AUTH_SOCK", "/tmp/first-agent.sock");

            try {
                const config = ZSshUtils.buildSshConfig(session);
                expect(config.agent).toBe("/tmp/first-agent.sock");
                expect(config.password).toBeUndefined();
                expect(config.privateKey).toBeUndefined();
                expect(config.passphrase).toBeUndefined();
                expect(readFileSyncSpy).not.toHaveBeenCalled();

                vi.stubEnv("SSH_AUTH_SOCK", "/tmp/current-agent.sock");
                expect(ZSshUtils.buildSshConfig(session).agent).toBe("/tmp/current-agent.sock");

                vi.stubEnv("SSH_AUTH_SOCK", undefined);
                const fallbackConfig = ZSshUtils.buildSshConfig(session);
                expect(fallbackConfig.agent).toBeUndefined();
                expect(fallbackConfig.privateKey).toBe("key-content");
                expect(fallbackConfig.passphrase).toBe("passphrase");
                expect(fallbackConfig.password).toBe("mypassword");
                expect(readFileSyncSpy).toHaveBeenCalledExactlyOnceWith("/path/to/key", "utf-8");
                expect(session.ISshSession.identityAgent).toBe("SSH_AUTH_SOCK");
            } finally {
                vi.unstubAllEnvs();
            }
        });

        it("should use the configured password when SSH_AUTH_SOCK is unset", () => {
            const session = new SshSession({
                hostname: "example.com",
                user: "testuser",
                identityAgent: "SSH_AUTH_SOCK",
                password: "mypassword",
            });
            vi.stubEnv("SSH_AUTH_SOCK", undefined);

            try {
                const config = ZSshUtils.buildSshConfig(session);
                expect(config.agent).toBeUndefined();
                expect(config.password).toBe("mypassword");
                expect(session.ISshSession.identityAgent).toBe("SSH_AUTH_SOCK");
            } finally {
                vi.unstubAllEnvs();
            }
        });

        it("should merge configProps", () => {
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const config = ZSshUtils.buildSshConfig(session, { readyTimeout: 5000, keepaliveInterval: 1000 });
            expect(config.readyTimeout).toBe(5000);
            expect(config.keepaliveInterval).toBe(1000);
        });

        it("should trace log in debug callback", () => {
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const traceMock = vi.fn();
            vi.spyOn(Logger, "getAppLogger").mockReturnValue({
                trace: traceMock,
            } as any);

            const config = ZSshUtils.buildSshConfig(session);
            expect(config.debug).toBeDefined();
            config.debug!("test debug message");
            expect(traceMock).toHaveBeenCalledWith("test debug message");
        });
    });

    describe("isPrivateKeyAuthFailure", () => {
        it("should return false if hasPrivateKey is false", () => {
            const result = ZSshUtils.isPrivateKeyAuthFailure("Permission denied (publickey)", false);
            expect(result).toBe(false);
        });

        it("should return true for private key failure patterns", () => {
            const result = ZSshUtils.isPrivateKeyAuthFailure("Permission denied (publickey)");
            expect(result).toBe(true);
        });

        it("should return false for other error messages", () => {
            const result = ZSshUtils.isPrivateKeyAuthFailure("Connection timed out");
            expect(result).toBe(false);
        });
    });

    describe("getBinDir error path", () => {
        it("should throw error when bin directory is not found", () => {
            vi.spyOn(fs, "existsSync").mockReturnValue(false);
            expect(() => (ZSshUtils as any).getBinDir("/")).toThrow("Failed to find bin directory in path");
        });
    });

    describe("detectServerOnPath", () => {
        const mockDetectServerSSH = (
            expectedServerPathOutput: string,
            expectedVersion: string | undefined,
            shouldHaveExecuteAccess: boolean,
            shellShouldError: boolean = false,
            expectedShellError: Error | undefined = undefined,
        ) => {
            let exitEventHandler: () => void;
            let errorEventHandler: (err: Error) => void;
            let dataEventHandler: (strOrBuf: string | Buffer) => void;
            const mockChannel = vi.mockObject({
                on: vi.fn().mockImplementation((eventName: string, eventHandler) => {
                    if (eventName === "exit") {
                        exitEventHandler = eventHandler;
                    } else if (eventName === "data") {
                        dataEventHandler = eventHandler;
                    } else if (eventName === "error") {
                        errorEventHandler = eventHandler;
                    }
                }),
                write: vi.fn().mockImplementation((_writeData: string) => {}),
                end: vi.fn(),
                closed: false,
                stdout: { readableEnded: false },
            });

            const sshMock = {
                execCommand: vi.fn().mockImplementation((cmd) => {
                    if (cmd.indexOf("-v") > 0) {
                        return shouldHaveExecuteAccess
                            ? { code: 0, stderr: "", stdout: expectedVersion }
                            : { code: 1, stderr: "No such command, buddy", stdout: "" };
                    }

                    throw new Error("Unknown command passed to execCommand");
                }),
                withShell: vi.fn().mockImplementation(async (shellCallback) => {
                    const callbackPromise = shellCallback(mockChannel);
                    dataEventHandler(`user@host$ blahblah\n${expectedServerPathOutput} \n`);
                    if (shellShouldError) {
                        errorEventHandler(expectedShellError);
                    }
                    if (exitEventHandler) {
                        exitEventHandler();
                    }

                    await callbackPromise;
                }),
            };
            vi.spyOn(ZSshUtils as any, "withSsh").mockImplementation(
                async (_session: any, callback: (ssh: any) => Promise<any>) => {
                    return await callback(sshMock);
                },
            );
        };
        it("Should return hasExecutePermission=true and version number if zowex command succeeds", async () => {
            const expectedServerPath = `/u/users/user/mybins/${ZSshClient.BIN_NAME}`;
            const expectedVersion = "1.0.0-aefbab";
            mockDetectServerSSH(expectedServerPath, expectedVersion, true);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const detectResult = await ZSshUtils.detectServerOnPath(session);
            expect(detectResult).toBeDefined();
            expect(detectResult.serverPath).toEqual(expectedServerPath);
            expect(detectResult.hasExecutePermission).toEqual(true);
            expect(detectResult.version).toEqual(expectedVersion);
        });
        it("Should return hasExecutePermission=false if zowex command fails", async () => {
            const expectedServerPath = `/u/users/user/mybins/${ZSshClient.BIN_NAME}`;
            mockDetectServerSSH(expectedServerPath, undefined, false);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const detectResult = await ZSshUtils.detectServerOnPath(session);
            expect(detectResult).toBeDefined();
            expect(detectResult.serverPath).toEqual(expectedServerPath);
            expect(detectResult.hasExecutePermission).toEqual(false);
            expect(detectResult.version).toBeUndefined();
        });
        it("Should detect binary not found in output of shell command", async () => {
            mockDetectServerSSH("Zowe binary not found", undefined, false);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const detectResult = await ZSshUtils.detectServerOnPath(session);
            expect(detectResult).toBeDefined();
            expect(detectResult.serverPath).toBeUndefined();
            expect(detectResult.hasExecutePermission).toEqual(false);
            expect(detectResult.version).toBeUndefined();
        });
        it("Should handle unexpected error message in output of shell command", async () => {
            mockDetectServerSSH("Eek! Some kinda error that's unexpected", undefined, false);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const detectResult = await ZSshUtils.detectServerOnPath(session);
            expect(detectResult).toBeDefined();
            expect(detectResult.serverPath).toBeUndefined();
            expect(detectResult.hasExecutePermission).toEqual(false);
            expect(detectResult.version).toBeUndefined();
        });
        it("Should reject if the shell throws an error via the on 'error' event handler", async () => {
            const expectedError = new Error("The shell is bad and it failed");
            mockDetectServerSSH("", undefined, false, true, expectedError);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            await expect(ZSshUtils.detectServerOnPath(session)).rejects.toEqual(expectedError);
        });
    });

    describe("lacksWriteAccess", () => {
        const mockLacksWriteAccessSsh = (shouldExist: boolean, shouldHaveWriteAccess: boolean) => {
            const sshMock = {
                execCommand: vi.fn().mockImplementation((cmd) => {
                    if (cmd.indexOf("-e") > 0) {
                        return shouldExist ? { code: 0, stderr: "", stdout: "" } : { code: 1, stderr: "", stdout: "" };
                    } else if (cmd.indexOf("-w") > 0) {
                        return shouldHaveWriteAccess
                            ? { code: 0, stderr: "", stdout: "" }
                            : { code: 1, stderr: "", stdout: "" };
                    }

                    throw new Error("Unknown command passed to execCommand");
                }),
            };
            vi.spyOn(ZSshUtils as any, "withSsh").mockImplementation(
                async (_session: any, callback: (ssh: any) => Promise<any>) => {
                    return await callback(sshMock);
                },
            );
        };

        it("Should return true if the path does exist but the user does NOT have write access to it", async () => {
            mockLacksWriteAccessSsh(true, false);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const lacksAccess = await ZSshUtils.lacksWriteAccess(session, "fakePath");
            expect(lacksAccess).toEqual(true);
        });
        it("Should return false if the path does exist but the user does INDEED have write access to it", async () => {
            mockLacksWriteAccessSsh(true, true);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const lacksAccess = await ZSshUtils.lacksWriteAccess(session, "fakePath");
            expect(lacksAccess).toEqual(false);
        });
        it("Should return false if the path does NOT exist even if (for some reason) write access check returns 0", async () => {
            mockLacksWriteAccessSsh(false, true);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const lacksAccess = await ZSshUtils.lacksWriteAccess(session, "fakePath");
            expect(lacksAccess).toEqual(false);
        });
        it("Should return false if the path does NOT exist and the write access command also fails", async () => {
            // since if the path does not exist, we don't know whether the user can create the directory without
            // additional checks
            mockLacksWriteAccessSsh(false, false);
            const session = new SshSession({
                hostname: "example.com",
                port: 22,
                user: "testuser",
            });
            const lacksAccess = await ZSshUtils.lacksWriteAccess(session, "fakePath");
            expect(lacksAccess).toEqual(false);
        });
    });
});
