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

import * as childProcess from "node:child_process";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { PassThrough, pipeline, Readable, Transform, type TransformCallback } from "node:stream";
import { promisify } from "node:util";
import { DeferredPromise, DeferredPromiseStatus, type IProfile, ProfileInfo } from "@zowe/imperative";
import * as chokidar from "chokidar";
import * as yaml from "js-yaml";
import { Client, type ClientCallback, type PseudoTtyOptions, type SFTPWrapper } from "ssh2";

interface IConfig {
    sshProfile: string | IProfile;
    deployDir: string;
    preBuildCmd?: string;
    testEnv?: Record<string, string>;
    pythonEnv?: Record<string, string>;
}

type SftpError = Error & { code?: number };

// Constants for precompiled Python bindings
const STICKY_MARKER = "Precompiled Python bindings";
const COMMENT_HEADER = "🐍 Precompiled Python bindings";
const RELEASE_TAG = "py-bindings-dev";
const RELEASE_TITLE = "Python Bindings (Dev Artifacts)";
const RELEASE_NOTES =
    "Persistent host for precompiled Python bindings built from pull requests. Not for distribution — assets here are dev artifacts linked from PR comments.";
const TARBALL = path.resolve(__dirname, "../dist/zbind_bin_dist.tar.gz");

// Precompiled SWIG binary for z/OS, published to the zowex releases used as a
// persistent host for dev artifacts (see RELEASE_TAG above).
const SWIG_RELEASE_URL = "https://github.com/zowe/zowex/releases/download/py-bindings-dev/swig-4.4.1.pax.Z";
const SWIG_RELEASE_SHA256 = "024ae42af6c1210e6c67b0dd2dc18372a97430eb3e36f6461de9343005d0f96e";

/** Runs `gh` with the given args, returning trimmed stdout. */
function gh(args: string[]): string {
    try {
        return childProcess.execFileSync("gh", args, { encoding: "utf-8" }).trim();
    } catch (err) {
        if ((err as NodeJS.ErrnoException).code === "ENOENT") {
            console.error(
                'Required executable "gh" was not found on PATH. Install the GitHub CLI: https://cli.github.com/',
            );
            process.exit(1);
        }
        throw err;
    }
}

/** Returns the short git commit hash, or "local" if it cannot be determined. */
function getShortHash(): string {
    try {
        const hash = childProcess.execFileSync("git", ["rev-parse", "--short", "HEAD"], { encoding: "utf-8" }).trim();
        // Only accept a real short hash; anything else falls back to a safe literal.
        return /^[0-9a-f]{4,40}$/i.test(hash) ? hash : "local";
    } catch {
        return "local";
    }
}

/**
 * Converts a file path to use POSIX separators (forward slashes).
 * This ensures consistent cache keys across Windows and Unix systems.
 */
function toPosixPath(filePath: string): string {
    return filePath.split(path.sep).join("/");
}

/**
 * Compares two strings for sorting in alphabetical order.
 */
function localeCompare(a: string, b: string): number {
    return a.localeCompare(b);
}

const localDeployDir = "./../native";
const args = process.argv.slice(2);
let preBuildCmd: string | undefined;
let configTestEnv: Record<string, string> = {};
let deployDirs: {
    root: string;
    cDir: string;
    asmchdrDir: string;
    cTestDir: string;
    pythonDir: string;
    pythonTestDir: string;
    pythonSwigDir: string;
};

// python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)))" \
//  | iconv -f ISO8859-1 -t IBM-1047 | od -An -tx1 -v | tr -d ' \n'
const asciiToEbcdicMap =
    // biome-ignore format: the array should not be formatted
    [
    /*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    /* 0 */ 0x00, 0x01, 0x02, 0x03, 0x37, 0x2d, 0x2e, 0x2f, 0x16, 0x05, 0x15, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    /* 1 */ 0x10, 0x11, 0x12, 0x13, 0x3c, 0x3d, 0x32, 0x26, 0x18, 0x19, 0x3f, 0x27, 0x1c, 0x1d, 0x1e, 0x1f,
    /* 2 */ 0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d, 0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61,
    /* 3 */ 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f,
    /* 4 */ 0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    /* 5 */ 0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xad, 0xe0, 0xbd, 0x5f, 0x6d,
    /* 6 */ 0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    /* 7 */ 0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xc0, 0x4f, 0xd0, 0xa1, 0x07,
    /* 8 */ 0x20, 0x21, 0x22, 0x23, 0x24, 0x15, 0x06, 0x17, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x09, 0x0a, 0x1b,
    /* 9 */ 0x30, 0x31, 0x1a, 0x33, 0x34, 0x35, 0x36, 0x08, 0x38, 0x39, 0x3a, 0x3b, 0x04, 0x14, 0x3e, 0xff,
    /* A */ 0x41, 0xaa, 0x4a, 0xb1, 0x9f, 0xb2, 0x6a, 0xb5, 0xbb, 0xb4, 0x9a, 0x8a, 0xb0, 0xca, 0xaf, 0xbc,
    /* B */ 0x90, 0x8f, 0xea, 0xfa, 0xbe, 0xa0, 0xb6, 0xb3, 0x9d, 0xda, 0x9b, 0x8b, 0xb7, 0xb8, 0xb9, 0xab,
    /* C */ 0x64, 0x65, 0x62, 0x66, 0x63, 0x67, 0x9e, 0x68, 0x74, 0x71, 0x72, 0x73, 0x78, 0x75, 0x76, 0x77,
    /* D */ 0xac, 0x69, 0xed, 0xee, 0xeb, 0xef, 0xec, 0xbf, 0x80, 0xfd, 0xfe, 0xfb, 0xfc, 0xba, 0xae, 0x59,
    /* E */ 0x44, 0x45, 0x42, 0x46, 0x43, 0x47, 0x9c, 0x48, 0x54, 0x51, 0x52, 0x53, 0x58, 0x55, 0x56, 0x57,
    /* F */ 0x8c, 0x49, 0xcd, 0xce, 0xcb, 0xcf, 0xcc, 0xe1, 0x70, 0xdd, 0xde, 0xdb, 0xdc, 0x8d, 0x8e, 0xdf,
    ];

export class AsciiToEbcdicTransform extends Transform {
    _transform(chunk: Buffer, _encoding: BufferEncoding, callback: TransformCallback) {
        const output = Buffer.allocUnsafe(chunk.length);
        for (let i = 0; i < chunk.length; i++) {
            const b = chunk[i];
            // Only convert 7-bit ASCII bytes. Bytes >= 0x80 are likely part of multi-byte UTF-8
            // sequences and are left unchanged to avoid corrupting the data or introducing NULs.
            output[i] = b < 0x80 ? asciiToEbcdicMap[b] : b;
        }
        callback(null, output);
    }
}

const ebcdicToAsciiMap =
    // biome-ignore format: the array should not be formatted
    [
    /*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    /* 0 */ 0x00, 0x01, 0x02, 0x03, 0x9c, 0x09, 0x86, 0x7f, 0x97, 0x8d, 0x8e, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    /* 1 */ 0x10, 0x11, 0x12, 0x13, 0x9d, 0x0a, 0x08, 0x87, 0x18, 0x19, 0x92, 0x8f, 0x1c, 0x1d, 0x1e, 0x1f,
    /* 2 */ 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x17, 0x1b, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x05, 0x06, 0x07,
    /* 3 */ 0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04, 0x98, 0x99, 0x9a, 0x9b, 0x14, 0x15, 0x9e, 0x1a,
    /* 4 */ 0x20, 0xa0, 0xe2, 0xe4, 0xe0, 0xe1, 0xe3, 0xe5, 0xe7, 0xf1, 0xa2, 0x2e, 0x3c, 0x28, 0x2b, 0x7c,
    /* 5 */ 0x26, 0xe9, 0xea, 0xeb, 0xe8, 0xed, 0xee, 0xef, 0xec, 0xdf, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e,
    /* 6 */ 0x2d, 0x2f, 0xc2, 0xc4, 0xc0, 0xc1, 0xc3, 0xc5, 0xc7, 0xd1, 0xa6, 0x2c, 0x25, 0x5f, 0x3e, 0x3f,
    /* 7 */ 0xf8, 0xc9, 0xca, 0xcb, 0xc8, 0xcd, 0xce, 0xcf, 0xcc, 0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22,
    /* 8 */ 0xd8, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xab, 0xbb, 0xf0, 0xfd, 0xfe, 0xb1,
    /* 9 */ 0xb0, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0xaa, 0xba, 0xe6, 0xb8, 0xc6, 0xa4,
    /* A */ 0xb5, 0x7e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0xa1, 0xbf, 0xd0, 0x5b, 0xde, 0xae,
    /* B */ 0xac, 0xa3, 0xa5, 0xb7, 0xa9, 0xa7, 0xb6, 0xbc, 0xbd, 0xbe, 0xdd, 0xa8, 0xaf, 0x5d, 0xb4, 0xd7,
    /* C */ 0x7b, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0xad, 0xf4, 0xf6, 0xf2, 0xf3, 0xf5,
    /* D */ 0x7d, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0xb9, 0xfb, 0xfc, 0xf9, 0xfa, 0xff,
    /* E */ 0x5c, 0xf7, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xb2, 0xd4, 0xd6, 0xd2, 0xd3, 0xd5,
    /* F */ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xb3, 0xdb, 0xdc, 0xd9, 0xda, 0x9f,
    ];

class EbcdicToAsciiTransform extends Transform {
    _transform(chunk: Buffer, _encoding: BufferEncoding, callback: TransformCallback) {
        const output = Buffer.allocUnsafe(chunk.length);
        for (let i = 0; i < chunk.length; i++) {
            output[i] = ebcdicToAsciiMap[chunk[i]];
        }
        // Re-encode the entire buffer from latin1 to UTF-8 so non-ASCII characters are valid in the XML test results.
        callback(null, Buffer.from(output.toString("latin1"), "utf8"));
    }
}

class WatchUtils {
    public cache: Record<string, number>;
    private buildMutex: DeferredPromise<void>;
    private cacheDir = path.resolve(__dirname, "../.cache");
    private cacheFile: string;
    private readonly pendingChanges: Map<string, { kind: "+" | "~" | "-"; mtime: Date }> = new Map();
    private rootDir: string;
    private sftp: SFTPWrapper | null = null;
    private readonly ui: WatchUI = new WatchUI();
    private watcher: chokidar.FSWatcher;
    private readonly runTests: boolean;

