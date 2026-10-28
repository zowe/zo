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
import { B64String, type certificates, type ZSshClient } from "@zowe/zowex-for-zowe-sdk";
import { SshBaseHandler } from "../../../SshBaseHandler";

export default class ExportCertHandler extends SshBaseHandler {
    public async processWithClient(
        params: IHandlerParameters,
        client: ZSshClient,
    ): Promise<certificates.ExportCertificateResponse> {
        const { owner, keyring, label, format, file, dsn, password } = params.arguments;
        const response = await client.certificates.exportCertificate({
            owner,
            keyring,
            label,
            format,
            file,
            dsn,
            password,
        });

        if (response.file || response.dsn) {
            const dest = response.file ? response.file : `data set ${response.dsn}`;
            const message = `Certificate '${label}' exported to ${dest} on the server (${response.bytesWritten} bytes, ${response.format}).`;
            params.response.data.setMessage(message);
            params.response.console.log(message);
        } else {
            // No server-side file: the native command only allows this for PEM,
            // which it returns as base64 in `data`. Print the PEM to stdout.
            params.response.data.setMessage("Exported certificate '%s' (%s)", label, response.format);
            params.response.console.log(B64String.decode(response.data));
        }
        return response;
    }
}
