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

const { execSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const outDir = path.resolve(process.argv[2]);
process.chdir(__dirname + "/..");
fs.mkdirSync(outDir, { recursive: true });
const tempDir = fs.mkdtempSync("build-");

try {
    const execOptions = { cwd: tempDir, stdio: "inherit" };
    fs.cpSync(path.join("packages", "cli"), tempDir, { recursive: true });
    fs.copyFileSync("package-lock.json", path.join(tempDir, "npm-shrinkwrap.json"));
    execSync("npm install --ignore-scripts", execOptions);

    const sdkDir = path.join("node_modules", "@zowe/zowex-for-zowe-sdk");
    // npm 12 installs this workspace link as a directory. unlink only removes a symlink.
    fs.rmSync(path.join(tempDir, sdkDir), { recursive: true, force: true });
    fs.cpSync(fs.realpathSync(sdkDir), path.join(tempDir, sdkDir), { recursive: true });
    fs.rmSync(path.join(tempDir, "node_modules", "cpu-features"), { recursive: true, force: true });
    fs.rmSync(path.join(tempDir, "node_modules", "russh"), { recursive: true, force: true });
    execSync(`npm pack --pack-destination="${outDir}"`, execOptions);
} finally {
    fs.rmSync(tempDir, { recursive: true, force: true });
}