    constructor(
        private readonly connection: Client,
        sshProfile: IProfile,
        options?: { runTests?: boolean },
    ) {
        this.cacheFile = path.resolve(this.cacheDir, `${sshProfile.user}_${sshProfile.host}.json`);
        this.rootDir = path.resolve(__dirname, localDeployDir);
        this.runTests = options?.runTests ?? false;
        this.loadCache();
    }

    private async getSftp(): Promise<SFTPWrapper> {
        if (this.sftp == null) {
            this.sftp = await promisify(this.connection.sftp.bind(this.connection))();
            this.sftp.on("close", () => {
                this.sftp = null;
            });
        }
        return this.sftp;
    }

    private closeSftp() {
        if (this.sftp != null) {
            this.sftp.end();
            this.sftp = null;
        }
    }

    public loadCache() {
        if (fs.existsSync(this.cacheFile)) {
            this.cache = JSON.parse(fs.readFileSync(this.cacheFile, "utf8"));
        } else {
            this.cache = {};
        }
    }

    public saveCache() {
        fs.mkdirSync(this.cacheDir, { recursive: true });
        fs.writeFileSync(this.cacheFile, JSON.stringify(this.cache, null, 2));
    }

    public async start() {
        const changedFiles = getAllServerFiles().filter(
            (filePath) =>
                !path.basename(filePath).startsWith(".") &&
                fs.statSync(path.join(this.rootDir, filePath)).mtimeMs > (this.cache[toPosixPath(filePath)] ?? 0),
        );
        for (const filePath of changedFiles) {
            const absLocalPath = path.resolve(__dirname, `${localDeployDir}/${filePath}`);
            const changeKind = this.cache[toPosixPath(filePath)] == null ? "+" : "~";
            this.pendingChanges.set(filePath, { kind: changeKind, mtime: fs.statSync(absLocalPath).mtime });
        }

        this.watcher = chokidar.watch(["**/*"], {
            cwd: this.rootDir,
            ignored: /(^|[\\/])\.cache([\\/]|$)/,
            ignoreInitial: true,
            persistent: true,
        });
        let debounceTimer: NodeJS.Timeout;
        this.connection.on("close", () => {
            if (debounceTimer) {
                clearTimeout(debounceTimer);
            }
            this.closeSftp();
            this.watcher.close();
        });
        const applyChangesDebounced = () => {
            if (debounceTimer) {
                clearTimeout(debounceTimer);
            }
            debounceTimer = setTimeout(() => void this.applyChanges(), 250);
        };
        this.watcher.on("add", (filePath, stats) => {
            this.pendingChanges.set(filePath, { kind: "+", mtime: stats?.mtime ?? new Date() });
            applyChangesDebounced();
        });
        this.watcher.on("change", (filePath, stats) => {
            this.pendingChanges.set(filePath, { kind: "~", mtime: stats?.mtime ?? new Date() });
            applyChangesDebounced();
        });
        this.watcher.on("unlink", (filePath) => {
            this.pendingChanges.set(filePath, { kind: "-", mtime: new Date() });
            applyChangesDebounced();
        });

        if (this.pendingChanges.size > 0) {
            void this.applyChanges();
        } else {
            // Show initial "watching" message when no changes pending
            console.log(`${new Date().toLocaleString()} watching for changes...`);
        }
    }

    private async applyChanges() {
        if (this.buildMutex?.status === DeferredPromiseStatus.Pending) {
            await this.buildMutex.promise;
        }
        if (this.pendingChanges.size === 0) {
            return;
        }

        this.buildMutex = new DeferredPromise<void>();
        try {
            // Update UI with pending file changes
            this.ui.setFiles(this.pendingChanges);
            this.ui.setFooter("syncing files...");
            this.ui.render();

            const toDelete: string[] = [];
            const toUpload: { localPath: string; remotePath: string }[] = [];
            const uniqueDirs = new Set<string>();
            for (const [filePath, { kind }] of this.pendingChanges.entries()) {
                const remotePath = `${deployDirs.root}/${toPosixPath(filePath)}`;
                if (kind === "-") {
                    toDelete.push(remotePath);
                } else {
                    toUpload.push({ localPath: filePath, remotePath });
                    // Ensure all parent directories exist on the remote side
                    this.collectParentDirs(remotePath, uniqueDirs);
                }
            }
            this.pendingChanges.clear();

            const sftp = await this.getSftp();
            await Promise.all(toDelete.map((remotePath) => this.deleteFile(sftp, remotePath)));
            const orderedDirs = Array.from(uniqueDirs).sort((left, right) => {
                return left.split("/").length - right.split("/").length;
            });
            for (const dirPath of orderedDirs) {
                // Create directories sequentially since order may matter
                await this.createDir(sftp, dirPath);
            }
            await Promise.all(
                toUpload.map(({ localPath, remotePath }) => this.uploadFile(sftp, localPath, remotePath)),
            );
            this.saveCache();
            await this.executeBuild(...toUpload.map(({ localPath }) => localPath));
        } finally {
            this.buildMutex.resolve();
            this.printReadyMessage();
        }
    }

    private printReadyMessage() {
        this.ui.setFooter("watching for changes...");
        this.ui.render();
        // Reset UI state for next change batch (keeps last render visible)
        this.ui.reset();
    }

    private async deleteFile(sftp: SFTPWrapper, remotePath: string) {
        try {
            await promisify(sftp.unlink.bind(sftp))(remotePath);
        } catch (err) {
            if (err && (err as SftpError).code !== 2) {
                throw err; // Ignore if file does not exist
            }
        }
        delete this.cache[path.posix.relative(deployDirs.root, remotePath)];
    }

    private collectParentDirs(remotePath: string, uniqueDirs: Set<string>) {
        let dirPath = path.posix.dirname(remotePath);
        while (dirPath && dirPath !== "." && dirPath !== "/" && dirPath !== deployDirs.root) {
            uniqueDirs.add(dirPath);
            const nextDir = path.posix.dirname(dirPath);
            if (nextDir === dirPath) {
                break;
            }
            dirPath = nextDir;
        }
        if (deployDirs.root && deployDirs.root !== "/" && deployDirs.root !== ".") {
            uniqueDirs.add(deployDirs.root);
        }
    }

    private async createDir(sftp: SFTPWrapper, remotePath: string) {
        try {
            await promisify(sftp.mkdir.bind(sftp))(remotePath);
        } catch (err) {
            if (err && (err as SftpError).code !== 4) {
                throw err; // Ignore if directory already exists
            }
        }
    }

    private async uploadFile(sftp: SFTPWrapper, localPath: string, remotePath: string) {
        const absLocalPath = path.resolve(__dirname, `${localDeployDir}/${localPath}`);
        await uploadFile(sftp, absLocalPath, remotePath);
        this.cache[path.posix.relative(deployDirs.root, remotePath)] = fs.statSync(absLocalPath).mtimeMs;
    }

    private async executeBuild(...paths: string[]) {
        const cSourceChanged = paths.some((filePath) => {
            const parts = toPosixPath(filePath).split("/");
            return parts[0] === "c" && parts[1] !== "test";
        });
        const cTestChanged = paths.some((filePath) => toPosixPath(filePath).startsWith("c/test/"));

        const tasksToRun = [...(cSourceChanged ? ["c"] : [])];

        const shouldRunTests = this.runTests && (cSourceChanged || cTestChanged);

        if (shouldRunTests) {
            tasksToRun.push("c:test:make", "c:test:run");
        }

        for (const task of tasksToRun) {
            this.ui.addTask(task);
        }
        this.ui.setFooter("");
        this.ui.startSpinner();

        try {
            let result: number | string;
            if (cSourceChanged) {
                result = await this.makeTask("c");
            }

            const buildSucceeded = typeof result === "string" || result === 0 || result === undefined;
            if (buildSucceeded && shouldRunTests) {
                const testMakeResult = await this.makeTask("c:test:make", deployDirs.cTestDir);
                if (typeof testMakeResult === "string" || testMakeResult === 0) {
                    await this.runTestTask("c:test:run", deployDirs.cTestDir, "ztest_runner");
                }
            }
        } finally {
            this.ui.stopSpinner();
            this.ui.setFooter("watching for changes...");
            this.ui.render();
        }
    }

    private makeTask(taskName: string, inDir?: string): Promise<number | string> {
        this.ui.updateTask(taskName, "running");
        this.ui.render();

        return new Promise<number | string>((resolve, reject) => {
            this.connection.shell(false, async (err, stream) => {
                if (err) {
                    this.ui.updateTask(taskName, "error", err.message);
                    this.ui.render();
                    reject(err);
                    return;
                }

                // Capture initial shell output like MOTD before sending commands
                stream.write("echo\n");
                await new Promise<void>((resolve) => stream.once("data", resolve));

                const cwd = inDir ?? deployDirs.cDir;
                const envSetup = preBuildCmd ? `${preBuildCmd}\n` : "";
                const cmd = `${envSetup}cd ${cwd}\nmake ${BUILD_TYPE_FLAG()}\nexit $?\n`;
                stream.write(cmd);

                let outText = "";
                let errText = "";
                stream
                    .on("exit", (code: number) => {
                        if (errText.length > 0) {
                            const status: TaskStatus = code > 0 ? "error" : "warning";
                            this.ui.updateTask(taskName, status, errText);
                            this.ui.render();
                            resolve(code);
                        } else {
                            const status: TaskStatus = outText.length > 0 ? "success" : "no_changes";
                            this.ui.updateTask(taskName, status);
                            this.ui.render();
                            resolve(outText);
                        }
                    })
                    .on("error", (err: Error) => {
                        this.ui.updateTask(taskName, "error", err.message);
                        this.ui.render();
                        reject(err);
                    })
                    .stdout.on("data", (data: Buffer) => {
                        const str = data.toString().trim();
                        if (str.length > 0) {
                            outText += (outText.length > 0 ? "\n" : "") + str;
                        }
                    })
                    .stderr.on("data", (data: Buffer) => {
                        // Filter out INFO level messages and ones about compiler optimizations
                        const str = data.toString().trim();
                        if (/IGD\d{5}I /.test(str) || /WARNING CLC\d+:/.test(str)) return;
                        if (str.length > 0) {
                            errText += (errText.length > 0 ? "\n" : "") + str;
                        }
                    });
            });
        });
    }

