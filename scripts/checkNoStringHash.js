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

// Rejects string-keyed hash containers in the shipping native build.
//
// std::hash<std::string> and std::hash<std::string_view> resolve to the out-of-line libc++ helper
// std::__1_e::__hash_memory, which is not exported by CRTEQCXE on z/OS systems below the required
// Language Environment maintenance level. A binary that references it dies at load time with
// CEE3561S, before main() runs, so nothing in the binary can diagnose itself.
//
// Integer-keyed hash containers are fine: std::hash<int> is the identity functor.
//
// See native/c/compat/README.md and https://github.com/zowe/zowex/issues/871.

const fs = require("node:fs");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "../native/c");
const SKIP_DIRS = new Set(["test", "chdsect", "build-out", "examples"]);
const EXTENSIONS = new Set([".c", ".cpp", ".h", ".hpp"]);

const PATTERNS = [
    /unordered_(?:map|set)\s*<\s*(?:std::)?string(?:_view)?\s*[,>]/,
    /unordered_(?:map|set)\s*<\s*(?:const\s+)?char\s*\*/,
    /\bhash\s*<\s*(?:std::)?string(?:_view)?\s*>/,
];

function collect(dir, out) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            if (!SKIP_DIRS.has(entry.name)) collect(full, out);
        } else if (EXTENSIONS.has(path.extname(entry.name))) {
            out.push(full);
        }
    }
    return out;
}

const findings = [];
for (const file of collect(ROOT, [])) {
    const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
    lines.forEach((line, idx) => {
        // Skip comments so the explanatory notes next to the ordered containers do not trip this.
        if (/^\s*(?:\/\/|\*|\/\*)/.test(line)) return;
        if (PATTERNS.some((pattern) => pattern.test(line))) {
            findings.push(`${path.relative(process.cwd(), file)}:${idx + 1}: ${line.trim()}`);
        }
    });
}

if (findings.length > 0) {
    console.error("String-keyed hash container(s) found in the shipping native build:\n");
    for (const finding of findings) console.error(`  ${finding}`);
    console.error(
        [
            "",
            "std::hash<std::string> pulls in the libc++ symbol std::__1_e::__hash_memory, which is not",
            "exported by CRTEQCXE on z/OS systems below the required Language Environment maintenance",
            "level. zo then fails to load with CEE3561S.",
            "",
            "Use std::map / std::set instead (zjson::ObjectMap, ast::ObjMap and plugin::ArgumentMap are",
            "already ordered). See native/c/compat/README.md and https://github.com/zowe/zowex/issues/871.",
            "",
        ].join("\n"),
    );
    process.exit(1);
}

console.log("No string-keyed hash containers in the shipping native build.");
