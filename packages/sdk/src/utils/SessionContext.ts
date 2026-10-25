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
import { type Config, NodeSSH } from "node-ssh";
import { ZSshUtils } from "../ZSshUtils";

export class SessionContext implements Disposable {
    private sshConn?: NodeSSH;
    private connectPromise?: Promise<NodeSSH>;

    /**
     * @param session Session whose credentials are used to (re)connect
     * @param existingConnection An already authenticated connection to adopt, so no new authentication is needed
     * until it drops. The context takes ownership and disposes it.
     */
    constructor(
        private readonly session: SshSession,
        existingConnection?: NodeSSH,
    ) {
        if (existingConnection != null) {
            this.sshConn = existingConnection;
            this.connectPromise = Promise.resolve(existingConnection);
        }
    }

    public [Symbol.dispose](): void {
        if (this.sshConn != null) {
            this.sshConn.dispose();
        } else if (this.connectPromise != null) {
            // A connect is still in flight (e.g. dispose ran while concurrent calls were still
            // establishing it). Dispose must stay synchronous, so dispose it once it settles
            // instead of leaking the socket; swallow a failed connect because getSsh handles
            // the error and disposes the attempted connection.
            this.connectPromise.then(
                (ssh) => ssh.dispose(),
                () => {},
            );
        }
    }

    /**
     * Relinquish the live connection to the caller (e.g. to start the server on it) so disposing this context
     * no longer ends it. The caller owns the returned connection. Undefined if there is no live connection.
     */
    public async releaseSsh(): Promise<NodeSSH | undefined> {
        let ssh: NodeSSH | undefined;
        try {
            ssh = await this.connectPromise;
        } catch {
            ssh = undefined;
        }
        this.sshConn = undefined;
        this.connectPromise = undefined;
        if (ssh != null && !ssh.isConnected()) {
            ssh.dispose();
            return undefined;
        }
        return ssh;
    }

    public get ISshSession(): ISshSession {
        return this.session.ISshSession;
    }

    public async getSsh(): Promise<NodeSSH> {
        if (this.sshConn != null && !this.sshConn.isConnected()) {
            this.sshConn.dispose();
            this.sshConn = undefined;
            this.connectPromise = undefined;
        }
        this.connectPromise ??= (async () => {
            const ssh = new NodeSSH();
            try {
                await ssh.connect(ZSshUtils.buildSshConfig(this.session) as Config);
                this.sshConn = ssh;
                return ssh;
            } catch (error) {
                ssh.dispose();
                throw error;
            }
        })();
        try {
            return await this.connectPromise;
        } catch (error) {
            this.connectPromise = undefined;
            throw error;
        }
    }
}