    private runTestTask(taskName: string, testDir: string, runner: string): Promise<number | string> {
        this.ui.updateTask(taskName, "running");
        this.ui.render();

        return new Promise<number | string>((resolve, reject) => {
            this.connection.shell(false, async (err, stream) => {
                if (err) {
                    this.ui.updateTask(taskName, "error", err.message);
                    this.ui.render();
                    reject(err);
                    return;
                }

                // Capture initial shell output like MOTD before sending commands
                stream.write("echo\n");
                await new Promise<void>((resolve) => stream.once("data", resolve));

                const cmd = `cd ${testDir}\n./build-out/${runner}\nexit $?\n`;
                stream.write(cmd);

                let outText = "";
                let outputBuffer = "";
                let resolved = false;
                let hasFailed = false;
                let seenTimeLine = false;
                // Collect failures as { suite: string, tests: string[] }
                const failedSuites: { suite: string; tests: string[] }[] = [];
                let currentFailSuite: { suite: string; tests: string[] } | null = null;

                const finish = () => {
                    if (resolved) return;
                    resolved = true;
                    stream.removeAllListeners();
                    stream.stdout.removeAllListeners();
                    stream.stderr.removeAllListeners();
                    // End the shell so the SSH channel closes cleanly
                    stream.end();

                    if (hasFailed) {
                        let errorMsg = "Tests failed";
                        if (failedSuites.length > 0) {
                            const lines = ["Tests failed:"];
                            for (const { suite, tests } of failedSuites) {
                                lines.push(`  • ${suite}`);
                                for (const test of tests) {
                                    lines.push(`      ${test}`);
                                }
                            }
                            errorMsg = lines.join("\n");
                        }
                        this.ui.updateTask(taskName, "error", errorMsg);
                        this.ui.render();
                        resolve(1);
                    } else {
                        this.ui.updateTask(taskName, "success");
                        this.ui.render();
                        resolve(outText);
                    }
                };

                // Detect test completion from stdout by matching the "Time:" summary line.
                // The "exit" and "close" events do not fire reliably for shell sessions on z/OS.
                //
                // In the FAILED TESTS section, suite paths are unindented:
                //   ✗ FAIL zo > data-set > compress
                // Individual tests are indented with 2 spaces:
                //     ✗ FAIL should compress a data set (392.248ms)
                const suiteFailPattern = /^[✗-] FAIL\s+(.+)/;
                const testFailPattern = /^\s+[✗-] FAIL\s+(.+)/;
                const timeLinePattern = /^Time:\s+[\d.]+ms/;

                const processLine = (line: string) => {
                    const trimmed = line.trim();
                    if (trimmed.length > 0) {
                        this.ui.appendTaskOutput(taskName, line);
                    }
                    // Suite path line (no leading whitespace before the cross char)
                    const suiteMatch = suiteFailPattern.exec(line);
                    if (suiteMatch) {
                        currentFailSuite = { suite: suiteMatch[1].trim(), tests: [] };
                        failedSuites.push(currentFailSuite);
                    }
                    // Individual test line (indented)
                    else {
                        const testMatch = testFailPattern.exec(line);
                        if (testMatch && currentFailSuite) {
                            currentFailSuite.tests.push(testMatch[1].trim());
                        }
                    }
                    // Check for test failures from the "Tests:" summary line
                    if (/Tests:\s+.*\d+ failed/.test(trimmed)) {
                        hasFailed = true;
                    }
                    // The "Time:" line is the last line of the test summary
                    if (timeLinePattern.test(trimmed)) {
                        seenTimeLine = true;
                    }
                };

                // After the "Time:" line is detected, wait briefly for any trailing
                // data before finishing. This avoids resolving too early if the stream
                // delivers additional chunks after the summary.
                const finishAfterTimeLine = () => {
                    setTimeout(() => {
                        // Flush any leftover buffer content
                        if (outputBuffer.length > 0) {
                            processLine(outputBuffer);
                            outputBuffer = "";
                        }
                        finish();
                    }, 250);
                };

                stream.on("exit", () => {
                    // Flush any remaining buffered output before finishing
                    if (outputBuffer.length > 0) {
                        processLine(outputBuffer);
                        outputBuffer = "";
                    }
                    finish();
                });
                stream.on("error", (err: Error) => {
                    if (resolved) return;
                    resolved = true;
                    this.ui.updateTask(taskName, "error", err.message);
                    this.ui.render();
                    reject(err);
                });
                stream.stdout.on("data", (data: Buffer) => {
                    if (resolved) return;
                    const str = data.toString();
                    outText += str;

                    // Process line by line for sliding window display
                    outputBuffer += str;
                    const lines = outputBuffer.split("\n");
                    // Keep last incomplete line in buffer
                    outputBuffer = lines.pop() || "";

                    for (const line of lines) {
                        processLine(line);
                    }

                    // Also check the incomplete buffer for the Time: pattern
                    // (the line may arrive without a trailing newline)
                    if (outputBuffer.length > 0 && timeLinePattern.test(outputBuffer.trim())) {
                        processLine(outputBuffer);
                        outputBuffer = "";
                    }

                    this.ui.render();

                    if (seenTimeLine) {
                        finishAfterTimeLine();
                    }
                });
                stream.stderr.on("data", (data: Buffer) => {
                    if (resolved) return;
                    const str = data.toString().trim();
                    if (str.length > 0) {
                        // Surface stderr lines in the sliding window too
                        this.ui.appendTaskOutput(taskName, str);
                        this.ui.render();
                    }
                });
            });
        });
    }
}

function DEBUG_MODE() {
    return process.env.ZOWE_NATIVE_DEBUG?.toUpperCase() === "TRUE" || process.env.ZOWE_NATIVE_DEBUG === "1";
}

function BUILD_TYPE_FLAG() {
    if (DEBUG_MODE()) return "-DBuildType=DEBUG";
    if (process.env.CI != null) return "-DBuildType=RELEASE";
    return "";
}

function IS_PULL_REQUEST_FLAG() {
    return process.env.GITHUB_EVENT_NAME === "pull_request" ? "-DIsPullRequest=TRUE" : "";
}

// ANSI escape codes for terminal formatting
const ANSI = {
    HIDE_CURSOR: "\x1b[?25l",
    SHOW_CURSOR: "\x1b[?25h",
    CLEAR_LINE: "\x1b[2K",
    CLEAR_DOWN: "\x1b[J",
    MOVE_UP: (n: number) => `\x1b[${n}A`,
    MOVE_TO_COL: (n: number) => `\x1b[${n}G`,
    GREEN: "\x1b[32m",
    RED: "\x1b[31m",
    YELLOW: "\x1b[33m",
    DIM: "\x1b[2m",
    RESET: "\x1b[0m",
};

const BRAILLE_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

type FileChangeKind = "+" | "~" | "-";
type TaskStatus = "pending" | "running" | "success" | "warning" | "error" | "no_changes";

interface TaskState {
    name: string;
    status: TaskStatus;
    error?: string;
    errorLogPath?: string;
    outputLines?: string[]; // For streaming test output (sliding window)
}

class WatchUI {
    private readonly files: Map<string, FileChangeKind> = new Map();
    private readonly tasks: Map<string, TaskState> = new Map();
    private timestamp: Date = new Date();
    private lineCount = 0;
    private spinnerInterval: NodeJS.Timeout | null = null;
    private spinnerFrame = 0;
    private footerMessage = "watching for changes...";
    private lastFallbackState = "";
    private readonly printedFallbackItems: Set<string> = new Set();

    private isInteractive(): boolean {
        // Allow users to force simple output mode
        if (process.env.ZOWE_WATCH_SIMPLE === "1" || process.env.ZOWE_WATCH_SIMPLE?.toUpperCase() === "TRUE") {
            return false;
        }

        // Basic TTY check
        if (process.stdout.isTTY !== true || DEBUG_MODE() || process.env.CI != null) {
            return false;
        }

        // Check if terminal width is reasonable (indicates proper terminal emulation)
        const columns = process.stdout.columns;
        if (columns == null || columns < 20) {
            return false;
        }

        return true;
    }

    setFiles(files: Map<string, { kind: FileChangeKind; mtime: Date }>) {
        this.files.clear();
        this.timestamp = new Date();
        for (const [filePath, { kind }] of files.entries()) {
            this.files.set(filePath, kind);
        }
    }

    addTask(name: string) {
        this.tasks.set(name, { name, status: "pending" });
    }

    updateTask(name: string, status: TaskStatus, error?: string) {
        const task = this.tasks.get(name);
        if (task) {
            task.status = status;
            task.error = error;
            // Write full error to log file if present
            if (error) {
                const errorLogDir = path.resolve(__dirname, "../.cache/build-errors");
                fs.mkdirSync(errorLogDir, { recursive: true });
                const logName = name.replace(/:/g, "_");
                const logPath = path.join(errorLogDir, `${logName}.log`);
                const logContent = `Build error log for [${name}]\nTimestamp: ${this.timestamp.toLocaleString()}\n${"─".repeat(50)}\n\n${error}`;
                fs.writeFileSync(logPath, logContent);
                task.errorLogPath = `.cache/build-errors/${logName}.log`;
            }
        }
    }

    appendTaskOutput(name: string, line: string) {
        const task = this.tasks.get(name);
        if (task) {
            if (!task.outputLines) {
                task.outputLines = [];
            }
            // Keep only last 10 lines (sliding window)
            task.outputLines.push(line);
            if (task.outputLines.length > 10) {
                task.outputLines.shift();
            }
        }
    }

    setFooter(message: string) {
        this.footerMessage = message;
    }

    private getStatusIcon(status: TaskStatus): string {
        switch (status) {
            case "running":
                return BRAILLE_FRAMES[this.spinnerFrame];
            case "success":
                return `${ANSI.GREEN}✔${ANSI.RESET}`;
            case "warning":
                return `${ANSI.YELLOW}!${ANSI.RESET}`;
            case "error":
                return `${ANSI.RED}✘${ANSI.RESET}`;
            case "no_changes":
                return `${ANSI.DIM}—${ANSI.RESET}`;
            default:
                return " ";
        }
    }

    private getFileIcon(kind: FileChangeKind): string {
        switch (kind) {
            case "+":
                return `${ANSI.GREEN}+${ANSI.RESET}`;
            case "-":
                return `${ANSI.RED}-${ANSI.RESET}`;
            case "~":
                return `${ANSI.YELLOW}~${ANSI.RESET}`;
        }
    }

