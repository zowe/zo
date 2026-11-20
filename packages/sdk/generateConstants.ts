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

import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { loadLicenseHeader } from "../../scripts/generateTypes";

const main = () => {
    try {
        const licenseHeader = loadLicenseHeader();
        const constantsClassTemplate = `${licenseHeader}
// Generated via generateConstants.ts
export const BUNDLED_SSH_SERVER_VERSION = "{{version}}";
export const RUSSH_BINARY_SHA256: Record<string, string> = {{russhHashes}};
`;
        let versionValue = process.env.ZO_RELEASE_VERSION;
        if (!versionValue) {
            const packageJsonContent = fs.readFileSync(path.resolve(__dirname, "..", "..", "package.json"));
            const packageJsonObj = JSON.parse(packageJsonContent.toString());

            if (!packageJsonObj.version) {
                throw new Error("No version field found in package.json");
            }
            versionValue = packageJsonObj.version;
        }

        const russhDir = path.resolve(__dirname, "..", "..", "node_modules", "russh");
        const russhBinaries = fs
            .readdirSync(russhDir)
            .filter((file) => file.endsWith(".node"))
            .sort();
        if (russhBinaries.length === 0) {
            throw new Error("No russh .node binaries found in node_modules/russh");
        }
        const russhHashes: Record<string, string> = {};
        for (const file of russhBinaries) {
            const triple = file.match(/^russh\.(.+)\.node$/)[1];
            const fileBuffer = fs.readFileSync(path.join(russhDir, file));
            russhHashes[triple] = crypto.createHash("sha256").update(fileBuffer).digest("hex");
        }

        const output = constantsClassTemplate
            .replace("{{version}}", versionValue)
            .replace("{{russhHashes}}", JSON.stringify(russhHashes, null, 4).replace(/"(\n\s*\})/g, '",$1'));
        const outputPath = path.resolve(__dirname, "src", "ZSshConstants.ts");
        fs.writeFileSync(outputPath, output);
    } catch (e) {
        console.error("Encountered an error trying to write out ZSshConstants.ts::", e);
        process.exit(1);
    }
};
main();
