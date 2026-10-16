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

import type { Readable, Writable } from "node:stream";
import type { B64String } from "../types";
import type * as common from "./common";

export interface CreateDatasetRequest extends common.CommandRequest<"createDataset"> {
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Dataset attributes
     */
    attributes: common.DatasetAttributes;
}

export type CreateDatasetResponse = common.CommandResponse;

export interface CreateMemberRequest extends common.CommandRequest<"createMember"> {
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Whether to overwrite existing member
     */
    overwrite?: boolean;
}

export type CreateMemberResponse = common.CommandResponse;

export interface DeleteDatasetRequest extends common.CommandRequest<"deleteDataset"> {
    /**
     * Dataset name
     */
    dsname: string;
}

export type DeleteDatasetResponse = common.CommandResponse;

export interface DeleteVsamRequest extends common.CommandRequest<"deleteVsam"> {
    /**
     * VSAM cluster name
     */
    dsname: string;
    /**
     * Overwrite the data component with binary zeros before deletion. Defaults to false (NOERASE).
     */
    erase?: boolean;
    /**
     * Delete the cluster even if its retention period has not expired. Defaults to false (NOPURGE).
     */
    purge?: boolean;
}

export type DeleteVsamResponse = common.CommandResponse;

export interface RenameDatasetRequest extends common.CommandRequest<"renameDataset"> {
    /**
     * Dataset name before
     */
    dsnameBefore: string;

    /**
     * Dataset name after
     */
    dsnameAfter: string;
}

export type RenameDatasetResponse = common.CommandResponse;

export interface RenameMemberRequest extends common.CommandRequest<"renameMember"> {
    /**
     * Dataset name
     */
    dsname: string;

    /**
     * Member name before
     */
    memberBefore: string;

    /**
     * Member name after
     */
    memberAfter: string;
}

export type RenameMemberResponse = common.CommandResponse;

export interface ListDatasetsRequest
    extends common.CommandRequest<"listDatasets">,
        common.ListOptions,
        common.ListDatasetOptions {
    /**
     * Pattern to match against dataset names
     */
    pattern: string;
    /**
     * Whether to include attributes in the response
     */
    attributes?: boolean;
    /**
     * Whether to match the pattern exactly instead of appending a trailing wildcard
     */
    exactMatch?: boolean;
}

export interface ListDatasetsResponse extends common.CommandResponse {
    /**
     * List of returned datasets
     */
    items: common.Dataset[];
    /**
     * Number of rows returned
     */
    returnedRows: number;
}

export interface ListDsMembersRequest
    extends common.CommandRequest<"listDsMembers">,
        common.ListOptions,
        common.ListDatasetOptions {
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Whether to include attributes in the response
     */
    attributes?: boolean;
    /**
     * A wildcard pattern used to filter the returned entries (supports '*' and '?').
     */
    pattern?: string;
}

export interface ListDsMembersResponse extends common.CommandResponse {
    /**
     * List of returned dataset members
     */
    items: common.DsMember[];
    /**
     * Number of rows returned
     */
    returnedRows: number;
}

export interface ResolveDsAliasRequest extends common.CommandRequest<"resolveDsAlias"> {
    /**
     * Dataset name
     */
    dsname: string;
}
export interface ResolveDsAliasResponse extends common.CommandResponse {
    /**
     *  The original data set name pointed to by the alias.
     */
    targetDsn: string;
}

export interface ReadDatasetRequest extends common.CommandRequest<"readDataset">, common.WritableStreamRpc {
    /**
     * Desired encoding for the dataset (optional)
     */
    encoding?: string;
    /**
     * Source encoding of the dataset content (optional, defaults to UTF-8)
     */
    localEncoding?: string;
    /**
     * Volume serial for the data set (optional)
     */
    volume?: string;
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Stream to write contents to
     */
    stream?: () => Writable;
}

export interface ReadDatasetResponse extends common.CommandResponse {
    /**
     * Desired encoding for the dataset (optional)
     */
    encoding?: string;
    /**
     * Returned e-tag for the data set
     */
    etag: string;
    /**
     * Dataset contents (omitted if streaming)
     */
    data: B64String;
    /**
     * Length of dataset contents in bytes (only used for streaming)
     */
    contentLen?: number;
}

export interface RestoreDatasetRequest extends common.CommandRequest<"restoreDataset"> {
    /**
     * Dataset name
     */
    dsname: string;
}

export type RestoreDatasetResponse = common.CommandResponse;

export interface WriteDatasetRequest extends common.CommandRequest<"writeDataset">, common.ReadableStreamRpc {
    /**
     * Desired encoding for the dataset (optional)
     */
    encoding?: string;
    /**
     * Source encoding of the dataset content (optional, defaults to UTF-8)
     */
    localEncoding?: string;
    /**
     * Last e-tag for the data set (optional, omit to overwrite)
     */
    etag?: string;
    /**
     * Volume serial for the data set (optional)
     */
    volume?: string;
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Dataset contents
     */
    data?: B64String;
    /**
     * Stream to read contents from
     */
    stream?: () => Readable;
}

export interface WriteDatasetResponse extends common.CommandResponse {
    /**
     * Returned e-tag for the data set
     */
    etag: string;
    /**
     * Length of dataset contents in bytes (only used for streaming)
     */
    contentLen?: number;
    /**
     * Warning message if lines were truncated to fit LRECL
     */
    truncationWarning?: string;
}

export interface CopyDatasetRequest extends common.CommandRequest {
    /**
     * Source dataset name to copy from
     */
    source: string;

    /**
     * Target dataset name to copy to
     */
    target: string;

    /**
     * Whether to replace the target dataset if it already exists
     */
    replace?: boolean;

    /**
     * Whether to overwrite the target dataset if it already exists
     */
    overwrite?: boolean;
}

export interface CopyDatasetResponse extends common.CommandResponse {
    /**
     * Whether the target dataset was created
     */
    targetCreated?: boolean;

    /**
     * Whether the target member was created
     */
    memberCreated?: boolean;

    /**
     * Whether the target dataset was overwritten
     */
    overwrite?: boolean;

    /**
     * Whether the target dataset was replaced
     */
    replace?: boolean;
}
