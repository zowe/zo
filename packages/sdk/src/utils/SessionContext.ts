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

    constructor(private readonly session: SshSession) {}

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
