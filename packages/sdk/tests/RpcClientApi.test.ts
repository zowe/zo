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

import { beforeEach, describe, expect, it, vi } from "vitest";
import { RpcClientApi } from "../src/RpcClientApi";

class TestRpcClient extends RpcClientApi {
    public request = vi.fn().mockResolvedValue({ success: true });
}

describe("RpcClientApi", () => {
    let client: TestRpcClient;

    beforeEach(() => {
        client = new TestRpcClient();
    });

    it("should route console commands correctly", async () => {
        await client.console.issueCmd({ cmd: "D A,L" });
        expect(client.request).toHaveBeenCalledWith({
            command: "consoleCommand",
            cmd: "D A,L",
        });
    });

    it("should route core commands correctly", async () => {
        await client.core.getInfo();
        expect(client.request).toHaveBeenCalledWith({
            command: "getInfo",
        });
    });

    it("should route dataset commands correctly", async () => {
        await client.ds.createDataset({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "createDataset",
            dsname: "USER.DATASET",
        });

        await client.ds.createMember({ dsname: "USER.DATASET", member: "MEM" });
        expect(client.request).toHaveBeenCalledWith({
            command: "createMember",
            dsname: "USER.DATASET",
            member: "MEM",
        });

        await client.ds.deleteDataset({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "deleteDataset",
            dsname: "USER.DATASET",
        });

        await client.ds.listDatasets({ dsname: "USER.*" });
        expect(client.request).toHaveBeenCalledWith({
            command: "listDatasets",
            dsname: "USER.*",
        });

        await client.ds.listDsMembers({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "listDsMembers",
            dsname: "USER.DATASET",
        });

        await client.ds.readDataset({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "readDataset",
            dsname: "USER.DATASET",
        });

        await client.ds.restoreDataset({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "restoreDataset",
            dsname: "USER.DATASET",
        });

        await client.ds.writeDataset({ dsname: "USER.DATASET" });
        expect(client.request).toHaveBeenCalledWith({
            command: "writeDataset",
            dsname: "USER.DATASET",
        });

        await client.ds.renameDataset({ oldName: "OLD.NAME", newName: "NEW.NAME" });
        expect(client.request).toHaveBeenCalledWith({
            command: "renameDataset",
            oldName: "OLD.NAME",
            newName: "NEW.NAME",
        });

        await client.ds.renameMember({ dsname: "USER.DATASET", oldName: "OLD", newName: "NEW" });
        expect(client.request).toHaveBeenCalledWith({
            command: "renameMember",
            dsname: "USER.DATASET",
            oldName: "OLD",
            newName: "NEW",
        });

        await client.ds.copyDatasetOrMember({ fromName: "FROM.NAME", toName: "TO.NAME" });
        expect(client.request).toHaveBeenCalledWith({
            command: "copyDatasetOrMember",
            fromName: "FROM.NAME",
            toName: "TO.NAME",
        });

        await client.ds.resolveDsAlias({ dsname: "MY.ALIAS" });
        expect(client.request).toHaveBeenCalledWith({
            command: "resolveDsAlias",
            dsname: "MY.ALIAS",
        });
    });

    it("should route jobs commands correctly", async () => {
        await client.jobs.cancelJob({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "cancelJob",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.deleteJob({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "deleteJob",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.getJcl({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "getJcl",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.getStatus({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "getJobStatus",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.holdJob({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "holdJob",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.listJobs();
        expect(client.request).toHaveBeenCalledWith({
            command: "listJobs",
        });

        await client.jobs.listSpools({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "listSpools",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.readSpool({ jobname: "JOB", jobid: "JOB00001", spoolid: 1 });
        expect(client.request).toHaveBeenCalledWith({
            command: "readSpool",
            jobname: "JOB",
            jobid: "JOB00001",
            spoolid: 1,
        });

        await client.jobs.releaseJob({ jobname: "JOB", jobid: "JOB00001" });
        expect(client.request).toHaveBeenCalledWith({
            command: "releaseJob",
            jobname: "JOB",
            jobid: "JOB00001",
        });

        await client.jobs.submitJcl({ jcl: "//JOB..." });
        expect(client.request).toHaveBeenCalledWith({
            command: "submitJcl",
            jcl: "//JOB...",
        });

        await client.jobs.submitJob({ dsname: "USER.JCL(JOB)" });
        expect(client.request).toHaveBeenCalledWith({
            command: "submitJob",
            dsname: "USER.JCL(JOB)",
        });

        await client.jobs.submitUss({ fspath: "/tmp/job.jcl" });
        expect(client.request).toHaveBeenCalledWith({
            command: "submitUss",
            fspath: "/tmp/job.jcl",
        });
    });

    it("should route system commands correctly", async () => {
        await client.system.viewSyslog();
        expect(client.request).toHaveBeenCalledWith({
            command: "viewSyslog",
        });
    });

    it("should route certificate commands correctly", async () => {
        await client.certificates.exportCertificate({
            owner: "USER01",
            keyring: "RING02",
            label: "CERT03",
            format: "p12",
            dsn: "USER01.CERT03.P12",
            password: "secret",
        });
        expect(client.request).toHaveBeenCalledWith({
            command: "exportCertificate",
            owner: "USER01",
            keyring: "RING02",
            label: "CERT03",
            format: "p12",
            dsn: "USER01.CERT03.P12",
            password: "secret",
        });

        await client.certificates.importCertificate({
            owner: "USER01",
            keyring: "RING02",
            label: "CERT03",
            usage: "PERSONAL",
            dsn: "USER01.CERT03.P12",
            password: "secret",
        });
        expect(client.request).toHaveBeenCalledWith({
            command: "importCertificate",
            owner: "USER01",
            keyring: "RING02",
            label: "CERT03",
            usage: "PERSONAL",
            dsn: "USER01.CERT03.P12",
            password: "secret",
        });
    });

    it("should route tool commands correctly", async () => {
        await client.tool.search({ dsname: "USER.DATASET", pattern: "FIND" });
        expect(client.request).toHaveBeenCalledWith({
            command: "toolSearch",
            dsname: "USER.DATASET",
            pattern: "FIND",
        });
    });

    it("should route tso commands correctly", async () => {
        await client.tso.issueCmd({ cmd: "ALIST" });
        expect(client.request).toHaveBeenCalledWith({
            command: "tsoCommand",
            cmd: "ALIST",
        });
    });

    it("should route uss commands correctly", async () => {
        await client.uss.copyUss({ fromPath: "/tmp/a", toPath: "/tmp/b" });
        expect(client.request).toHaveBeenCalledWith({
            command: "copyUss",
            fromPath: "/tmp/a",
            toPath: "/tmp/b",
        });

        await client.uss.chmodFile({ fspath: "/tmp/a", mode: "755" });
        expect(client.request).toHaveBeenCalledWith({
            command: "chmodFile",
            fspath: "/tmp/a",
            mode: "755",
        });

        await client.uss.chownFile({ fspath: "/tmp/a", owner: "user" });
        expect(client.request).toHaveBeenCalledWith({
            command: "chownFile",
            fspath: "/tmp/a",
            owner: "user",
        });

        await client.uss.chtagFile({ fspath: "/tmp/a", tag: "txt" });
        expect(client.request).toHaveBeenCalledWith({
            command: "chtagFile",
            fspath: "/tmp/a",
            tag: "txt",
        });

        await client.uss.createFile({ fspath: "/tmp/a" });
        expect(client.request).toHaveBeenCalledWith({
            command: "createFile",
            fspath: "/tmp/a",
        });

        await client.uss.deleteFile({ fspath: "/tmp/a" });
        expect(client.request).toHaveBeenCalledWith({
            command: "deleteFile",
            fspath: "/tmp/a",
        });

        await client.uss.listFiles({ fspath: "/tmp" });
        expect(client.request).toHaveBeenCalledWith({
            command: "listFiles",
            fspath: "/tmp",
        });

        await client.uss.issueCmd({ cmd: "ls -la" });
        expect(client.request).toHaveBeenCalledWith({
            command: "unixCommand",
            cmd: "ls -la",
        });

        await client.uss.moveFile({ fromPath: "/tmp/a", toPath: "/tmp/b" });
        expect(client.request).toHaveBeenCalledWith({
            command: "moveFile",
            fromPath: "/tmp/a",
            toPath: "/tmp/b",
        });
    });

    it("should route uss commands with progress correctly", async () => {
        const progressCallback = vi.fn();

        // With progress callback
        await client.uss.readFile({ fspath: "/tmp/a" }, progressCallback);
        expect(client.request).toHaveBeenCalledWith(
            {
                command: "readFile",
                fspath: "/tmp/a",
            },
            progressCallback,
        );

        // Without progress callback (covers line 91 branch)
        await client.uss.readFile({ fspath: "/tmp/a" });
        expect(client.request).toHaveBeenCalledWith(
            {
                command: "readFile",
                fspath: "/tmp/a",
            },
            undefined,
        );

        // writeFile with progress callback
        await client.uss.writeFile({ fspath: "/tmp/a" }, progressCallback);
        expect(client.request).toHaveBeenCalledWith(
            {
                command: "writeFile",
                fspath: "/tmp/a",
            },
            progressCallback,
        );

        // writeFile without progress callback
        await client.uss.writeFile({ fspath: "/tmp/a" });
        expect(client.request).toHaveBeenCalledWith(
            {
                command: "writeFile",
                fspath: "/tmp/a",
            },
            undefined,
        );
    });
});
