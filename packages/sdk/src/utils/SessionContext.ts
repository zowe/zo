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

import type { ISshSession, SshSession } from "@zowe/zos-uss-for-zowe-sdk";
import type { NodeSSH } from "node-ssh";
import { SshClientCache, type SshConnectionLease } from "../SshClientCache";

/**
 * Compatibility wrapper for a scoped cache lease.
 * @deprecated Pass SshSession directly to SDK methods. Hold a SshClientCache lease for a longer workflow if needed.
 */
export class SessionContext implements Disposable {
    private lease?: Promise<SshConnectionLease>;
    private disposed = false;
    private activeLease?: SshConnectionLease;

    constructor(private readonly session: SshSession) {}

    public [Symbol.dispose](): void {
        if (this.disposed) return;
        this.disposed = true;
        this.lease?.then(
            (lease) => lease[Symbol.dispose](),
            () => {},
        );
    }

    public get ISshSession(): ISshSession {
        return this.session.ISshSession;
    }

    public async getSsh(): Promise<NodeSSH> {
        if (this.disposed) throw new Error("SSH session context has been disposed");
        if (this.activeLease && !this.activeLease.ssh.isConnected()) {
            this.activeLease[Symbol.dispose]();
            this.activeLease = undefined;
            this.lease = undefined;
        }
        this.lease ??= SshClientCache.inst.acquire(this.session);
        try {
            const lease = await this.lease;
            this.activeLease = lease;
            return lease.ssh;
        } catch (error) {
            this.lease = undefined;
            throw error;
        }
    }
}
