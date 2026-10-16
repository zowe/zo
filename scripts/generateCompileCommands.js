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

const fs = require("node:fs");
const path = require("node:path");

// Local clangd parsing flags; the z/OS build remains controlled by its Makefiles.
const nativeDir = path.resolve(__dirname, "../native");
const flags = fs.readFileSync(path.join(nativeDir, "compile_flags.txt"), "utf8").trim().split(/\r?\n/);

function cppSources(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const filename = path.join(directory, entry.name);
    return entry.isDirectory() ? cppSources(filename) : filename.endsWith(".cpp") ? [filename] : [];
  });
}

const sources = [
  ...cppSources(path.join(nativeDir, "c")),
  ...fs.readdirSync(path.join(nativeDir, "python/bindings"))
    .filter((name) => name.endsWith("_py.cpp"))
    .map((name) => path.join(nativeDir, "python/bindings", name)),
];
const commands = sources.sort().map((file) => ({
  directory: nativeDir,
  file,
  arguments: ["clang++", "-std=c++17", ...flags, "-c", file],
}));
const output = path.join(nativeDir, "compile_commands.json");
fs.writeFileSync(output, `${JSON.stringify(commands, null, 2)}\n`);
console.log(`Wrote ${commands.length} C++ navigation commands to ${output}`);