    private buildLines(): string[] {
        const lines: string[] = [];

        // Timestamp header
        lines.push(`${ANSI.DIM}${this.timestamp.toLocaleString()}${ANSI.RESET}`);

        // File changes
        for (const [filePath, kind] of this.files.entries()) {
            const icon = this.getFileIcon(kind);
            lines.push(`  ${icon} ${filePath}`);
        }

        // Tasks section
        if (this.tasks.size > 0) {
            lines.push("", "  tasks:");

            for (const [, task] of this.tasks.entries()) {
                const icon = this.getStatusIcon(task.status);
                // Format task name for better readability
                let taskLabel = "make";
                if (task.name.includes(":test:make")) {
                    taskLabel = "compile tests";
                } else if (task.name.includes(":test:run")) {
                    taskLabel = "run tests";
                }
                lines.push(`    [${task.name}] ${taskLabel} ${icon}`);

                // Show streaming output for test tasks (sliding window of last 10 lines)
                if (task.outputLines && task.outputLines.length > 0 && task.name.includes(":test:run")) {
                    const columns = process.stdout.columns || 80;
                    const indent = "      ";
                    const maxContentWidth = columns - indent.length;
                    for (const outputLine of task.outputLines) {
                        const visible = WatchUI.stripAnsi(outputLine);
                        const truncated =
                            visible.length > maxContentWidth ? visible.slice(0, maxContentWidth - 3) + "..." : visible;
                        lines.push(`${indent}${ANSI.DIM}${truncated}${ANSI.RESET}`);
                    }
                }

                // Show error inline if present
                if (task.error) {
                    lines.push("", `    ${ANSI.RED}error:${ANSI.RESET}`);

                    // Split error into lines and show first few
                    const allErrorLines = task.error.split("\n").filter((line) => line.trim().length > 0);
                    const maxErrorLines = 8;
                    const visibleLines = allErrorLines.slice(0, maxErrorLines);
                    const remainingCount = allErrorLines.length - maxErrorLines;

                    for (const errLine of visibleLines) {
                        lines.push(`      ${ANSI.DIM}${errLine}${ANSI.RESET}`);
                    }

                    if (remainingCount > 0) {
                        lines.push(
                            `      ${ANSI.DIM}... ${remainingCount} more line${remainingCount > 1 ? "s" : ""}${ANSI.RESET}`,
                        );
                    }

                    if (task.errorLogPath) {
                        lines.push("", `    ${ANSI.DIM}full log: ${task.errorLogPath}${ANSI.RESET}`);
                    }
                }
            }
        }

        // Footer
        lines.push("", `${ANSI.DIM}${this.footerMessage}${ANSI.RESET}`);

        return lines;
    }

