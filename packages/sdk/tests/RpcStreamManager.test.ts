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

import { PassThrough, Readable, Writable } from "node:stream";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { CallbackInfo, RpcNotification, RpcPromise, RpcRequest } from "../src/doc";
import { RpcStreamManager } from "../src/RpcStreamManager";

describe("RpcStreamManager", () => {
    let mockSshClient: any;
    let streamManager: RpcStreamManager;

    beforeEach(() => {
        mockSshClient = {
            exec: vi.fn(),
        };
        streamManager = new RpcStreamManager(mockSshClient);
    });

    describe("registerStream", () => {
        it("should register a stream for a dataset request", () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 123,
                method: "readDataset",
                params: {
                    dsname: "USER.DATASET",
                },
            };
            const mockStream = () => new PassThrough();
            const timeoutId = { refresh: vi.fn() } as any;
            const callbackInfo: CallbackInfo = { callback: vi.fn(), totalBytes: 100 };

            streamManager.registerStream(request, mockStream, timeoutId, callbackInfo);

            expect(request.params.stream).toBe(123);
        });

        it("should register a stream for a file request", () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 456,
                method: "readFile",
                params: {
                    fspath: "/tmp/file.txt",
                },
            };
            const mockStream = () => new PassThrough();

            streamManager.registerStream(request, mockStream);

            expect(request.params.stream).toBe(456);
        });
    });

    describe("linkStreamToPromise", () => {
        it("should link a PUT stream and resolve the promise when content length matches", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 1,
                method: "writeDataset",
                params: {
                    dsname: "USER.DATASET",
                },
            };

            const dataToUpload = "Hello, Zowe!";
            const readStream = Readable.from([dataToUpload]);
            const timeoutId = { refresh: vi.fn() } as any;

            streamManager.registerStream(request, () => readStream, timeoutId);

            const rpcPromise: RpcPromise = {
                rpc: { id: 1, method: "writeDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: {
                    id: 1,
                    pipePath: "/tmp/pipe_1",
                },
            };

            // Mock SSH exec for upload using a Writable stream
            let uploadedData = "";
            const mockChannel = {
                stdin: new Writable({
                    write(chunk, _encoding, cb) {
                        uploadedData += chunk.toString();
                        cb();
                    },
                }),
            };
            mockSshClient.exec.mockImplementation((cmd, cb) => {
                expect(cmd).toBe(`exec /bin/sh -c "cat > /tmp/pipe_1"`);
                cb(null, mockChannel);
            });

            streamManager.linkStreamToPromise(rpcPromise, notif, "PUT");

            // Simulate server response
            const response = {
                success: true,
                contentLen: dataToUpload.length,
            };
            await rpcPromise.resolve(response);

            // Verify base64 encoded data was written to stdin
            const expectedBase64 = Buffer.from(dataToUpload).toString("base64");
            expect(uploadedData).toBe(expectedBase64);
            expect(rpcPromise.reject).not.toHaveBeenCalled();
        });

        it("should link a GET stream and resolve the promise when content length matches", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 2,
                method: "readDataset",
                params: {
                    dsname: "USER.DATASET",
                },
            };

            let downloadedData = "";
            const writeStream = new Writable({
                write(chunk, _encoding, cb) {
                    downloadedData += chunk.toString();
                    cb();
                },
            });
            const timeoutId = { refresh: vi.fn() } as any;
            const callbackInfo: CallbackInfo = { callback: vi.fn() };

            streamManager.registerStream(request, () => writeStream, timeoutId, callbackInfo);

            const rpcPromise: RpcPromise = {
                rpc: { id: 2, method: "readDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: {
                    id: 2,
                    pipePath: "/tmp/pipe_2",
                    contentLen: 12,
                },
            };

            const mockChannel = {
                stdout: new PassThrough(),
            };
            mockSshClient.exec.mockImplementation((cmd, cb) => {
                expect(cmd).toBe(`exec /bin/sh -c "cat /tmp/pipe_2"`);
                cb(null, mockChannel);
            });

            streamManager.linkStreamToPromise(rpcPromise, notif, "GET");

            // Push base64 encoded data to stdout
            const originalText = "Hello, Zowe!";
            const base64Text = Buffer.from(originalText).toString("base64");
            mockChannel.stdout.write(base64Text);
            mockChannel.stdout.end();

            // Simulate server response
            const response = {
                success: true,
                contentLen: originalText.length,
            };
            await rpcPromise.resolve(response);

            expect(downloadedData).toBe(originalText);
            expect(callbackInfo.totalBytes).toBe(12);
            expect(rpcPromise.reject).not.toHaveBeenCalled();
        });

        it("should reject the promise if there is a content length mismatch", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 3,
                method: "readDataset",
                params: {
                    dsname: "USER.DATASET",
                },
            };

            const writeStream = new PassThrough();
            streamManager.registerStream(request, () => writeStream);

            const rpcPromise: RpcPromise = {
                rpc: { id: 3, method: "readDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: {
                    id: 3,
                    pipePath: "/tmp/pipe_3",
                },
            };

            const mockChannel = {
                stdout: new PassThrough(),
            };
            mockSshClient.exec.mockImplementation((_, cb) => cb(null, mockChannel));

            streamManager.linkStreamToPromise(rpcPromise, notif, "GET");

            mockChannel.stdout.write(Buffer.from("Hello").toString("base64"));
            mockChannel.stdout.end();

            // Server response reports contentLen = 100, but we only received 5 bytes
            const response = {
                success: true,
                contentLen: 100,
            };
            await rpcPromise.resolve(response);

            expect(rpcPromise.reject).toHaveBeenCalled();
            const errorArg = rpcPromise.reject.mock.calls[0][0] as Error;
            expect(errorArg.message).toContain("Content length mismatch");
        });

        it("should reject the promise if there is a content length mismatch on PUT", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 33,
                method: "writeDataset",
                params: {
                    dsname: "USER.DATASET",
                },
            };

            const readStream = Readable.from(["Hello"]);
            streamManager.registerStream(request, () => readStream);

            const rpcPromise: RpcPromise = {
                rpc: { id: 33, method: "writeDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: {
                    id: 33,
                    pipePath: "/tmp/pipe_33",
                },
            };

            const mockChannel = {
                stdin: new Writable({
                    write(_chunk, _encoding, cb) {
                        cb();
                    },
                }),
            };
            mockSshClient.exec.mockImplementation((_, cb) => cb(null, mockChannel));

            streamManager.linkStreamToPromise(rpcPromise, notif, "PUT");

            // Server response reports contentLen = 100, but we only sent 5 bytes
            const response = {
                success: true,
                contentLen: 100,
            };
            await rpcPromise.resolve(response);

            expect(rpcPromise.reject).toHaveBeenCalled();
            const errorArg = rpcPromise.reject.mock.calls[0][0] as Error;
            expect(errorArg.message).toContain("Content length mismatch");
            expect(errorArg.message).toContain("client sent 5, server received 100");
        });
    });

    describe("Error cases", () => {
        it("should throw an error in uploadStream if readStream is not Readable", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 4,
                method: "writeDataset",
                params: { dsname: "USER.DATASET" },
            };

            // Register an invalid stream (has .on but not instanceof Readable)
            const mockStream = {
                on: vi.fn(),
            };
            streamManager.registerStream(request, () => mockStream as any);

            const rpcPromise: RpcPromise = {
                rpc: { id: 4, method: "writeDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: { id: 4, pipePath: "/tmp/pipe_4" },
            };

            streamManager.linkStreamToPromise(rpcPromise, notif, "PUT");

            await rpcPromise.resolve({ success: true });

            expect(rpcPromise.reject).toHaveBeenCalled();
            const errorArg = rpcPromise.reject.mock.calls[0][0] as Error;
            expect(errorArg.message).toContain("No stream found for request ID: 4");
        });

        it("should throw an error in downloadStream if writeStream is not Writable", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 5,
                method: "readDataset",
                params: { dsname: "USER.DATASET" },
            };

            // Register an invalid stream (has .on but not instanceof Writable)
            const mockStream = {
                on: vi.fn(),
            };
            streamManager.registerStream(request, () => mockStream as any);

            const rpcPromise: RpcPromise = {
                rpc: { id: 5, method: "readDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: { id: 5, pipePath: "/tmp/pipe_5" },
            };

            streamManager.linkStreamToPromise(rpcPromise, notif, "GET");

            await rpcPromise.resolve({ success: true });

            expect(rpcPromise.reject).toHaveBeenCalled();
            const errorArg = rpcPromise.reject.mock.calls[0][0] as Error;
            expect(errorArg.message).toContain("No stream found for request ID: 5");
        });

        it("should propagate SSH exec errors in uploadStream", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 6,
                method: "writeDataset",
                params: { dsname: "USER.DATASET" },
            };

            const readStream = Readable.from(["data"]);
            streamManager.registerStream(request, () => readStream);

            const rpcPromise: RpcPromise = {
                rpc: { id: 6, method: "writeDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "sendStream",
                params: { id: 6, pipePath: "/tmp/pipe_6" },
            };

            // Mock SSH exec to return an error
            const sshError = new Error("SSH connection lost");
            mockSshClient.exec.mockImplementation((_, cb) => cb(sshError));

            streamManager.linkStreamToPromise(rpcPromise, notif, "PUT");

            await rpcPromise.resolve({ success: true });

            expect(rpcPromise.reject).toHaveBeenCalledWith(sshError);
        });

        it("should propagate SSH exec errors in downloadStream", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 7,
                method: "readDataset",
                params: { dsname: "USER.DATASET" },
            };

            const writeStream = new PassThrough();
            streamManager.registerStream(request, () => writeStream);

            const rpcPromise: RpcPromise = {
                rpc: { id: 7, method: "readDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: { id: 7, pipePath: "/tmp/pipe_7" },
            };

            const sshError = new Error("SSH download connection lost");
            mockSshClient.exec.mockImplementation((_, cb) => cb(sshError));

            streamManager.linkStreamToPromise(rpcPromise, notif, "GET");

            await rpcPromise.resolve({ success: true });

            expect(rpcPromise.reject).toHaveBeenCalledWith(sshError);
        });

        it("should reject with generic mismatch message if resourceName is undefined", async () => {
            const request: RpcRequest = {
                jsonrpc: "2.0",
                id: 8,
                method: "readDataset",
                params: {}, // No dsname or fspath
            };

            const writeStream = new PassThrough();
            streamManager.registerStream(request, () => writeStream);

            const rpcPromise: RpcPromise = {
                rpc: { id: 8, method: "readDataset", params: {} },
                resolve: vi.fn(),
                reject: vi.fn(),
            };

            const notif: RpcNotification = {
                jsonrpc: "2.0",
                method: "receiveStream",
                params: { id: 8, pipePath: "/tmp/pipe_8" },
            };

            const mockChannel = {
                stdout: new PassThrough(),
            };
            mockSshClient.exec.mockImplementation((_, cb) => cb(null, mockChannel));

            streamManager.linkStreamToPromise(rpcPromise, notif, "GET");

            mockChannel.stdout.write(Buffer.from("Hello").toString("base64"));
            mockChannel.stdout.end();

            const response = {
                success: true,
                contentLen: 100,
            };
            await rpcPromise.resolve(response);

            expect(rpcPromise.reject).toHaveBeenCalled();
            const errorArg = rpcPromise.reject.mock.calls[0][0] as Error;
            expect(errorArg.message).toContain("Content length mismatch");
            expect(errorArg.message).not.toContain("for ");
        });
    });
});
