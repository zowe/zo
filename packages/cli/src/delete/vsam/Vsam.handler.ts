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

import type { IHandlerParameters } from "@zowe/imperative";
import type { ds, ZSshClient } from "@zowe/zowex-for-zowe-sdk";
import { SshBaseHandler } from "../../SshBaseHandler";

export default class DeleteVsamHandler extends SshBaseHandler {
    public async processWithClient(params: IHandlerParameters, client: ZSshClient): Promise<ds.DeleteVsamResponse> {
        const dsname = params.arguments.name;
        const response = await client.ds.deleteVsam({
            dsname,
            erase: params.arguments.erase,
            purge: params.arguments.purge,
        });

        params.response.data.setObj(response);
        if (response.success) {
            const message = `VSAM cluster "${dsname}" deleted`;
            params.response.data.setMessage(message);
            params.response.console.log(message);
        }
        return response;
    }
}