    /**
     * Strips ANSI escape sequences to get the visible length of a string.
     */
    private static stripAnsi(str: string): string {
        // biome-ignore lint/suspicious/noControlCharactersInRegex: stripping ANSI escape codes
        return str.replace(/\x1b\[[0-9;]*[a-zA-Z]/g, "");
    }

    /**
     * Counts the number of physical terminal rows a set of logical lines will occupy,
     * accounting for line wrapping based on terminal width.
     */
    private countPhysicalRows(lines: string[]): number {
        const columns = process.stdout.columns || 80;
        let rows = 0;
        for (const line of lines) {
            const visibleLen = WatchUI.stripAnsi(line).length;
            rows += visibleLen === 0 ? 1 : Math.ceil(visibleLen / columns);
        }
        return rows;
    }

    private clear() {
        if (!this.isInteractive() || this.lineCount === 0) return;

        process.stdout.write(ANSI.MOVE_UP(this.lineCount));
        process.stdout.write(ANSI.MOVE_TO_COL(1));
        process.stdout.write(ANSI.CLEAR_DOWN);
    }

    render() {
        if (!this.isInteractive()) {
            this.renderFallback();
            return;
        }

        this.clear();
        const lines = this.buildLines();
        process.stdout.write(lines.join("\n") + "\n");
        this.lineCount = this.countPhysicalRows(lines);
    }

    private renderFallback() {
        const fileKey = Array.from(this.files.keys()).sort(localeCompare).join(",");
        const filesChanged = fileKey !== this.lastFallbackState;

        // Print file changes header once
        if (filesChanged && this.files.size > 0) {
            console.log(`\n${this.timestamp.toLocaleString()}`);
            for (const [filePath, kind] of this.files.entries()) {
                console.log(`[${kind}] ${filePath}`);
            }
            this.lastFallbackState = fileKey;
        }

        for (const [, task] of this.tasks.entries()) {
            const runningKey = `task:${task.name}:running`;
            const doneKey = `task:${task.name}:done`;

            // Print "running" once when task starts
            if (task.status === "running" && !this.printedFallbackItems.has(runningKey)) {
                let taskLabel = "make";
                if (task.name.includes(":test:make")) {
                    taskLabel = "compile tests";
                } else if (task.name.includes(":test:run")) {
                    taskLabel = "run tests";
                }
                console.log(`  [${task.name}] ${taskLabel} running`);
                this.printedFallbackItems.add(runningKey);
            }

            // Stream test output as it comes in (for test:run tasks)
            if (task.name.includes(":test:run") && task.outputLines) {
                for (let i = 0; i < task.outputLines.length; i++) {
                    const outputKey = `task:${task.name}:output:${i}`;
                    if (!this.printedFallbackItems.has(outputKey)) {
                        console.log(`    ${task.outputLines[i]}`);
                        this.printedFallbackItems.add(outputKey);
                    }
                }
            }

            // Print final result once when task completes
            if (!this.printedFallbackItems.has(doneKey)) {
                if (
                    task.status === "success" ||
                    task.status === "error" ||
                    task.status === "warning" ||
                    task.status === "no_changes"
                ) {
                    const statusText =
                        task.status === "success"
                            ? "✔"
                            : task.status === "error"
                              ? "✘"
                              : task.status === "warning"
                                ? "!"
                                : "—";
                    let taskLabel = "make";
                    if (task.name.includes(":test:make")) {
                        taskLabel = "compile tests";
                    } else if (task.name.includes(":test:run")) {
                        taskLabel = "run tests";
                    }
                    console.log(`  [${task.name}] ${taskLabel} ${statusText}`);
                    if (task.error) {
                        console.log("    error:");
                        const errorLines = task.error.split("\n").filter((line) => line.trim().length > 0);
                        const maxLines = 10;
                        for (const line of errorLines.slice(0, maxLines)) {
                            console.log(`      ${line}`);
                        }
                        if (errorLines.length > maxLines) {
                            console.log(
                                `      ... ${errorLines.length - maxLines} more line${errorLines.length - maxLines > 1 ? "s" : ""}`,
                            );
                        }
                        if (task.errorLogPath) {
                            console.log(`    full log: ${task.errorLogPath}`);
                        }
                    }
                    this.printedFallbackItems.add(doneKey);
                }
            }
        }

        // Print footer only when transitioning to "watching" state
        if (this.footerMessage === "watching for changes..." && !this.printedFallbackItems.has("footer:watching")) {
            console.log(this.footerMessage);
            this.printedFallbackItems.add("footer:watching");
        }
    }

    startSpinner() {
        if (!this.isInteractive()) return;

        process.stdout.write(ANSI.HIDE_CURSOR);
        this.spinnerInterval = setInterval(() => {
            this.spinnerFrame = (this.spinnerFrame + 1) % BRAILLE_FRAMES.length;
            this.render();
        }, 160);
    }

    stopSpinner() {
        if (this.spinnerInterval) {
            clearInterval(this.spinnerInterval);
            this.spinnerInterval = null;
        }
        if (this.isInteractive()) {
            process.stdout.write(ANSI.SHOW_CURSOR);
        }
    }

    reset() {
        this.stopSpinner();
        this.files.clear();
        this.tasks.clear();
        this.lineCount = 0;
        this.lastFallbackState = "";
        this.printedFallbackItems.clear();
    }
}

function startSpinner(text = "Loading...") {
    if (DEBUG_MODE() || process.env.CI != null) {
        console.log(text);
        return null;
    }

    const BRAILLE_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
    let frameIndex = 0;

    // Truncate text to fit terminal width (leave room for spinner and padding)
    const maxWidth = (process.stdout.columns || 80) - 4;
    const displayText = text.length > maxWidth ? `${text.slice(0, maxWidth - 3)}...` : text;

    // Hide cursor
    process.stdout.write("\x1b[?25l");
    return setInterval(() => {
        process.stdout.write(`\x1b[2K\r${BRAILLE_FRAMES[frameIndex]} ${displayText}`);
        frameIndex = (frameIndex + 1) % BRAILLE_FRAMES.length;
    }, 160);
}

function stopSpinner(spinner: NodeJS.Timeout | null, text = "Done!") {
    if (DEBUG_MODE() || process.env.CI != null) {
        return;
    }
    spinner && clearInterval(spinner);
    // Restore cursor
    process.stdout.write(`\x1b[?25h`);
    process.stdout.write(`\x1b[2K\r${text}\n`);
}

function getAllServerFiles() {
    const files: string[] = getServerFiles();

    // skip reading dirs if specific arguments were passed to upload
    if (args[1]) {
        return files;
    }

    const dirs = getDirs();
    const dirFiles = dirs.map((dir) => getServerFiles(dir));
    files.push(...dirFiles.flat());

    return files;
}

function getServerFiles(dir = "") {
    let argsFound = false;
    const fileList: string[] = [];

    if (args[1]) {
        argsFound = true;

        args.forEach((arg, index) => {
            if (0 === index) {
                // do nothing
            } else {
                let stats: fs.Stats;
                try {
                    stats = fs.statSync(path.resolve(__dirname, `${localDeployDir}/${arg}`));
                } catch {
                    console.log(`Error: input '${arg}' is not found`);
                    process.exit(1);
                }

                if (stats.isDirectory()) {
                    fileList.push(...getFilesRecursive(arg.replace(/\/$/, "")));
                } else {
                    fileList.push(arg);
                }
            }
        });
    }

    if (argsFound) {
        return [...fileList];
    }

    const filesList: string[] = [];
    const files = fs.readdirSync(path.resolve(__dirname, `${localDeployDir}/${dir}`), {
        withFileTypes: true,
    });

    for (const file of files) {
        if (file.isDirectory()) {
        } else {
            filesList.push(`${dir}${file.name}`);
        }
    }
    return filesList;
}

function getFilesRecursive(dir: string): string[] {
    const fileList: string[] = [];
    const entries = fs.readdirSync(path.resolve(__dirname, `${localDeployDir}/${dir}`), { withFileTypes: true });
    for (const entry of entries) {
        const entryPath = `${dir}/${entry.name}`;
        if (entry.isDirectory()) {
            fileList.push(...getFilesRecursive(entryPath));
        } else {
            fileList.push(entryPath);
        }
    }
    return fileList;
}

function getDirs(next = "") {
    const dirs: string[] = [];

    const readDirs = fs.readdirSync(path.resolve(__dirname, `${localDeployDir}/${next}`), { withFileTypes: true });
    for (const dir of readDirs) {
        if (dir.isDirectory() && dir.name !== ".cache") {
            const newDir = `${dir.name}/`;
            dirs.push(`${next}${newDir}`, ...getDirs(`${next}${newDir}`));
        }
    }
    return dirs;
}

async function artifacts(connection: Client, packageAll: boolean) {
    const artifactPaths = ["c/build-out/zo", packageAll && "c/build-out/zoa"].filter(Boolean);
    const artifactNames = artifactPaths.map((file) => path.basename(file)).sort(localeCompare);
    const localDir = packageAll ? "dist" : "packages/sdk/bin";
    const localFiles = ["server.pax.Z"];
    const [paxFile] = localFiles;
    const prePaxCmds = artifactPaths.map(
        (file) => `cp ../${file} ${path.basename(file)} && chmod 700 ${path.basename(file)}`,
    );
    const postPaxCmd = `rm ${artifactNames.join(" ")} && rmdir ../bin`;
    await runCommandInShell(
        connection,
        [
            `cd ${deployDirs.root} && mkdir -p bin dist && cd bin`,
            ...prePaxCmds,
            `pax -wvz -o saveext -f ../dist/${paxFile} ${artifactNames.join(" ")}`,
            postPaxCmd,
        ].join("\n"),
        { stepName: "Packaging artifacts" },
    );
    fs.mkdirSync(path.resolve(__dirname, `./../${localDir}`), { recursive: true });
    for (const localFile of localFiles) {
        await retrieve(connection, [`dist/${localFile}`], localDir, true);
    }
}

const pythonWheelDownloadSource = `import base64, hashlib, json, pathlib, sys
files = list(pathlib.Path(sys.argv[1]).glob('*.whl'))
if len(files) != 1:
    raise RuntimeError('Expected exactly one built wheel')
wheel = files[0]
data = wheel.read_bytes()
print(json.dumps({'name':wheel.name, 'sha256':hashlib.sha256(data).hexdigest(), 'data':base64.b64encode(data).decode()}))
`;

function quoteWheelShell(value: string): string {
    return `'${value.replaceAll("'", "'\"'\"'")}'`;
}

async function buildPythonWheel(connection: Client, config: IConfig): Promise<void> {
    const root = path.resolve(__dirname, "..");
    const pythonEnv = { ...config.pythonEnv };
    for (const key of ["ZPY_PYTHON_ROOT", "ZPY_PYTHON_311", "ZPY_PYTHON_313", "ZPY_PYTHON_314"]) {
        if (process.env[key] !== undefined) pythonEnv[key] = process.env[key];
    }
    const run = (command: string, input?: string): Promise<string> => {
        const profile = `/tmp/zbind-profile-${crypto.randomUUID()}`;
        const initialize = `umask 077
sed '/exec .*[/]bin[/]bash/d' "$HOME/.profile" > ${quoteWheelShell(profile)}
. ${quoteWheelShell(profile)} > /dev/null 2>&1
profile_status=$?
rm -f ${quoteWheelShell(profile)}
test "$profile_status" -eq 0 || exit "$profile_status"
${preBuildCmd ?? ""}
`;
        return new Promise((resolve, reject) => {
            connection.exec(initialize + command, (err, stream) => {
                if (err) return reject(err);
                let stdout = "";
                let stderr = "";
                stream.on("data", (data: Buffer) => {
                    stdout += data.toString();
                });
                stream.stderr.on("data", (data: Buffer) => {
                    stderr += data.toString();
                });
                stream.on("error", reject);
                stream.on("close", (code: number) => {
                    if (code === 0) resolve(stdout);
                    else
                        reject(
                            new Error(
                                `Python wheel command failed (${code}):\n${stdout}\n${stderr}`.replace(
                                    /https?:\/\/[^\s/]+@/g,
                                    "https://[redacted]@",
                                ),
                            ),
                        );
                });
                stream.end(input ?? "");
            });
        });
    };
    // Resolve deployDir using the SSH login directory, as the other build commands do.
    const remoteRoot = (await run(`cd ${quoteWheelShell(config.deployDir)} && pwd -P`)).trim();
    const home = (await run('printf "%s" "$HOME"')).trim();
    const expandHome = (value: string) => value.replace(/^~(?=\/|$)/, home);
    const pythonRoot = expandHome(pythonEnv.ZPY_PYTHON_ROOT ?? "/usr/lpp/IBM/cyp/").replace(/\/$/, "");
    const runtimes = Object.fromEntries(
        [11, 13, 14].map((minor) => [
            minor,
            expandHome(pythonEnv[`ZPY_PYTHON_3${minor}`] ?? `${pythonRoot}/v3r${minor}/pyz/bin/python`),
        ]),
    );
    if (!remoteRoot.startsWith("/") || !Object.values(runtimes).every((value) => value.startsWith("/"))) {
        throw new Error("Wheel deployment and Python paths must resolve to absolute USS paths");
    }
    const stage = `${remoteRoot}/.python-wheel-${crypto.randomUUID()}`;
    const python = runtimes[11];
    const pythonCommand = (source: string) => `${quoteWheelShell(python)} -c ${quoteWheelShell(source)}`;
    const tracked = childProcess
        .execFileSync("git", ["ls-files", "-z", "--", "native/c", "native/asmmac"], { cwd: root, encoding: "utf8" })
        .split("\0")
        .filter(Boolean);
    const bindings = "native/python/bindings";
    const buildFiles = [
        "Makefile",
        "setup.py",
        "setup.cfg",
        "pyproject.toml",
        "requirements-build.txt",
        "package_wheel.py",
        "_zbind_loader.py",
    ];
    const interfaces = fs.readdirSync(path.join(root, bindings)).filter((name) => /\.(cpp|hpp|i)$/.test(name));
    const files = [...tracked, ...[...buildFiles, ...interfaces].map((name) => `${bindings}/${name}`), "LICENSE"];
    let staged = false;
    try {
        console.log("Staging current sources for the Python wheel...");
        await run(`mkdir ${quoteWheelShell(stage)}`);
        staged = true;
        const directories = [...new Set(files.map((file) => path.posix.dirname(`${stage}/${file}`)))];
        await run(`mkdir -p ${directories.map(quoteWheelShell).join(" ")}`);
        const sftp = await new Promise<SFTPWrapper>((resolve, reject) =>
            connection.sftp((err, value) => (err ? reject(err) : resolve(value))),
        );
        try {
            for (const file of files) {
                await promisify(pipeline)(
                    fs.createReadStream(path.join(root, file)),
                    new AsciiToEbcdicTransform(),
                    sftp.createWriteStream(`${stage}/${file}`),
                );
            }
        } finally {
            sftp.end();
        }
        await run(`chtag -R -t -c IBM-1047 ${quoteWheelShell(stage)}`);
        await run(
            `cp -R ${quoteWheelShell(`${remoteRoot}/c/chdsect`)} ${quoteWheelShell(`${stage}/native/c/chdsect`)}`,
        );
        console.log("Building the wheel for IBM Python 3.11, 3.13 and 3.14...");
        await run(
            `${quoteWheelShell(python)} ${quoteWheelShell(`${stage}/${bindings}/package_wheel.py`)} --build-from-stdin`,
            JSON.stringify({ runtimes, indexUrl: process.env.PIP_INDEX_URL }),
        );
        const result = JSON.parse(
            await run(
                `${pythonCommand(pythonWheelDownloadSource)} ${quoteWheelShell(`${stage}/native/python/bindings/dist`)}`,
            ),
        ) as {
            name: string;
            sha256: string;
            data: string;
        };
        if (path.basename(result.name) !== result.name || !result.name.endsWith(".whl"))
            throw new Error("Invalid wheel filename");
        const bytes = Buffer.from(result.data, "base64");
        if (crypto.createHash("sha256").update(bytes).digest("hex") !== result.sha256)
            throw new Error("Wheel checksum mismatch");
        const dist = path.join(root, "dist");
        fs.mkdirSync(dist, { recursive: true });
        const file = path.join(dist, result.name);
        const temporary = `${file}.${crypto.randomUUID()}.tmp`;
        try {
            fs.writeFileSync(temporary, bytes);
            fs.renameSync(temporary, file);
        } finally {
            fs.rmSync(temporary, { force: true });
        }
        fs.writeFileSync(`${file}.sha256`, `${result.sha256}  ${result.name}\n`);
        console.log(`Downloaded ${path.relative(root, file)}\nSHA-256: ${result.sha256}`);
    } finally {
        if (staged) await run(`rm -rf ${quoteWheelShell(stage)}`);
    }
}

/**
 * Runs the precompiled Python bindings packaging script on z/OS and downloads
 * the resulting binary tarball to the local `dist/` directory. The tarball is
 * retrieved without ASCII conversion (binary-safe) to preserve the archive.
 */
async function packPrecompiled(connection: Client) {
    await runCommandInShell(connection, `cd ${deployDirs.pythonDir} && python package_precompiled.py`, {
        streamOutput: true,
        stepName: "Packaging precompiled Python bindings",
    });
    fs.mkdirSync(path.resolve(__dirname, "./../dist"), { recursive: true });
    await retrieve(connection, ["python/bindings/zbind_bin_dist.tar.gz"], "dist", true);
    console.log("Precompiled bindings downloaded to dist/zbind_bin_dist.tar.gz");
}

function getLatestTag(repoName: string, prefix?: string) {
    // --sort=-version:refname sorts tags by their numeric version (descending)
    const out = childProcess.execSync(
        `git ls-remote --tags --refs --sort=-version:refname https://github.com/${repoName}.git`,
    );
    const tags = [];
    for (const line of out.toString().trim().split(/\r?\n/)) {
        const lastSlashIdx = line.lastIndexOf("/");
        tags.push(line.slice(lastSlashIdx + 1));
    }
    const isPrerelease = (tag: string) => tag.includes("-beta") || tag.includes("-RC");
    const matchesPrefix = (tag: string) => prefix == null || tag.startsWith(prefix);
    const foundTag = tags.find((tag) => !isPrerelease(tag) && matchesPrefix(tag));
    if (foundTag == null) {
        throw Error(`No stable tag found for ${repoName}`);
    }
    return foundTag;
}

/**
 * Downloads a tarball to `destPath` if it isn't already cached. Throws if the
 * request fails outright (network error) or resolves with a non-2xx status
 * (e.g. the tarball was removed from the host), so callers fail fast instead
 * of proceeding to extract a missing or invalid archive.
 */
async function downloadTarball(url: string, destPath: string) {
    if (fs.existsSync(destPath)) {
        return;
    }
    const label = path.basename(destPath).split(/\W/)[0];
    console.log(`Downloading ${label} tarball...`);
    let response: Response;
    try {
        response = await fetch(url);
    } catch (err) {
        const reason = err instanceof Error && err.cause ? err.cause : err;
        throw new Error(`Failed to download ${label} tarball from ${url}: ${reason}`);
    }
    if (!response.ok) {
        throw new Error(`Failed to download ${label} tarball from ${url}: ${response.status} ${response.statusText}`);
    }
    await new Promise<void>((resolve, reject) => {
        pipeline(
            Readable.fromWeb(response.body as import("node:stream/web").ReadableStream),
            fs.createWriteStream(destPath),
            (err) => {
                if (err) {
                    reject(new Error(`Failed to download ${label} tarball from ${url}: ${err}`));
                } else {
                    resolve();
                }
            },
        );
    });
}

/**
 * Fails unless `filePath` hashes to `expectedSha256`. A mismatching file is deleted so the next run
 * re-downloads instead of reusing a bad cache entry, and the error is raised before the file is ever
 * uploaded or executed on z/OS.
 */
async function verifySha256(filePath: string, expectedSha256: string) {
    const expected = expectedSha256.trim().toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(expected)) {
        throw new Error(
            `No valid SHA-256 is pinned for ${path.basename(filePath)} (got "${expectedSha256}"). ` +
                "Compute it with `curl -sL <url> | shasum -a 256` and update the constant in scripts/buildTools.ts.",
        );
    }
    const hash = crypto.createHash("sha256");
    for await (const chunk of fs.createReadStream(filePath)) {
        hash.update(chunk as Buffer);
    }
    const actual = hash.digest("hex");
    if (actual !== expected) {
        fs.rmSync(filePath, { force: true });
        throw new Error(
            `Checksum mismatch for ${path.basename(filePath)}: expected ${expected}, got ${actual}. ` +
                "Refusing to upload it to z/OS; the local copy has been removed.",
        );
    }
    console.log(`Verified ${path.basename(filePath)} (sha256 ${actual}).`);
}

