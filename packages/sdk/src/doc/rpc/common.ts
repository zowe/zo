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

// Base Request/Response interfaces

/**
 * JSON-RPC 2.0 Standard Error Codes
 */
export const RpcErrorCode = {
    PARSE_ERROR: -32700, // Invalid JSON was received
    INVALID_REQUEST: -32600, // The JSON sent is not a valid Request object
    METHOD_NOT_FOUND: -32601, // The method does not exist / is not available
    INVALID_PARAMS: -32602, // Invalid method parameter(s)
    INTERNAL_ERROR: -32603, // Internal JSON-RPC error
    // -32000 to -32099 are reserved for implementation-defined server-errors
    REQUEST_TIMEOUT: -32001, // Request exceeded timeout limit
} as const;

export type RpcErrorCodeType = (typeof RpcErrorCode)[keyof typeof RpcErrorCode];

export interface RpcNotification {
    jsonrpc: "2.0";
    method: string;
    // biome-ignore lint/suspicious/noExplicitAny: RPC params can be any object
    params?: any;
}

export interface RpcRequest extends RpcNotification {
    id: number;
}

export interface ErrorDetails {
    code: number;
    message: string;
    // biome-ignore lint/suspicious/noExplicitAny: RPC data can be any object
    data?: any;
}

export interface RpcResponse {
    jsonrpc: "2.0";
    // biome-ignore lint/suspicious/noExplicitAny: RPC result can be any object
    result?: any;
    error?: ErrorDetails;
    id: number;
}

export interface ReadableStreamRpc {
    stream?: () => Readable;
}
export interface WritableStreamRpc {
    stream?: () => Writable;
}

export interface CommandRequest<CommandT extends string = string> {
    /**
     * Requested command to execute
     */
    command: CommandT;
}

export interface CommandResponse {
    /**
     * True if command succeeded
     */
    success: boolean;
}

/**
 * StatusMessage represents the status of the server and serves as a health check
 */
export interface StatusMessage {
    status: string;
    message: string;
    // biome-ignore lint/suspicious/noExplicitAny: Data properties are unknown type
    data?: { [key: string]: any };
}

// Options interfaces

export interface IoserverOptions {
    numWorkers?: number;
    verbose?: boolean;
}

export interface ListOptions {
    /**
     * Maximum number of items to return
     */
    maxItems?: number;
    /**
     * Response timeout in seconds
     */
    responseTimeout?: number;
}

export interface ListDatasetOptions {
    /**
     * Skip data sets that come before this data set name
     */
    start?: string;
}

// Certificate / key ring types

export interface CertificateInfo {
    /**
     * Certificate label
     */
    label: string;
    /**
     * Owning user ID
     */
    owner: string;
    /**
     * Usage: PERSONAL, CERTAUTH, or OTHER
     */
    usage: string;
    /**
     * Trust status: TRUST, HIGHTRUST, NOTRUST, or UNKNOWN
     */
    status: string;
    /**
     * Whether this certificate is the default in the ring
     */
    default: boolean;
}

// Dataset-related types

export interface Dataset {
    /**
     * Data set name
     */
    name: string;
    /**
     * Allocated units
     */
    alloc?: number;
    /**
     * Allocated extents
     */
    allocx?: number;
    /**
     * Block size
     */
    blksize?: number;
    /**
     * Creation date
     */
    cdate?: string;
    /**
     * Data class
     */
    dataclass?: string;
    /**
     * Device type
     */
    devtype?: string;
    /**
     * Dsname type (e.g., PDS, LIBRARY)
     */
    dsntype?: string;
    /**
     * Data set organization
     */
    dsorg?: string;
    /**
     * Expiration date
     */
    edate?: string;
    /**
     * Whether the data set is encrypted
     */
    encrypted?: boolean;
    /**
     * Logical record length
     */
    lrecl?: number;
    /**
     * Management class
     */
    mgmtclass?: string;
    /**
     * Whether the data set is migrated
     */
    migrated?: boolean;
    /**
     * Whether the data set is on multiple volumes
     */
    multivolume?: boolean;
    /**
     * Primary units
     */
    primary?: number;
    /**
     * Date last referenced
     */
    rdate?: string;
    /**
     * Record format
     */
    recfm?: string;
    /**
     * Secondary units
     */
    secondary?: number;
    /**
     * Space units
     */
    spacu?: string;
    /**
     * Storage class
     */
    storclass?: string;
    /**
     * Used percentage
     */
    usedp?: number;
    /**
     * Used extents
     */
    usedx?: number;
    /**
     * Volume serial
     */
    volser?: string;
    /**
     * List of volume serials
     */
    volsers?: string[];
}

