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

import { Readable, type Stream, Writable } from "node:stream";
import { pipeline } from "node:stream/promises";
import { Logger } from "@zowe/imperative";
import { Base64Decode, Base64Encode } from "base64-stream";
import type { CallbackInfo, CommandResponse, RpcNotification, RpcPromise, RpcRequest } from "./doc";
import type { ReadDatasetRequest } from "./doc/rpc/ds";
import type { ReadFileRequest } from "./doc/rpc/uss";
import { ProgressTransform } from "./ProgressTransform";
import type { Client, ClientChannel } from "./ssh-rs";
import { ZSshUtils } from "./ZSshUtils";

type StreamData = {
    streamFn: () => Stream;
    callbackInfo?: CallbackInfo;
    resourceName?: string;
    timeoutId: NodeJS.Timeout;
};
type StreamMode = "GET" | "PUT";

export class RpcStreamManager {
    private readonly mPendingStreamMap: Map<number, StreamData> = new Map();

    public constructor(private readonly mSshClient: Client) {}

    public registerStream(
        request: RpcRequest,
        stream: () => Stream,
        timeoutId?: NodeJS.Timeout,
        callbackInfo?: CallbackInfo,
    ): void {
        this.mPendingStreamMap.set(request.id, {
            streamFn: stream,
            timeoutId,
            callbackInfo,
            resourceName: (request.params as ReadDatasetRequest).dsname ?? (request.params as ReadFileRequest).fspath,
        });
        request.params.stream = request.id;
    }

    public linkStreamToPromise(rpcPromise: RpcPromise, notif: RpcNotification, mode: StreamMode): void {
        const { reject, resolve } = rpcPromise;
        const { resourceName } = this.mPendingStreamMap.get(notif.params.id)!;
        const streamPromise = mode === "PUT" ? this.uploadStream(notif.params) : this.downloadStream(notif.params);
        rpcPromise.resolve = async (response: CommandResponse) => {
            try {
                const clientLen = await streamPromise;
                this.expectContentLengthMatches(response, clientLen, mode, resourceName);
                resolve(response);
            } catch (err) {
                reject(err);
            }
        };
    }

    private async uploadStream(params: { id: number; pipePath: string }): Promise<number> {
        const pendingStream = this.mPendingStreamMap.get(params.id)!;
        const readStream = pendingStream.streamFn();
        const callbackInfo = pendingStream.callbackInfo;
        if (readStream == null || !(readStream instanceof Readable)) {
            throw new Error(`No stream found for request ID: ${params.id}`);
        }
        readStream.on("keepAlive", () => pendingStream.timeoutId?.refresh());
        this.mPendingStreamMap.delete(params.id);

        const sshStream = await new Promise<ClientChannel>((resolve, reject) => {
            // Need to use sh built-in version of cat that converts to EBCDIC
            this.mSshClient.exec(`exec /bin/sh -c "cat > ${ZSshUtils.quotePath(params.pipePath)}"`, (err, stream) =>
                err ? reject(err) : resolve(stream),
            );
        });
        const progressTransform = new ProgressTransform(callbackInfo, () => readStream.emit("keepAlive"));

        await pipeline(readStream, progressTransform, new Base64Encode(), sshStream.stdin);
        return progressTransform.bytesProcessed;
    }

    private async downloadStream(params: { id: number; pipePath: string; contentLen?: number }): Promise<number> {
        const pendingStream = this.mPendingStreamMap.get(params.id)!;
        const writeStream = pendingStream.streamFn();
        const callbackInfo = pendingStream.callbackInfo;
        if (writeStream == null || !(writeStream instanceof Writable)) {
            throw new Error(`No stream found for request ID: ${params.id}`);
        }
        writeStream.on("keepAlive", () => pendingStream.timeoutId?.refresh());
        this.mPendingStreamMap.delete(params.id);
        if (callbackInfo != null && params.contentLen != null) {
            callbackInfo.totalBytes = params.contentLen;
        }

        const sshStream = await new Promise<ClientChannel>((resolve, reject) => {
            // Need to use sh built-in version of cat that converts to EBCDIC
            this.mSshClient.exec(`exec /bin/sh -c "cat ${ZSshUtils.quotePath(params.pipePath)}"`, (err, stream) =>
                err ? reject(err) : resolve(stream),
            );
        });
        const progressTransform = new ProgressTransform(callbackInfo, () => writeStream.emit("keepAlive"));

        await pipeline(sshStream.stdout, new Base64Decode(), progressTransform, writeStream);
        return progressTransform.bytesProcessed;
    }

    private expectContentLengthMatches(
        response: CommandResponse,
        clientLen: number,
        mode: StreamMode,
        resourceName?: string,
    ): void {
        if ("contentLen" in response && response.contentLen != null && response.contentLen !== clientLen) {
            const errMsg = `Content length mismatch${resourceName != null ? ` for ${resourceName}` : ""}`;
            const errDetails =
                mode === "GET"
                    ? `server sent ${response.contentLen}, client received ${clientLen}`
                    : `client sent ${clientLen}, server received ${response.contentLen}`;
            throw new Error(Logger.getAppLogger().error(`${errMsg}: ${errDetails}`));
        }
    }
}