async function buildSwig(connection: Client) {
    const cacheDir = path.resolve(__dirname, "./../.cache");
    fs.mkdirSync(cacheDir, { recursive: true });
    const swigVersion = getLatestTag("swig/swig", "v4.").slice(1);
    const swigTgz = path.join(cacheDir, `swig-${swigVersion}.tar.gz`);
    const pcreVersion = getLatestTag("PCRE2Project/pcre2").split("-").pop();
    const pcreTgz = path.join(cacheDir, `pcre2-${pcreVersion}.tar.gz`);

    await Promise.all([
        downloadTarball(`http://prdownloads.sourceforge.net/swig/swig-${swigVersion}.tar.gz`, swigTgz),
        downloadTarball(
            `https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${pcreVersion}/pcre2-${pcreVersion}.tar.gz`,
            pcreTgz,
        ),
    ]);
    process.exit(1);

    console.log("Uploading source tarballs...");
    await new Promise<void>((resolve, reject) => {
        connection.sftp(async (err, sftpcon) => {
            if (err) {
                reject(err);
                return;
            }
            try {
                await Promise.all([
                    uploadFile(sftpcon, swigTgz, `${deployDirs.pythonSwigDir}/swig-${swigVersion}.tar.gz`, false),
                    uploadFile(sftpcon, pcreTgz, `${deployDirs.pythonSwigDir}/pcre2-${pcreVersion}.tar.gz`, false),
                ]);
                resolve();
            } catch (uploadErr) {
                reject(uploadErr);
            } finally {
                sftpcon.end();
            }
        });
    });

    await runCommandInShell(
        connection,
        `cd ${deployDirs.pythonSwigDir} && LDFLAGS='' . build.sh "${swigVersion}" "${pcreVersion}"`,
        {
            streamOutput: true,
            stepName: "Building SWIG for z/OS",
        },
    );
    const filename = `swig-${swigVersion}.pax.Z`;
    await retrieve(connection, [`python/swig/${filename}`], "dist", true);
    console.log(`SWIG package downloaded to dist/${filename}`);
}

/**
 * Checks whether `swig` is available on the remote system using the same PATH
 * the build uses (preBuildCmd is prepended by runCommandInShell). Sets a
 * non-zero exit code when swig is absent so callers can branch on it, e.g.
 * `if npm run -s z:has:swig; then ...`.
 */
async function hasSwig(connection: Client) {
    const out = await runCommandInShell(connection, "command -v swig\n", {
        stepName: "Checking for swig on remote",
        suppressError: true,
    });
    const found = out.trim().length > 0 && !out.includes("not found");
    if (found) {
        console.log(`swig found: ${out.trim()}`);
    } else {
        console.log("swig not found on remote system");
    }
    // Override any exit code set by the suppressed command so the result is explicit.
    process.exitCode = found ? 0 : 1;
}

/**
 * Downloads the precompiled SWIG binary published to SWIG_RELEASE_URL, uploads
 * it to the remote python/swig directory, and extracts it there. The extracted
 * `swig` binary and `Lib/` land directly under that directory (see
 * native/python/swig/README.md), which preBuildCmd puts on PATH and points
 * SWIG_LIB at, so `z:python:build` can find it without building SWIG from
 * source. Used when swig is unavailable on z/OS.
 */
async function installSwigRelease(connection: Client) {
    const cacheDir = path.resolve(__dirname, "./../.cache");
    fs.mkdirSync(cacheDir, { recursive: true });
    const filename = path.basename(SWIG_RELEASE_URL);
    const localPax = path.join(cacheDir, filename);
    await downloadTarball(SWIG_RELEASE_URL, localPax);
    // Checked after downloadTarball rather than inside it so a previously cached (and possibly
    // tampered-with) copy is validated too - downloadTarball is a no-op when the file already exists.
    await verifySha256(localPax, SWIG_RELEASE_SHA256);

    await runCommandInShell(connection, `mkdir -p ${deployDirs.pythonSwigDir}\n`, {
        stepName: "Creating remote SWIG directory",
    });

    // Upload the archive without EBCDIC conversion to preserve its binary contents.
    await new Promise<void>((resolve, reject) => {
        connection.sftp(async (err, sftpcon) => {
            if (err) {
                reject(err);
                return;
            }
            try {
                await uploadFile(sftpcon, localPax, `${deployDirs.pythonSwigDir}/${filename}`, false);
                resolve();
            } catch (uploadErr) {
                reject(uploadErr);
            } finally {
                sftpcon.end();
            }
        });
    });

    await runCommandInShell(
        connection,
        [
            `cd ${deployDirs.pythonSwigDir}`,
            "rm -rf Lib swig",
            `pax -rz -f ${filename}`,
            "chtag -b swig",
            "chmod 755 swig",
        ].join("\n"),
        { stepName: "Extracting SWIG release", streamOutput: true },
    );
    console.log(`Installed SWIG from ${SWIG_RELEASE_URL} to ${deployDirs.pythonSwigDir}`);
}

interface RunCommandOpts {
    streamOutput?: boolean;
    stepName?: string;
    suppressError?: boolean;
    ttyOptions?: PseudoTtyOptions;
}

async function runCommandInShell(connection: Client, command: string, opts?: RunCommandOpts) {
    if (preBuildCmd) {
        command = `${preBuildCmd}\n${command}`;
    }
    // For multi-line commands, show only the first line in the spinner
    const firstLine = command.trim().split("\n")[0];
    const spinnerText = opts?.stepName ?? (command.includes("\n") ? `$ ${firstLine}...` : `$ ${command.trim()}`);
    const spinner = opts?.streamOutput ? null : startSpinner(spinnerText);
    return new Promise<string>((resolve, reject) => {
        let data = "";
        let error = "";
        let hasError = false;
        const cb: ClientCallback = (err, stream) => {
            if (err) {
                if (spinner) stopSpinner(spinner, `Error: ${err.message}`);
                reject(err);
                return;
            }
            stream.on("close", () => {
                if (!hasError && spinner) {
                    stopSpinner(spinner);
                }
                if (!hasError) {
                    // Print any stderr output (warnings) that wasn't already streamed live
                    const trimmedError = error.trim();
                    if (trimmedError.length > 0 && !opts?.streamOutput && !DEBUG_MODE()) {
                        process.stderr.write(`${trimmedError}\n`);
                    }
                    resolve(data);
                }
            });
            stream.on("data", (part: Buffer) => {
                const output = part.toString();
                data += output;
                if (opts?.streamOutput) {
                    process.stdout.write(output);
                }
            });
            stream.stderr.on("data", (err: Buffer) => {
                const errOutput = err.toString();
                error += errOutput;
                if (opts?.streamOutput || DEBUG_MODE()) {
                    process.stderr.write(errOutput);
                }
            });
            stream.on("exit", (exitCode: number) => {
                if (exitCode !== 0) {
                    hasError = true;
                    const fullError = `${error || data}`.trim();
                    if (spinner) stopSpinner(spinner, `\x1b[31m${opts?.stepName ?? "Command"} failed\x1b[0m`);
                    process.exitCode = exitCode;
                    if (opts?.suppressError) {
                        resolve(data);
                    } else {
                        reject(new Error(fullError));
                    }
                }
            });
            stream.end(`${command}\nexit $?\n`);
        };
        connection.shell(opts?.ttyOptions ?? false, cb);
    });
}

async function retrieve(
    connection: Client,
    files: string[],
    targetDir: string,
    useBasename = false,
    convertAscii = false,
) {
    return new Promise<void>((finish) => {
        console.log("Retrieving files...");

        connection.sftp(async (err, sftpcon) => {
            if (err) {
                console.log("Retrieve err");
                throw err;
            }

            for (let i = 0; i < files.length; i++) {
                const absTargetDir = path.resolve(__dirname, `./../${targetDir}`);
                if (!fs.existsSync(`${absTargetDir}`)) fs.mkdirSync(`${absTargetDir}`);
                const to = `${absTargetDir}/${useBasename ? path.basename(files[i]) : files[i]}`;
                const from = `${deployDirs.root}/${files[i]}`;
                await download(sftpcon, from, to, convertAscii);
            }
            console.log("Get complete!");
            finish();
        });
    });
}

