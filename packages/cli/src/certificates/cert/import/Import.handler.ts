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
import type { certificates, ZSshClient } from "@zowe/zowex-for-zowe-sdk";
import { SshBaseHandler } from "../../../SshBaseHandler";

export default class ImportCertHandler extends SshBaseHandler {
    public async processWithClient(
        params: IHandlerParameters,
        client: ZSshClient,
    ): Promise<certificates.ImportCertificateResponse> {
        const { owner, keyring, label, usage, file, dsn, password } = params.arguments;
        const response = await client.certificates.importCertificate({
            owner,
            keyring,
            label,
            usage,
            file,
            dsn,
            password,
            skipRefresh: params.arguments.skipRefresh,
        });

        params.response.data.setMessage("Certificate '%s' imported into %s/%s", response.label, owner, keyring);
        params.response.console.log("Certificate '%s' imported into %s/%s", response.label, owner, keyring);
        if (response.warning) {
            params.response.console.log("Note: %s", response.warning);
        }
        return response;
    }
}
