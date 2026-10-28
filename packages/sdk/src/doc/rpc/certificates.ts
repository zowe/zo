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

import type { B64String } from "../types";
import type * as common from "./common";

/**
 * The exact SAF/ESM codes returned by the underlying R_datalib call, preserved
 * alongside the natural-language message so the precise codes remain available.
 * See z/OS Security Server RACF Callable Services (SA23-2293), R_datalib.
 */
export interface SafReturns {
    /**
     * R_datalib function code (e.g. 0x08 DataPut, 0x09 DataRemove)
     */
    functionCode: number;
    /**
     * SAF return code
     */
    safReturnCode: number;
    /**
     * ESM (e.g. RACF) return code
     */
    esmReturnCode: number;
    /**
     * ESM (e.g. RACF) reason code
     */
    esmReasonCode: number;
}

/**
 * A command response that may carry a non-fatal SAF warning (ESM return code 4),
 * e.g. "the certificate already exists in the ESM database; the supplied label was ignored".
 */
export interface CertCommandResponse extends common.CommandResponse {
    /**
     * Human-readable note for a non-fatal SAF warning, when one occurred
     */
    warning?: string;
    /**
     * The exact SAF/ESM codes behind a warning or error, when a non-zero code was returned
     */
    safReturns?: SafReturns;
    /**
     * System SSL / GSKCMS status code, when a GSK call failed
     */
    gskReturnCode?: number;
}

export interface CreateKeyringRequest extends common.CommandRequest<"createKeyring"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
}

export type CreateKeyringResponse = CertCommandResponse;

export interface DeleteKeyringRequest extends common.CommandRequest<"deleteKeyring"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
}

export type DeleteKeyringResponse = CertCommandResponse;

export interface RefreshDigtcertRequest extends common.CommandRequest<"refreshDigtcert"> {}

export type RefreshDigtcertResponse = CertCommandResponse;

export interface DeleteCertificateRequest extends common.CommandRequest<"deleteCertificate"> {
    /**
     * Certificate owner (user ID)
     */
    owner: string;
    /**
     * Key ring to disconnect the certificate from. Omit and set `database: true`
     * to delete the certificate from the ESM database instead.
     */
    keyring?: string;
    /**
     * Delete the certificate from the ESM database (removes it entirely, not
     * just from one ring). Mutually exclusive with `keyring`.
     */
    database?: boolean;
    /**
     * Certificate label
     */
    label: string;
    /**
     * Do not automatically REFRESH the DIGTCERT class if the ESM reports it is
     * required for the change to take effect (default is to refresh)
     */
    skipRefresh?: boolean;
}

export type DeleteCertificateResponse = CertCommandResponse;

export interface ListCertificatesRequest extends common.CommandRequest<"listCertificates"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Filter by certificate label
     */
    label?: string;
    /**
     * Filter by usage (PERSONAL, CERTAUTH, OTHER)
     */
    usage?: string;
    /**
     * Return only certificate labels
     */
    labelOnly?: boolean;
    /**
     * Return only certificate owners
     */
    ownerOnly?: boolean;
    /**
     * Maximum number of certificates to return (0 = all)
     */
    maxEntries?: number;
}

export interface ListCertificatesResponse extends common.CommandResponse {
    /**
     * List of certificates connected to the key ring
     */
    items: common.CertificateInfo[];
    /**
     * Number of rows returned
     */
    returnedRows: number;
    /**
     * True when the ring holds more certificates than were returned because the
     * max-entries limit was reached (request with maxEntries: 0 to retrieve all)
     */
    moreAvailable: boolean;
}

export interface ExportCertificateRequest extends common.CommandRequest<"exportCertificate"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Certificate label
     */
    label: string;
    /**
     * Export format: "pem" (certificate) or "p12" (certificate + private key)
     */
    format?: string;
    /**
     * Output file path on z/OS (required for p12). Mutually exclusive with `dsn`.
     */
    file?: string;
    /**
     * Output data set on z/OS (sequential or PDS/E member), created if it does
     * not exist. Mutually exclusive with `file`.
     */
    dsn?: string;
    /**
     * PKCS#12 passphrase (required when format is "p12")
     */
    password?: string;
}

export interface ExportCertificateResponse extends common.CommandResponse {
    /**
     * Certificate label
     */
    label: string;
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Export format that was produced ("pem" or "p12")
     */
    format: string;
    /**
     * Output file path, when written to disk
     */
    file?: string;
    /**
     * Output data set name, when written to a data set
     */
    dsn?: string;
    /**
     * Number of bytes written to the output file or data set
     */
    bytesWritten?: number;
    /**
     * Exported certificate bytes, base64-encoded (PEM text or PKCS#12 binary)
     */
    data: B64String;
}