async function upload(connection: Client, sshProfile: IProfile) {
    return new Promise<void>((finish) => {
        const spinner = startSpinner("Deploying files...");

        const dirs = getDirs();
        const files = getAllServerFiles();

        connection.sftp(async (err, sftpcon) => {
            if (err) {
                stopSpinner(spinner, `Deploy error!\n${err}`);
                throw err;
            }

            const filteredDirs = args[1] ? dirs.filter((dir) => files.some((file) => file.startsWith(dir))) : dirs;
            for (const dir of ["", ...filteredDirs]) {
                await new Promise<void>((resolve, reject) => {
                    sftpcon.mkdir(`${deployDirs.root}/${dir}`, (err) => {
                        if (err && (err as SftpError).code !== 4) {
                            reject(err); // Ignore if directory already exists
                        } else {
                            resolve();
                        }
                    });
                });
            }

            const pendingUploads = [];
            const watcher = new WatchUtils(connection, sshProfile);
            if (args[1] == null) {
                const packageJson = JSON.parse(fs.readFileSync(path.resolve(__dirname, "../package.json"), "utf-8"));
                try {
                    const gitHash = childProcess.execSync(`git rev-parse --short HEAD`).toString().trim();
                    packageJson.gitHash = `+${gitHash}`;
                } catch {}
                pendingUploads.push(
                    uploadFile(
                        sftpcon,
                        Buffer.from(JSON.stringify(packageJson, null, 2), "utf-8"),
                        `${deployDirs.root}/package.json`,
                    ),
                );
            }
            for (let i = 0; i < files.length; i++) {
                const from = path.resolve(__dirname, `${localDeployDir}/${files[i]}`);
                const to = `${deployDirs.root}/${toPosixPath(files[i])}`;
                pendingUploads.push(uploadFile(sftpcon, from, to));
                watcher.cache[toPosixPath(files[i])] = fs.statSync(from).mtimeMs;
            }
            await Promise.all(pendingUploads);

            stopSpinner(spinner, "Deploy complete!");
            watcher.saveCache();
            finish();
        });
    });
}

async function build(connection: Client) {
    const response = await runCommandInShell(
        connection,
        `cd ${deployDirs.cDir} && make ${BUILD_TYPE_FLAG()} ${IS_PULL_REQUEST_FLAG()}\n`,
        {
            stepName: "Building native/c",
        },
    );
    DEBUG_MODE() && console.log(response);
    console.log("Build complete!");
}

async function make(connection: Client, inDir?: string) {
    const pwd = inDir ?? deployDirs.cDir;
    const targets = args.filter((arg, idx) => idx > 0 && !arg.startsWith("--")).join(" ");
    const response = await runCommandInShell(connection, `cd ${pwd} && make ${targets} ${BUILD_TYPE_FLAG()}\n`, {
        stepName: `Running make ${targets || "all"}`,
    });
    console.log(response);
}

async function test(connection: Client) {
    const localZnpEnv = Object.fromEntries(
        Object.entries(process.env)
            .filter(([key]) => key.startsWith("ZNP_"))
            .map(([key, val]) => [key, val as string]),
    );
    const mergedEnv = { ...configTestEnv, ...localZnpEnv };
    const envPrefix = Object.entries(mergedEnv)
        .map(([key, val]) => `${key}="${val}"`)
        .join(" ");
    // Wrap multi-word pattern in single quotes to avoid shell expansion
    const testPattern = args[1] ? `'${args[1].replaceAll("'", String.raw`'\''`)}'` : "";
    const envString = envPrefix ? `${envPrefix} ` : "";
    const cTestCmd = `cd ${deployDirs.cTestDir} && ${envString}./build-out/ztest_runner ${testPattern}`;
    await runCommandInShell(connection, `${cTestCmd}\n`, {
        streamOutput: true,
        stepName: "Running tests",
        suppressError: true,
    });
    console.log("\nTesting complete!");
    await retrieve(connection, [`c/test/test-results.xml`], "native", false, true);
}

async function testPython(connection: Client) {
    await runCommandInShell(connection, `cd ${deployDirs.pythonTestDir} && make ${BUILD_TYPE_FLAG()}\n`, {
        streamOutput: true,
        stepName: "Running Python binding tests",
        suppressError: true,
    });
    console.log("\nTesting complete!");
    await retrieve(connection, ["python/bindings/test/pybi_results.xml"], "native", false, false);
}

/**
 * Lists the LE/libc++ symbols the built binaries import and downloads the report.
 *
 * Informational, not a gate: the floor is enforced at bind time by pointing LDFLAGS at side decks
 * captured from a system at the minimum supported release. See native/c/compat/README.md.
 */
async function reportRuntimeImports(connection: Client) {
    try {
        await runCommandInShell(connection, `cd ${deployDirs.cDir} && make runtime-imports ${BUILD_TYPE_FLAG()}\n`, {
            streamOutput: true,
            stepName: "Listing runtime imports",
        });
    } finally {
        // Retrieve the report even on failure - that is exactly when it is needed.
        await retrieve(connection, ["c/build-out/runtime-imports.txt"], "listings", true, true);
    }
}

async function buildChdsect(connection: Client, sftpcon: SFTPWrapper, target: string) {
    await uploadFile(
        sftpcon,
        path.resolve(__dirname, `${localDeployDir}/asmchdr/${target}`),
        `${deployDirs.asmchdrDir}/${target}`,
    );
    const response = await runCommandInShell(
        connection,
        `cd ${deployDirs.asmchdrDir} && make build-${target} 2>&1 \n`,
        { stepName: `Building chdsect ${target}` },
    );
    console.log(response);

    const headerName = target.replace(".s", ".h");
    const from = `${deployDirs.asmchdrDir}/build-out/${headerName}`;
    const to = path.resolve(__dirname, `${localDeployDir}/c/chdsect/${headerName}`);
    console.log(`Downloading file from '${from}' to '${to}'`);
    await download(sftpcon, from, to);
}

async function chdsect(connection: Client) {
    const targets =
        args[1] == null
            ? fs.readdirSync(path.resolve(__dirname, `${localDeployDir}/asmchdr`)).filter((f) => f.endsWith(".s"))
            : [args[1]];

    if (targets.length === 0) {
        throw new Error("No .s files found in native/asmchdr");
    }

    console.log(`Building chdsect target(s): ${targets.join(", ")}`);

    return new Promise<void>((finish, reject) => {
        connection.sftp(async (err, sftpcon) => {
            if (err) {
                reject(err);
                return;
            }
            try {
                await uploadFile(
                    sftpcon,
                    path.resolve(__dirname, `${localDeployDir}/asmchdr/gen_chdsect.sh`),
                    `${deployDirs.asmchdrDir}/gen_chdsect.sh`,
                );
                await uploadFile(
                    sftpcon,
                    path.resolve(__dirname, `${localDeployDir}/asmchdr/makefile`),
                    `${deployDirs.asmchdrDir}/makefile`,
                );
                for (const target of targets) {
                    await buildChdsect(connection, sftpcon, target);
                }
                console.log("Chdsect complete!");
            } catch (err) {
                reject(err);
                return;
            } finally {
                sftpcon.end();
            }
            finish();
        });
    });
}

async function clean(connection: Client) {
    console.log(
        await runCommandInShell(connection, `cd ${deployDirs.cDir} && make clean\n`, { stepName: "Cleaning native/c" }),
    );
    console.log(
        await runCommandInShell(connection, `cd ${deployDirs.cTestDir} && make clean\n`, {
            stepName: "Cleaning native/c/test",
        }),
    );
    console.log(
        await runCommandInShell(connection, `cd ${deployDirs.pythonDir} && make clean\n`, {
            stepName: "Cleaning native/python",
        }),
    );
    console.log(
        await runCommandInShell(connection, `cd ${deployDirs.pythonTestDir} && make clean\n`, {
            stepName: "Cleaning native/python/test",
        }),
    );
    console.log("Clean complete");
}

async function rmdir(connection: Client, sshProfile: IProfile) {
    console.log(
        await runCommandInShell(connection, `rm -rf "$(cd ${deployDirs.root} && pwd -P)"\n`, {
            stepName: "Removing deploy directory",
        }),
    );
    console.log("Removal complete");
    const watcher = new WatchUtils(connection, sshProfile);
    watcher.cache = {};
    watcher.saveCache();
}

async function watch(connection: Client, sshProfile: IProfile) {
    await new WatchUtils(connection, sshProfile).start();
    return new Promise<void>((resolve) => connection.on("close", () => resolve()));
}

async function watchTest(connection: Client, sshProfile: IProfile) {
    await new WatchUtils(connection, sshProfile, { runTests: true }).start();
    return new Promise<void>((resolve) => connection.on("close", () => resolve()));
}

async function uploadFile(sftpcon: SFTPWrapper, from: string | Buffer, to: string, convertEbcdic = true) {
    await new Promise<void>((finish) => {
        DEBUG_MODE() && console.log(`Uploading '${from}' to ${to}`);
        pipeline(
            typeof from === "string" ? fs.createReadStream(from) : Readable.from(from),
            convertEbcdic ? new AsciiToEbcdicTransform() : new PassThrough(),
            sftpcon.createWriteStream(to),
            (err) => {
                if (err) {
                    console.log("Upload err");
                    console.log(from, to);
                    throw err;
                }
                finish();
            },
        );
    });
}

async function download(sftpcon: SFTPWrapper, from: string, to: string, convertAscii = false) {
    return new Promise<void>((finish) => {
        console.log(`Downloading '${from}' to ${to}`);
        const callback = (err: Error | null) => {
            if (err) {
                console.log("Get err");
                console.log(from, to);
                throw err;
            }
            finish();
        };
        if (convertAscii) {
            pipeline(sftpcon.createReadStream(from), new EbcdicToAsciiTransform(), fs.createWriteStream(to), callback);
        } else {
            sftpcon.fastGet(from, to, callback);
        }
    });
}

async function loadConfig(): Promise<IConfig> {
    const configPath = path.join(__dirname, "..", "config.yaml");
    if (!fs.existsSync(configPath)) {
        console.error("Could not find config.yaml. See the README for instructions.");
        process.exit(1);
    }

    // biome-ignore lint/suspicious/noExplicitAny: Config file is not strongly typed
    const configYaml: any = yaml.load(fs.readFileSync(configPath, "utf-8"));
    let activeProfile = configYaml.activeProfile;
    const profileArgIdx = args.findIndex((arg) => arg.startsWith("--profile="));
    if (profileArgIdx !== -1) {
        activeProfile = args.splice(profileArgIdx, 1)[0].split("=").pop();
    }
    if (configYaml.profiles?.[activeProfile] == null) {
        console.error(`Could not find profile "${activeProfile}" in config.yaml. See the README for instructions.`);
        process.exit(1);
    }

    const config: IConfig = configYaml.profiles[activeProfile];
    if (typeof config.sshProfile === "string") {
        const profInfo = new ProfileInfo("zowe");
        await profInfo.readProfilesFromDisk();
        const profAttrs = profInfo.getAllProfiles("ssh").find((prof) => prof.profName === config.sshProfile);
        const mergedArgs = profInfo.mergeArgsForProfile(profAttrs, { getSecureVals: true });
        config.sshProfile = Object.fromEntries(mergedArgs.knownArgs.map((arg) => [arg.argName, arg.argValue]));
    }
    config.deployDir = config.deployDir.replace(/^~/, ".");
    return config;
}

