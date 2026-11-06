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

/**
 * For a detailed explanation regarding each configuration property, visit:
 * https://vitest.dev/config/
 */

import { defineConfig, mergeConfig } from "vitest/config";
import rootConfig from "../../vitest.config";

// biome-ignore lint/correctness/noUnusedVariables: removing `projects` variable breaks tests
const { projects, ...sharedTestConfig } = rootConfig.test || {};
const sharedConfig = { ...rootConfig, test: sharedTestConfig };

export default mergeConfig(
    sharedConfig,
    defineConfig({
        test: {
            name: "zowex-for-zowe-cli",
        },
    }),
);