export interface ImportCertificateRequest extends common.CommandRequest<"importCertificate"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Certificate label to assign
     */
    label: string;
    /**
     * Certificate usage: PERSONAL or CERTAUTH
     */
    usage: string;
    /**
     * Path to the source PKCS#12 file on z/OS. Mutually exclusive with `dsn`.
     */
    file?: string;
    /**
     * Source PKCS#12 data set on z/OS (sequential or PDS/E member). Mutually
     * exclusive with `file`.
     */
    dsn?: string;
    /**
     * PKCS#12 passphrase
     */
    password: string;
    /**
     * Do not automatically REFRESH the DIGTCERT class if the ESM reports it is
     * required for the change to take effect (default is to refresh)
     */
    skipRefresh?: boolean;
}

export interface ImportCertificateResponse extends CertCommandResponse {
    /**
     * Certificate label
     */
    label: string;
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
}

export interface ShowCertificateRequest extends common.CommandRequest<"showCertificate"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Certificate label
     */
    label: string;
}

export interface ShowCertificateResponse extends common.CommandResponse {
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
     * Whether this certificate is the ring's default
     */
    default: boolean;
    /**
     * Private-key type code (from R_datalib)
     */
    keyType: number;
    /**
     * Private-key size in bits (0 if no private key)
     */
    keySize: number;
    /**
     * Certificate serial number (hex), from decoding the DER certificate
     */
    serialNumber?: string;
    /**
     * Validity start (ISO-8601), from decoding the DER certificate
     */
    notBefore?: string;
    /**
     * Validity end (ISO-8601), from decoding the DER certificate
     */
    notAfter?: string;
    /**
     * ESM record ID (serial + issuer identifier)
     */
    recordId?: string;
}

export interface RingCertificate {
    /**
     * Certificate owner (user ID or CERTAUTH pseudo-id such as irrcerta)
     */
    owner: string;
    /**
     * Certificate label
     */
    label: string;
}

export interface RingInfo {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    name: string;
    /**
     * Certificates connected to this ring
     */
    certificates: RingCertificate[];
}

export interface ListRingsRequest extends common.CommandRequest<"listRings"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Optional key ring name to narrow the result
     */
    keyring?: string;
}

export interface ListRingsResponse extends CertCommandResponse {
    /**
     * The user's key rings and the certificates connected to each
     */
    items: RingInfo[];
    /**
     * Number of rings returned
     */
    returnedRows: number;
}

export interface ConnectCertificateRequest extends common.CommandRequest<"connectCertificate"> {
    /**
     * Certificate owner (user ID)
     */
    owner: string;
    /**
     * Target key ring name
     */
    keyring: string;
    /**
     * Source key ring the certificate is already on (its bytes are read from here).
     * Mutually exclusive with `fromDatabase`.
     */
    fromRing?: string;
    /**
     * Read the certificate from the ESM database instead of a specific ring (use
     * when the certificate is not connected to any ring). Mutually exclusive with `fromRing`.
     */
    fromDatabase?: boolean;
    /**
     * Certificate label
     */
    label: string;
    /**
     * Certificate usage: PERSONAL or CERTAUTH (default: the certificate's current usage)
     */
    usage?: string;
    /**
     * Set this certificate as the target ring's default
     */
    default?: boolean;
}

export type ConnectCertificateResponse = CertCommandResponse;

export interface SetDefaultCertificateRequest extends common.CommandRequest<"setDefaultCertificate"> {
    /**
     * Certificate owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
    /**
     * Certificate label
     */
    label: string;
}

export type SetDefaultCertificateResponse = CertCommandResponse;

export interface TrustCertificateRequest extends common.CommandRequest<"trustCertificate"> {
    /**
     * Certificate owner (user ID)
     */
    owner: string;
    /**
     * Certificate label
     */
    label: string;
    /**
     * New trust status: TRUST, HIGHTRUST, or NOTRUST
     */
    status: string;
}

export type TrustCertificateResponse = CertCommandResponse;

export interface RenameCertificateRequest extends common.CommandRequest<"renameCertificate"> {
    /**
     * Certificate owner (user ID)
     */
    owner: string;
    /**
     * Current certificate label
     */
    label: string;
    /**
     * New certificate label
     */
    newLabel: string;
}

export type RenameCertificateResponse = CertCommandResponse;

export interface CountRingRequest extends common.CommandRequest<"countRing"> {
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name, or "*" for the owner's virtual key ring
     */
    keyring: string;
}

export interface CountRingResponse extends common.CommandResponse {
    /**
     * Number of certificates connected to the key ring
     */
    count: number;
    /**
     * Key ring owner (user ID)
     */
    owner: string;
    /**
     * Key ring name
     */
    keyring: string;
}
