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

import { EventEmitter } from "node:stream";
import type { ClientCallback, ConnectConfig } from "ssh2";

export class Client extends EventEmitter {
    public connect(_config: ConnectConfig) {}
    public end() {}
    public exec(_command: string, _callback: ClientCallback) {}
    public shell(_callback: ClientCallback) {}
    public forwardOut(_src: string, _srcPort: number, _dest: string, _destPort: number, _callback: ClientCallback) {}
}
