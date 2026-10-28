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
import * as path from "node:path";
import { promisify } from "node:util";
import { ImperativeError, type IProfile, Logger } from "@zowe/imperative";
import { type ISshSession, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import type { NodeSSH, SSHExecCommandResponse } from "node-ssh";
import * as semver from "semver";
import type { ConnectConfig, SFTPWrapper } from "ssh2";
import { matchLeRuntimeFailure, PrivateKeyFailurePatterns, SshErrors } from "./SshErrors";
import { SessionContext } from "./utils";
import { ZSshClient } from "./ZSshClient";
import { BUNDLED_SSH_SERVER_VERSION } from "./ZSshConstants";

export interface ISshCallbacks {
    onProgress?: (increment: number) => void; // Callback to report incremental progress
    onError?: (error: Error, context: string) => Promise<boolean>; // Callback to handle errors, returns true to continue/retry
    /**
     * Callback to ask if the user wants to proceed with deployment even when they appear.
     * If "available" is -1, it means we were not able to parse the output of the df command
     * and do not know the amount of available space.
     * @returns true if we should attempt deployment anyway.
     *
     * If this callback is not provided, the installServer method will continue even if insufficient space
     * is detected or if we are unable to detect the amount of available space.
     */
    onInsufficientSpaceWarning?: (available: number, recommended: number) => Promise<boolean>;
}

export interface IServerOnPathDetails {
    /**
     * The absolute path of the zo instance on the user's path, if any was detected.
     */
    serverPath?: string;
    /**
     * If serverPath is defined, this will be true if the user is able to execute the program
     * found at serverPath.
     */
    hasExecutePermission: boolean;
    /**
     * If serverPath is defined, this will be the version reported by the output of the
     * server binary's -v flag.
     */
    version?: string;
}
type SftpError = Error & { code?: number };

export interface PathExistsResponse {
    exists: boolean;
    stderr: string;
}

export interface AvailableMBResponse {
    /**
     * The number of available megabytes detected at the mount point of the provided path.
     * is -1, it means we were not able to parse the output of the df command
     * and do not know the amount of available space.
     */
    mb: number;
    /**
     * Any standard error output produced when executing the df command to detect available space.
     */
    stderr: string;
}

// biome-ignore lint/complexity/noStaticOnlyClass: Utilities class has static methods
export class ZSshUtils {
    private static readonly SERVER_PAX_FILE = "server.pax.Z";
    private static readonly EXPIRED_PASSWORD_CODES = ["FOTS1668", "FOTS1669"];

    /**
     * Routes an expired-password error through `onError` when a callback is registered,
     * or throws an `ImperativeError` directly as a fallback (preserving CLI behavior).
     * Returns `true` if the text contained an expired-password indicator (i.e. the caller
     * should abort the current operation), `false` otherwise.
     */
    private static async routeExpiredPasswordError(
        text: string,
        context: string,
        options?: Pick<ISshCallbacks, "onError">,
    ): Promise<boolean> {
        if (!ZSshUtils.EXPIRED_PASSWORD_CODES.some((code) => text.includes(code))) {
            return false;
        }
        const err = new ImperativeError({
            msg: SshErrors.FOTS1668.summary,
            errorCode: "EPASSWD_EXPIRED",
            additionalDetails: text,
        });
        if (options?.onError) {
            await options.onError(err, context);
            return true;
        }
        throw err;
    }

    /**
     * Remove unsafe characters from a path to avoid injection attacks when
     * From the MIT-licensed node-shlex.
     * https://github.com/rgov/node-shlex
     */
    public static quotePath(path: string): string {
        if (path === "") {
            return "''";
        }

        const unsafeRe = /[^\w@%\-+=:,./]/;
        if (!unsafeRe.test(path)) {
            return path;
        }

        return `'${path.replace(/('+)/g, "'\"$1\"'")}'`.replace(/^''|''$/g, "");
    }

    /**
     * Runs the freshly installed server binary to confirm z/OS can load it.
     *
     * A Language Environment mismatch (e.g. CEE3561S) is a *load-time* failure: the binary never
     * reaches `main()`, so it can never report the problem itself. Without this check the first
     * symptom is a confusing failure on the user's next operation, long after the install reported
     * success.
     *
     * @returns `undefined` when the binary runs, or an `ImperativeError` describing why it does not.
     */
    private static async verifyServerBinary(ssh: NodeSSH, remoteDir: string): Promise<ImperativeError | undefined> {
        const bin = `./${ZSshClient.BIN_NAME}`;
        const result = await ssh.execCommand(`${bin} --version`, { cwd: remoteDir });
        if (result.code === 0) {
            Logger.getAppLogger().info(`[ZSshUtils] Server binary verified: ${result.stdout.trim()}`);
            return undefined;
        }

        const output = `${result.stdout ?? ""}\n${result.stderr ?? ""}`.trim();
        // Raw uname output, deliberately not parsed into a marketing release name: the mapping needs
        // verification per system, and an unparsed string is more useful in a bug report than a
        // wrong one.
        let systemInfo = "";
        try {
            const uname = await ssh.execCommand("uname -srv");
            systemInfo = `${uname.stdout ?? ""}${uname.stderr ?? ""}`.trim();
        } catch (err) {
            Logger.getAppLogger().debug(`[ZSshUtils] Could not read the target system level: ${err.message}`);
        }

        const details = [`${bin} --version RC=${result.code}: ${output}`, systemInfo && `System: ${systemInfo}`]
            .filter(Boolean)
            .join("\n");
        const leFailure = matchLeRuntimeFailure(output);
        return new ImperativeError({
            msg: leFailure ?? "The server was installed but the server binary could not be run on the remote system.",
            errorCode: leFailure != null ? "ELERUNTIME" : "EDEPLOYFAIL",
            additionalDetails: details,
        });
    }

    /**
     * Checks if an error message indicates a private key authentication failure
     * @param errorMessage The error message to check
     * @param hasPrivateKey Optional flag to indicate if a private key is configured (for more accurate detection)
     * @returns True if the error indicates a private key authentication failure
     */
    public static isPrivateKeyAuthFailure(errorMessage: string, hasPrivateKey?: boolean): boolean {
        // If no private key is configured, this can't be a private key failure
        if (hasPrivateKey === false) {
            return false;
        }

        return PrivateKeyFailurePatterns.some((pattern) => errorMessage.includes(pattern));
    }

    public static buildSession(args: IProfile): SshSession {
        const sshSessCfg: ISshSession = {
            hostname: args.host,
            port: args.port ?? 22,
            user: args.user,
            privateKey: args.privateKey,
            keyPassphrase: args.privateKey ? args.keyPassphrase : undefined,
            password: args.privateKey ? undefined : args.password,
            handshakeTimeout: args.handshakeTimeout,
            identityAgent: args.identityAgent,
        };
        return new SshSession(sshSessCfg);
    }

    public static buildSshConfig(session: SshSession, configProps?: ConnectConfig): ConnectConfig {
        const resolvedAgent =
            session.ISshSession.identityAgent === "SSH_AUTH_SOCK"
                ? process.env.SSH_AUTH_SOCK
                : session.ISshSession.identityAgent;
        return {
            host: session.ISshSession.hostname,
            port: session.ISshSession.port,
            username: session.ISshSession.user,
            password: resolvedAgent ? undefined : session.ISshSession.password,
            privateKey:
                !resolvedAgent && session.ISshSession.privateKey
                    ? fs.readFileSync(session.ISshSession.privateKey, "utf-8")
                    : undefined,
            passphrase: resolvedAgent ? undefined : session.ISshSession.keyPassphrase,
            readyTimeout: session.ISshSession.handshakeTimeout,
            agent: resolvedAgent,
            // ssh2 debug messages are extremely verbose so log at TRACE level
            debug: (msg) => Logger.getAppLogger().trace(msg),
            ...configProps,
        };
    }

    /**
     * Check whether a remote file or directory exists.
     * @param ssh Existing ssh connection
     * @param testPath the path to test for existence
     * @returns An object with the stderr of the exists check (you can check for password expired errors)
     * and a boolean of whether the path exists.
     */
    public static async pathExists(ssh: NodeSSH, testPath: string): Promise<PathExistsResponse> {
        const testExistsCmd = await ssh.execCommand(`test -e ${ZSshUtils.quotePath(testPath)}`);
        Logger.getAppLogger().debug(
            `[ZSshUtils] test -e %s, code %d, stdout: '%s', stderr: '%s'`,
            testPath,
            testExistsCmd.code,
            testExistsCmd.stdout,
            testExistsCmd.stderr,
        );
        return { exists: testExistsCmd.code === 0, stderr: testExistsCmd.stderr };
    }
    /**
     * Check the user's $PATH for our server binary.
     * @param session Pre-established SSH session
     * @returns object describing the details of any located zo program
     */
    public static async detectServerOnPath(session: SshSession | SessionContext): Promise<IServerOnPathDetails> {
        Logger.getAppLogger().debug(`[ZSshUtils] enter detectServerOnPath()`);
        return ZSshUtils.withSsh(session, async (ssh) => {
            const details: IServerOnPathDetails = {
                serverPath: undefined,
                hasExecutePermission: false,
                version: undefined,
            };
            const notFoundMessage = "Zowe binary not found";

            let commandVOutput = "";

            const findBinInOutput = () => {
                let foundBin: string | undefined;
                commandVOutput.split("\n").forEach((line: string) => {
                    if (line.trim().endsWith(ZSshClient.BIN_NAME)) {
                        // the output may be prefixed with unprintable characters such as \u001b,
                        // so consider the bin path to start with the first '/'
                        const firstSlashIndex = line.indexOf("/");
                        if (firstSlashIndex >= 0) {
                            foundBin = line.substring(firstSlashIndex).trim();
                        }
                    }
                });
                return foundBin;
            };
            const findNotFoundMessageInOutput = () => {
                let foundMsg: string | undefined;
                commandVOutput.split("\n").forEach((line: string) => {
                    if (line.trim().endsWith(notFoundMessage)) {
                        foundMsg = line.trim();
                    }
                });
                return foundMsg;
            };
            await ssh.withShell((shellChannel) => {
                return new Promise((resolve, reject) => {
                    shellChannel.on("error", reject);
                    shellChannel.on("exit", () => {
                        Logger.getAppLogger().debug(
                            `[ZSshUtils] detectServerOnPath(): SSH shell exited. Checking shell stdout...`,
                        );

                        if (shellChannel.stdout.readableLength > 0) {
                            commandVOutput += shellChannel.stdout.read().toString();
                            Logger.getAppLogger().debug(`[ZSshUtils] Final output: '${commandVOutput}'..`);
                        }
                        resolve();
                    });
                    shellChannel.on("data", (output: string | Buffer) => {
                        commandVOutput += output.toString();
                        Logger.getAppLogger().debug(`[ZSshUtils] Received command -v output: '${output}'..`);
                        if (
                            shellChannel.closed ||
                            shellChannel.stdout.readableEnded ||
                            findBinInOutput() ||
                            findNotFoundMessageInOutput()
                        ) {
                            resolve();
                        }
                    });
                    shellChannel.write(`(command -v ${ZSshClient.BIN_NAME} || echo ${notFoundMessage}) && exit\n`);
                    shellChannel.end();
                });
            });
            Logger.getAppLogger().debug(
                `[ZSshUtils] Returned from shell executing 'command -v ${ZSshClient.BIN_NAME}'`,
            );

            const foundBin = findBinInOutput();
            if (foundBin) {
                details.serverPath = foundBin;
                Logger.getAppLogger().info(`[ZSshUtils] Found zo executable at ${details.serverPath}`);
                // should not need to use quotePath here since this is not based on user input
                const testExecuteCmd = await ssh.execCommand(`${details.serverPath} -v`);
                details.hasExecutePermission = testExecuteCmd.code === 0;
                details.version = details.hasExecutePermission ? testExecuteCmd.stdout.trim() : undefined;
            } else {
                Logger.getAppLogger().info(
                    `[ZSshUtils] Did not detect any ${ZSshClient.BIN_NAME} program on the user's PATH.`,
                );
            }

            return details;
        });
    }

    /**
     * A value of `true` will prevent us from trying to deploy the server.
     * @param session Pre-established SSH session
     * @param path The path to test for write access
     * @returns A promise resolving to true if the user is denied write access.
     *          If the path does not exist, false will be returned.
     */
    public static async lacksWriteAccess(session: SshSession | SessionContext, testPath: string): Promise<boolean> {
        return ZSshUtils.withSsh(session, async (ssh) => {
            Logger.getAppLogger().info(`[ZSshUtils] Testing lacksWriteAccess to path '%s'`, testPath);

            // See: https://www.man7.org/linux/man-pages/man1/test.1.html
            const pathExistsCheck = await ZSshUtils.pathExists(ssh, testPath);
            let testWriteCmd: SSHExecCommandResponse;
            if (pathExistsCheck.exists) {
                testWriteCmd = await ssh.execCommand(`test -w ${ZSshUtils.quotePath(testPath)}`);
                Logger.getAppLogger().debug(
                    `[ZSshUtils] test -w %s, code %d, stdout: '%s', stderr: '%s'`,
                    testPath,
                    testWriteCmd.code,
                    testWriteCmd.stdout,
                    testWriteCmd.stderr,
                );
            }

            return pathExistsCheck.exists && testWriteCmd.code !== 0; // testWriteCmd non-zero: lacks write access
        });
    }

    /***
     * Get how many megabytes of available in the specified directory.
     *
     */
    public static async getAvailableMb(ssh: NodeSSH, dir: string): Promise<AvailableMBResponse> {
        const dfCommand = await ssh.execCommand(`df -m ${ZSshUtils.quotePath(dir)}`);
        const response: AvailableMBResponse = { mb: -1, stderr: dfCommand.stderr };
        if (dfCommand.code !== 0) {
            Logger.getAppLogger().info(
                `[ZSshUtils] getAvailableMB: failed to issue df command on path ${dir} ` +
                    `with exit code ${dfCommand.code} ${dfCommand.stderr}. ` +
                    "Unable to determine the available space.",
            );
            return response;
        }

        const statsLine = dfCommand.stdout.substring(dfCommand.stdout.indexOf("\n"));
        Logger.getAppLogger().info(`[ZSshUtils] getAvailableMB: df -m ${dir} output:\n${dfCommand.stdout}`);
        // example output:
        // Mounted on     Filesystem                Avail/Total    Files      Status
        // /u/users       (EXAMPLE.USER.ZFS)        826934/8120160 4294919164 Available
        if (!statsLine.includes(")")) {
            Logger.getAppLogger().warn(
                `[ZSshUtils] getAvailableMB: Unexpected format of df command output. Unable to parse available space`,
            );
            return response;
        }
        const stats = statsLine
            .substring(statsLine.lastIndexOf(")") + 1)
            .trim()
            .split(/\s+/);
        if (stats.length < 3 || !stats[0].includes("/")) {
            Logger.getAppLogger().warn(
                `[ZSshUtils] getAvailableMB: Unexpected format of df command output. Unable to parse available space`,
            );
        } else {
            const availableMB = stats[0].substring(0, stats[0].indexOf("/"));
            response.mb = Number.parseInt(availableMB, 10);
            Logger.getAppLogger().info(`[ZSshUtils] getAvailableMB: Path '${dir}' has ${response.mb} MB remaining`);
        }

        return response;
    }

    /**
     * Asynchronously deploy the SSH server to the specified path
     * @param session - pre-established SSH session
     * @param serverPath the USS directory to deploy the SSH server to
     * @param options  optional object providing callbacks to respond to different events during deployment.
     *                 See docs on the ISshCallbacks type.
     * @returns true if the deployment was completed successfully
     */
    public static async installServer(
        session: SshSession | SessionContext,
        serverPath: string,
        options?: ISshCallbacks,
    ): Promise<boolean> {
        Logger.getAppLogger().info(
            `[ZSshUtils] installServer to ${session.ISshSession.hostname} at path: ${serverPath}`,
        );
        const localDir = ZSshUtils.getBinDir(__dirname);
        const remoteDir = serverPath.replace(/^~/, ".");
        let extractionStarted = false;

        return ZSshUtils.sftp(session, async (sftp, ssh) => {
            Logger.getAppLogger().info(`[ZSshUtils] Step 1/5: Creating remote directory ${remoteDir}`);
            const remotePaxPath = path.posix.join(remoteDir, ZSshUtils.SERVER_PAX_FILE);
            const initialRemoteDirExistCheck = await ZSshUtils.pathExists(ssh, remoteDir);
            if (await ZSshUtils.routeExpiredPasswordError(initialRemoteDirExistCheck.stderr ?? "", "deploy", options)) {
                return false;
            }

            try {
                const execReturn = await ssh.execCommand(`mkdir -p ${ZSshUtils.quotePath(remoteDir)}`);
                if (execReturn.code !== 0) {
                    const technical = `mkdir -p ${remoteDir} RC=${execReturn.code}: ${execReturn.stderr}`;
                    Logger.getAppLogger().error(`[ZSshUtils] Step 1 FAILED: ${technical}`);
                    const err = new ImperativeError({
                        msg: "Failed to create the server directory on the remote system.",
                        errorCode: "EDEPLOYFAIL",
                        additionalDetails: technical,
                    });
                    if (options?.onError) {
                        const shouldRetry = await options.onError(err, "deploy");
                        if (!shouldRetry) {
                            return false;
                        }
                        return ZSshUtils.installServer(session, serverPath, options);
                    }
                    return false;
                }
                const availableMb = await ZSshUtils.getAvailableMb(ssh, remoteDir);
                if (await ZSshUtils.routeExpiredPasswordError(availableMb.stderr ?? "", "deploy", options)) {
                    return false;
                }

                if (availableMb.mb < ZSshClient.REQUIRED_DEPLOY_SIZE_MB) {
                    if (
                        options?.onInsufficientSpaceWarning &&
                        !(await options.onInsufficientSpaceWarning(availableMb.mb, ZSshClient.REQUIRED_DEPLOY_SIZE_MB))
                    ) {
                        Logger.getAppLogger().info(
                            `[ZSshUtils] User declined to deploy to '${remoteDir}' due to lack of available space`,
                        );
                        return false;
                    } else {
                        Logger.getAppLogger().info(
                            `[ZSshUtils] No onInsufficientSpaceWarning callback provided or user accepted the risk and proceeded to deploy`,
                        );
                    }
                }
                if (await ZSshUtils.routeExpiredPasswordError(execReturn.stderr ?? "", "deploy", options)) {
                    return false;
                }

                const localPaxPath = path.join(localDir, ZSshUtils.SERVER_PAX_FILE);

                let previousPercentage = 0;
                const progressCallback = options?.onProgress
                    ? (progress: number, _chunk: number, total: number) => {
                          const percentage = Math.floor((progress / total) * 100);
                          const increment = percentage - previousPercentage;
                          if (increment > 0) {
                              options.onProgress!(increment);
                              previousPercentage = percentage;
                          }
                      }
                    : undefined;

                Logger.getAppLogger().info(
                    `[ZSshUtils] Step 2/5: Uploading ${ZSshUtils.SERVER_PAX_FILE} to ${remotePaxPath}`,
                );
                try {
                    await promisify(sftp.fastPut.bind(sftp))(localPaxPath, remotePaxPath, { step: progressCallback });
                } catch (err) {
                    if (await ZSshUtils.routeExpiredPasswordError(String(err), "upload", options)) {
                        return false;
                    }
                    const codePart = (err as SftpError).code == null ? "" : ` RC=${(err as SftpError).code}`;
                    const technical = `Upload ${ZSshUtils.SERVER_PAX_FILE}${codePart}: ${err.message}`;
                    Logger.getAppLogger().error(`[ZSshUtils] Step 2 FAILED: ${technical}`);
                    const uploadErr = new ImperativeError({
                        msg: "Failed to upload the server binary to the remote system.",
                        errorCode: "EDEPLOYFAIL",
                        additionalDetails: technical,
                    });
                    if (options?.onError) {
                        const shouldRetry = await options.onError(uploadErr, "upload");
                        if (!shouldRetry) {
                            return false;
                        }
                        return ZSshUtils.installServer(session, serverPath, options);
                    }
                    return false;
                }

                Logger.getAppLogger().info(`[ZSshUtils] Step 3/5: Extracting PAX archive in ${remoteDir}`);
                extractionStarted = true;
                const result = await ssh.execCommand(`pax -rzf ${ZSshUtils.SERVER_PAX_FILE}`, { cwd: remoteDir });
                if (await ZSshUtils.routeExpiredPasswordError(result.stderr ?? "", "extract", options)) {
                    return false;
                }
                if (result.code === 0) {
                    Logger.getAppLogger().info(`[ZSshUtils] Step 3 OK: Extracted server binaries`);
                } else {
                    const technical = `pax -rzf RC=${result.code}: ${result.stderr}`;
                    Logger.getAppLogger().error(`[ZSshUtils] Step 3 FAILED: ${technical}`);
                    const paxErr = new ImperativeError({
                        msg: "Failed to extract the server archive on the remote system.",
                        errorCode: "EDEPLOYFAIL",
                        additionalDetails: technical,
                    });
                    if (options?.onError) {
                        const shouldContinue = await options.onError(paxErr, "extract");
                        if (!shouldContinue) {
                            throw paxErr;
                        }
                    } else {
                        throw paxErr;
                    }
                }
            } catch (deployErr) {
                Logger.getAppLogger().error(
                    `Error was thrown during deployment: ${deployErr.message}. Attempting post-failure cleanup...`,
                );
                if (deployErr instanceof ImperativeError && deployErr.errorCode === "EPASSWD_EXPIRED") {
                    // we can't clean up if the error is a password expiration, so just re-throw
                    throw deployErr;
                }
                try {
                    const postFailurePaxExistsCheck = await ZSshUtils.pathExists(ssh, remotePaxPath);
                    if (
                        await ZSshUtils.routeExpiredPasswordError(
                            postFailurePaxExistsCheck.stderr ?? "",
                            "extract",
                            options,
                        )
                    ) {
                        return false;
                    }
                    if (postFailurePaxExistsCheck.exists) {
                        await promisify(sftp.unlink.bind(sftp))(remotePaxPath);
                    }

                    const remoteProgramPath = path.posix.join(remoteDir, ZSshClient.BIN_NAME);

                    if (extractionStarted && (await ZSshUtils.pathExists(ssh, remoteProgramPath)).exists) {
                        Logger.getAppLogger().debug(
                            `Deployment failed, but extraction was started. Attempting to delete ${ZSshClient.BIN_NAME} program at '${remoteProgramPath}'`,
                        );
                        await promisify(sftp.unlink.bind(sftp))(remoteProgramPath);
                    }
                    if (!initialRemoteDirExistCheck.exists) {
                        Logger.getAppLogger().debug(
                            `Post-failure cleanup: deleting remote dir ${remoteDir} which we created`,
                        );
                        await promisify(sftp.rmdir.bind(sftp))(remoteDir);
                    }
                } catch (failureCleanupErr) {
                    Logger.getAppLogger().error(
                        `Error was thrown during post-failure cleanup: ${failureCleanupErr.message}`,
                    );
                }

                return false;
            }
            Logger.getAppLogger().info(`[ZSshUtils] Step 4/5: Cleaning up ${remotePaxPath}`);
            try {
                await promisify(sftp.unlink.bind(sftp))(remotePaxPath);
            } catch (err) {
                const technical = `${err}`;
                Logger.getAppLogger().warn(`[ZSshUtils] Step 4 WARNING: cleanup failed: ${technical}`);
                const cleanupErr = new ImperativeError({
                    msg: "Failed to clean up the upload archive on the remote system.",
                    errorCode: "ECLEANUPFAIL",
                    additionalDetails: technical,
                });
                if (options?.onError) {
                    await options.onError(cleanupErr, "cleanup");
                } else {
                    Logger.getAppLogger().debug("Cleanup error is non-fatal, continuing...");
                }
            }

            Logger.getAppLogger().info("[ZSshUtils] Step 5/5: Verifying the server binary runs");
            const verifyErr = await ZSshUtils.verifyServerBinary(ssh, remoteDir);
            if (verifyErr != null) {
                Logger.getAppLogger().error(`[ZSshUtils] Step 5 FAILED: ${verifyErr.additionalDetails}`);
                if (options?.onError) {
                    // The files are in place; only the runtime is unusable. Retrying the install
                    // cannot help, so treat the callback's answer as "reported, carry on or stop".
                    return await options.onError(verifyErr, "verify");
                }
                throw verifyErr;
            }

            Logger.getAppLogger().info("[ZSshUtils] installServer completed successfully");
            return true;
        });
    }

    public static async uninstallServer(
        session: SshSession | SessionContext,
        serverPath: string,
        options?: Omit<ISshCallbacks, "onProgress">,
    ): Promise<void> {
        Logger.getAppLogger().debug(`Uninstalling server from ${session.ISshSession.hostname} at path: ${serverPath}`);
        return ZSshUtils.sftp(session, async (_sftp, ssh) => {
            const result = await ssh.execCommand(`rm -rf ${ZSshUtils.quotePath(serverPath)}`);
            if (await ZSshUtils.routeExpiredPasswordError(result.stderr ?? "", "unlink", options)) return;
            if (result.code === 0) {
                Logger.getAppLogger().debug(`Deleted directory ${serverPath} with response: ${result.stdout}`);
            } else {
                const technical = `rm -rf ${serverPath} RC=${result.code}: ${result.stderr}`;
                Logger.getAppLogger().error(`[ZSshUtils] Uninstall FAILED: ${technical}`);
                const rmErr = new ImperativeError({
                    msg: "Failed to uninstall the server from the remote system.",
                    errorCode: "EUNINSTALLFAIL",
                    additionalDetails: technical,
                });
                if (options?.onError) {
                    const shouldContinue = await options.onError(rmErr, "unlink");
                    if (!shouldContinue) {
                        throw rmErr;
                    }
                    return ZSshUtils.uninstallServer(session, serverPath, options);
                }
                throw rmErr;
            }
        });
    }

    public static checkIfOutdated(remoteVersion?: string): boolean {
        Logger.getAppLogger().debug(
            `[ZSshUtils] checkIfOutdated: Comparing remote version '${remoteVersion}' to bundled server version '${BUNDLED_SSH_SERVER_VERSION}'`,
        );
        if (!semver.valid(remoteVersion)) {
            Logger.getAppLogger().warn(
                `[ZSshUtils] checkIfOutdated: Invalid remote version '${remoteVersion}' passed. Assuming outdated by default`,
            );
            return true;
        }
        // coerce to drop any -suffix in the remote version
        const isOutdated = semver.lt(semver.coerce(remoteVersion), BUNDLED_SSH_SERVER_VERSION);
        Logger.getAppLogger().debug(`[ZSshUtils] remote version '${remoteVersion}' is outdated? ${isOutdated}`);
        return isOutdated;
    }

    private static getBinDir(dir: string): string {
        if (!dir || fs.existsSync(path.join(dir, "package.json"))) {
            return path.join(dir, "bin");
        }

        const dirUp = path.normalize(path.join(dir, ".."));
        if (path.parse(dirUp).base.length > 0) {
            return ZSshUtils.getBinDir(dirUp);
        }

        throw new Error(`Failed to find bin directory in path ${dir}`);
    }

    private static async sftp<T>(
        session: SshSession | SessionContext,
        callback: (sftp: SFTPWrapper, ssh: NodeSSH) => Promise<T>,
    ): Promise<T> {
        return ZSshUtils.withSsh(session, async (ssh) => callback(await ssh.requestSFTP(), ssh));
    }

    private static async withSsh<T>(
        session: SshSession | SessionContext,
        callback: (ssh: NodeSSH) => Promise<T>,
    ): Promise<T> {
        if (session instanceof SessionContext) {
            return callback(await session.getSsh());
        }
        using context = new SessionContext(session);
        return await callback(await context.getSsh());
    }
}