async function testConnection(client: Client): Promise<void> {
    return new Promise((resolve, reject) => {
        client.exec('echo "SSH connection established successfully"', (err, stream) => {
            if (err) {
                return reject(err);
            }
            stream
                .on("close", (code: number) => {
                    if (code === 0) {
                        resolve();
                    } else {
                        reject(new Error(`SSH test failed with exit code ${code}`));
                    }
                })
                .on("data", () => {})
                .stderr.on("data", (data: Buffer) => {
                    console.error(`STDERR: ${data}`);
                });
        });
    });
}

async function buildSshClient(sshProfile: IProfile): Promise<Client> {
    const client = new Client();
    return new Promise((resolve, reject) => {
        client.on("close", () => {
            console.log("Client connection is closed");
        });
        client.on("error", (err) => {
            console.error("Client connection errored");
            process.exitCode = 1;
            reject(err);
        });
        client.on("ready", () => resolve(client));
        client.connect({
            ...sshProfile,
            username: sshProfile.user,
            privateKey: sshProfile.privateKey ? fs.readFileSync(sshProfile.privateKey) : undefined,
            keepaliveInterval: 30e3,
        });
    });
}

/**
 * Finds the REST comment id of the most recent comment on the PR that was
 * authored by the current user and contains the sticky marker. Returns null if
 * no such comment exists (so a fresh one should be created).
 */
function findStickyCommentId(prNumber: string): number | null {
    const comments = JSON.parse(gh(["pr", "view", prNumber, "--json", "comments"])).comments as Array<{
        body: string;
        url: string;
        viewerDidAuthor: boolean;
        createdAt: string;
    }>;
    const mine = comments
        .filter((c) => c.viewerDidAuthor && c.body.includes(STICKY_MARKER))
        .sort((a, b) => a.createdAt.localeCompare(b.createdAt));
    const latest = mine.at(-1);
    if (!latest) {
        return null;
    }
    // gh exposes a GraphQL node id; the numeric REST id needed for PATCH lives in the URL.
    const match = latest.url.match(/#issuecomment-(\d+)/);
    return match ? Number(match[1]) : null;
}

/** Uploads available Python artifacts to the dev prerelease and links them in a sticky PR comment. */
function postPrecompiledBindings(prNumber: string) {
    if (!prNumber || !/^\d+$/.test(prNumber)) {
        throw new Error("Usage: npm run z:python:post -- <PR_NUMBER>");
    }

    const distDir = path.dirname(TARBALL);
    const wheels = fs.existsSync(distDir)
        ? fs.readdirSync(distDir).filter((name) => name.startsWith("zbind-") && name.endsWith(".whl"))
        : [];
    if (wheels.length > 1) {
        throw new Error(
            "Multiple zbind wheels found in dist/. Keep the wheel you want to post and move the others elsewhere.",
        );
    }
    if (!fs.existsSync(TARBALL) && wheels.length === 0) {
        throw new Error(
            "No Python artifacts found in dist/. Run npm run z:python:pack or npm run z:python:wheel first.",
        );
    }

    const hash = getShortHash();
    const artifacts: Array<{ source: string; name: string; label: string }> = [];
    if (fs.existsSync(TARBALL)) {
        artifacts.push({ source: TARBALL, name: `zbind_bin_dist-pr${prNumber}-${hash}.tar.gz`, label: "Tarball" });
    }
    if (wheels.length) {
        const parts = wheels[0].split("-");
        if (parts.length !== 5 && parts.length !== 6) throw new Error(`Invalid wheel filename: ${wheels[0]}`);
        // A numeric build tag preserves a pip-installable filename while distinguishing PR builds.
        const name = [...parts.slice(0, 2), `1pr${prNumber}g${hash}`, ...parts.slice(-3)].join("-");
        artifacts.push({ source: path.join(distDir, wheels[0]), name, label: "Wheel" });
    }
    const staging = fs.mkdtempSync(path.join(distDir, ".python-post-"));
    try {
        const staged = artifacts.map((artifact) => {
            const destination = path.join(staging, artifact.name);
            fs.copyFileSync(artifact.source, destination);
            return destination;
        });
        const wheel = artifacts.find((artifact) => artifact.label === "Wheel");
        if (wheel) {
            const checksum = crypto
                .createHash("sha256")
                .update(fs.readFileSync(path.join(staging, wheel.name)))
                .digest("hex");
            const name = `${wheel.name}.sha256`;
            const destination = path.join(staging, name);
            fs.writeFileSync(destination, `${checksum}  ${wheel.name}\n`);
            staged.push(destination);
            artifacts.push({ source: destination, name, label: "Wheel SHA-256" });
        }
        const repo = gh(["repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"]);

        // Ensure the shared prerelease exists; create it only the first time.
        let releaseExists = true;
        try {
            childProcess.execFileSync("gh", ["release", "view", RELEASE_TAG], { stdio: "ignore" });
        } catch {
            releaseExists = false;
        }
        if (!releaseExists) {
            console.log(`Creating prerelease "${RELEASE_TAG}"...`);
            gh(["release", "create", RELEASE_TAG, "--prerelease", "--title", RELEASE_TITLE, "--notes", RELEASE_NOTES]);
        }

        console.log(`Uploading ${artifacts.length} Python artifact(s)...`);
        gh(["release", "upload", RELEASE_TAG, ...staged, "--clobber"]);
        const assets = JSON.parse(gh(["release", "view", RELEASE_TAG, "--json", "assets"])).assets as Array<{
            name: string;
            url: string;
        }>;
        const downloads = artifacts.map((artifact) => ({
            ...artifact,
            url:
                assets.find((asset) => asset.name === artifact.name)?.url ??
                `https://github.com/${repo}/releases/download/${RELEASE_TAG}/${artifact.name}`,
        }));
        const body = [
            `### ${COMMENT_HEADER}`,
            "",
            `Built from \`${hash}\` on z/OS.`,
            "",
            ...downloads.map((artifact) => `- **${artifact.label}:** [\`${artifact.name}\`](${artifact.url})`),
            "",
            `<sub>Hosted as assets on the \`${RELEASE_TAG}\` prerelease (dev artifacts, not for distribution).</sub>`,
        ].join("\n");

        const existingCommentId = findStickyCommentId(prNumber);
        if (existingCommentId != null) {
            console.log(`Updating existing comment ${existingCommentId} on PR #${prNumber}...`);
            const input = path.join(staging, "comment.json");
            fs.writeFileSync(input, JSON.stringify({ body }));
            gh(["api", "--method", "PATCH", `repos/${repo}/issues/comments/${existingCommentId}`, "--input", input]);
        } else {
            console.log(`Posting comment to PR #${prNumber}...`);
            const input = path.join(staging, "comment.md");
            fs.writeFileSync(input, body);
            gh(["pr", "comment", prNumber, "--body-file", input]);
        }

        console.log(`\n✅ ${existingCommentId != null ? "Updated" : "Posted"} Python bindings on PR #${prNumber}`);
        for (const artifact of downloads) console.log(`   ${artifact.label}: ${artifact.url}`);
    } finally {
        fs.rmSync(staging, { recursive: true, force: true });
    }
}

async function main() {
    switch (args[0]) {
        case "python:post":
            postPrecompiledBindings(args[1]);
            return;
    }
    const config = await loadConfig();
    preBuildCmd = config.preBuildCmd;
    configTestEnv = config.testEnv ?? {};
    deployDirs = {
        root: config.deployDir,
        cDir: `${config.deployDir}/c`,
        asmchdrDir: `${config.deployDir}/asmchdr`,
        cTestDir: `${config.deployDir}/c/test`,
        pythonDir: `${config.deployDir}/python/bindings`,
        pythonTestDir: `${config.deployDir}/python/bindings/test`,
        pythonSwigDir: `${config.deployDir}/python/swig`,
    };
    const sshClient = await buildSshClient(config.sshProfile as IProfile);
    await testConnection(sshClient);
    try {
        switch (args[0]) {
            case "artifacts":
                await artifacts(sshClient, false);
                break;
            case "build":
                await build(sshClient);
                break;
            case "build:chdsect":
                await chdsect(sshClient);
                break;
            case "python:build":
                await make(sshClient, deployDirs.pythonDir);
                break;
            case "clean":
                await clean(sshClient);
                break;
            case "delete":
                await rmdir(sshClient, config.sshProfile as IProfile);
                break;
            case "imports":
                await reportRuntimeImports(sshClient);
                break;
            case "make":
                await make(sshClient);
                break;
            case "build:swig":
                await buildSwig(sshClient);
                break;
            case "has:swig":
                await hasSwig(sshClient);
                break;
            case "python:swig:install":
                await installSwigRelease(sshClient);
                break;
            case "python:wheel":
                await buildPythonWheel(sshClient, config);
                break;
            case "python:pack":
                await packPrecompiled(sshClient);
                break;
            case "package":
                await artifacts(sshClient, true);
                break;
            case "rebuild":
                await upload(sshClient, config.sshProfile as IProfile);
                await build(sshClient);
                break;
            case "test":
                await test(sshClient);
                break;
            case "python:test":
                await testPython(sshClient);
                break;
            case "upload":
                await upload(sshClient, config.sshProfile as IProfile);
                break;
            case "watch":
                await watch(sshClient, config.sshProfile as IProfile);
                break;
            case "watch:test":
                await watchTest(sshClient, config.sshProfile as IProfile);
                break;
            default:
                console.error(`Unsupported command "${args[0]}". See README for instructions.`);
                break;
        }
    } finally {
        sshClient.end();
    }
}

if (path.resolve(process.argv[1]) === path.resolve(__filename)) {
    main().catch((err) => {
        const errorMessage = err instanceof Error ? err.message : String(err);
        if (errorMessage) {
            console.error(`\n${errorMessage}`);
        }
        process.exit(process.exitCode ?? 1);
    });
}
