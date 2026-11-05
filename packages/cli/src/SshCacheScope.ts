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

import { ImperativeConfig } from "@zowe/imperative";
import { SshClientCache } from "@zowe/zowex-for-zowe-sdk";

/** Retains connections across daemon commands and releases them after standalone CLI workflows. */
export class SshCacheScope implements Disposable {
    public static get isDaemon(): boolean {
        return ImperativeConfig.instance.daemonContext?.stream != null;
    }

    private readonly retainConnections = SshCacheScope.isDaemon;

    public [Symbol.dispose](): void {
        if (!this.retainConnections) SshClientCache.inst.dispose();
    }
}
