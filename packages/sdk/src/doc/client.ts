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

import type { Client } from "../ssh-rs";
import type { CommandRequest, CommandResponse } from "./rpc/common";
import type { ProgressCallback, RpcPromise } from "./types";

export interface ClientOptions {
    /**
     * Maximum number of unanswered keep-alive messages before the connection is considered dead
     * (default: 3)
     */
    keepAliveCountMax?: number;

    /**
     * Number of seconds between keep-alive messages
     * (default: 30)
     */
    keepAliveInterval?: number;

    /**
     * Function called when the connection is closed
     */
    onClose?: () => void | Promise<void>;

    /**
     * Function called when there is a connection error
     */
    onError?: (error: Error) => void | Promise<void>;

    /**
     * Number of workers to spawn
     */
    numWorkers?: number;

    /**
     * Request timeout in seconds before a worker is restarted
     * (default: 60)
     */
    requestTimeout?: number;

    /**
     * Number of seconds to wait for a response
     * (default: 60)
     */
    responseTimeout?: number;

    /**
     * Remote path of the Zowe Remote SSH server
     * (default: `~/.zowe-server`)
     */
    serverPath?: string;

    /**
     * Number of seconds to wait for the SSH connection and Zowe Remote SSH server to start before failing.
     * Prevents hanging when the connection drops during startup.
     * (default: 60)
     */
    serverStartupTimeout?: number;

    /**
     * An already connected and authenticated ssh2 client to start the server on, instead of opening a new
     * connection (and authenticating again, e.g. with MFA). The client takes ownership and ends it on dispose or
     * failed startup. Ignored when `useNativeSsh` is set, in which case the caller still owns it.
     * Keep-alive can only be configured when connecting, so it is the caller's responsibility for this connection.
     */
    existingConnection?: Client;

    /**
     * Use experimental native SSH client for improved performance
     * (default: false)
     */
    useNativeSsh?: boolean;

    /**
     * Enable verbose logging from the server
     * (default: false)
     */
    verbose?: boolean;

    /**
     * A set of requests to run after the client starts.
     * Typically previously failed requests queued for re-run.
     * (default: empty)
     */
    requests?: Set<ExistingClientRequest>;
}

export interface ExistingClientRequest {
    command: CommandRequest;
    rpc: RpcPromise;
    silenced: boolean;
    timeoutId: NodeJS.Timeout;
}

export interface IRpcClient {
    request(request: CommandRequest, progressCallback?: ProgressCallback): Promise<CommandResponse>;
}