export interface DatasetAttributes {
    alcunit?: string; // Allocation Unit
    blksize?: number; // Block Size
    dirblk?: number; // Directory Blocks
    dsorg?: string; // Data Set Organization
    primary: number; // Primary Space
    recfm?: string; // Record Format
    lrecl: number; // Record Length
    dataclass?: string; // Data Class
    unit?: string; // Device Type
    dsntype?: string; // Data Set Type
    mgntclass?: string; // Management Class
    dsname?: string; // Data Set Name
    avgblk?: number; // Average Block Length
    secondary?: number; // Secondary Space
    size?: string; // Size
    storclass?: string; // Storage Class
    vol?: string; // Volume Serial
}

export interface DsMember {
    name: string;
    user?: string;
    vers?: number;
    mod?: number;
    c4date?: string;
    m4date?: string;
    mtime?: string;
    cnorc?: number;
    inorc?: number;
    mnorc?: number;
    sclm?: string;
}

// USS-related types

export interface UssItem {
    /**
     * File name
     */
    name: string;
    /**
     * Number of links to the item
     */
    links?: number;
    /**
     * User (owner) of the item
     */
    user?: string;
    /**
     * Group of the item
     */
    group?: string;
    /**
     * Size of the item
     */
    size?: number;
    /**
     * The filetag of the item
     */
    filetag?: string;
    /**
     * Modification date of the item
     */
    mtime?: string;
    /**
     * The permission string of the item
     */
    mode?: string;
}

// Job-related types

export interface Job {
    /**
     * Job ID
     */
    id: string;
    /**
     * Job name
     */
    name: string;
    /**
     * JES subsystem (undefined if job was processed by the primary subsystem)
     */
    subsystem?: string;
    /**
     * Job owner
     */
    owner: string;
    /**
     * Job status - INPUT, ACTIVE, OUTPUT
     */
    status: string;
    /**
     * Job type - JOB, STC, TSU
     */
    type: string;
    /**
     * Job execution class
     */
    class: string;
    /**
     * Job return code (undefined if job is not complete)
     */
    retcode?: string;
    /**
     * Job correlator (undefined for JES3 jobs)
     */
    correlator?: string;
    /**
     * Job phase
     */
    phase: number;
    /**
     * Job phase name
     */
    phaseName: string;
    /**
     * z/OS system on which the job ran (undefined if execution data is unavailable)
     */
    execSystem?: string;
    /**
     * JES2 member on which the job ran (undefined if execution data is unavailable)
     */
    execMember?: string;
    /**
     * Submission time (JES input end time) in UTC, formatted as YYYY-MM-DDTHH:mm:ss.SSSZ
     */
    execSubmitted?: string;
    /**
     * Execution start time in UTC, formatted as YYYY-MM-DDTHH:mm:ss.SSSZ (undefined before execution)
     */
    execStarted?: string;
    /**
     * Execution end time in UTC, formatted as YYYY-MM-DDTHH:mm:ss.SSSZ (undefined before completion)
     */
    execEnded?: string;
}

export interface Spool {
    /**
     * Spool ID
     */
    id: number;
    /**
     * DD name
     */
    ddname: string;
    /**
     * Step name in the job
     */
    stepname: string;
    /**
     * Dataset name
     */
    dsname: string;
    /**
     * Procedure name for the step
     */
    procstep: string;
}
