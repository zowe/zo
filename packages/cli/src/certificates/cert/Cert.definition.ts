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

import type { ICommandDefinition, ICommandPositionalDefinition } from "@zowe/imperative";
import { RefreshDefinition } from "../refresh/Refresh.definition";

const OWNER_POSITIONAL: ICommandPositionalDefinition = {
    name: "owner",
    description: "The certificate/key ring owner (user ID).",
    type: "string",
    required: true,
};

const KEYRING_POSITIONAL: ICommandPositionalDefinition = {
    name: "keyring",
    description: "The key ring name.",
    type: "string",
    required: true,
};

const ExportCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/export/Export.handler`,
    type: "command",
    name: "export",
    summary: "Export a certificate from a key ring",
    description:
        "Export a certificate from a key ring in PEM (certificate only) or PKCS#12 (certificate plus " +
        "private key) format, to a file, a data set, or (PEM only) stdout. With --file the certificate is " +
        "written to a USS file on the server; with --dsn it is written to a sequential data set or PDS/E " +
        "member (created if it does not exist); PEM with neither is printed to stdout. The private key is " +
        "only available in the p12 format.",
    examples: [
        {
            description: "Export a certificate as PEM to a file on the server",
            options: "USER01 RING02 -l CERT03 -f /tmp/CERT03.pem",
        },
        {
            description: "Export a certificate and key as PKCS#12",
            options: "USER01 RING02 -l CERT03 -F p12 -f /tmp/CERT03.p12 -p secret",
        },
        {
            description: "Export a certificate and key as PKCS#12 to a data set",
            options: "USER01 RING02 -l CERT03 -F p12 -p secret --dsn USER01.CERT03.P12",
        },
    ],
    positionals: [OWNER_POSITIONAL, KEYRING_POSITIONAL],
    options: [
        { name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true },
        {
            name: "format",
            aliases: ["F"],
            description: "Export format: 'pem' (certificate) or 'p12' (certificate + private key).",
            type: "string",
            defaultValue: "pem",
            allowableValues: { values: ["pem", "p12"] },
        },
        {
            name: "file",
            aliases: ["f"],
            description:
                "Output file path on the z/OS server. Required for p12 unless --dsn is used; PEM prints to " +
                "stdout if omitted. Mutually exclusive with --dsn.",
            type: "string",
            conflictsWith: ["dsn"],
        },
        {
            name: "dsn",
            description:
                "Output data set on the z/OS server (sequential or PDS/E member), created if it does not " +
                "exist. Mutually exclusive with --file. Note that a data set is protected by its RACF " +
                "DATASET profile, not by file permissions.",
            type: "string",
            conflictsWith: ["file"],
        },
        {
            name: "password",
            aliases: ["p"],
            description: "PKCS#12 passphrase (used with --format p12).",
            type: "string",
        },
    ],
    profile: { optional: ["ssh"] },
};

const ImportCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/import/Import.handler`,
    type: "command",
    name: "import",
    summary: "Import a certificate into a key ring from a PKCS#12 file or data set",
    description:
        "Import a certificate (and its private key, when present) into a key ring from a PKCS#12 file or " +
        "data set that already resides on the z/OS server. If the certificate content already exists in " +
        "the ESM database, the existing record is connected to the ring and keeps its original label.",
    examples: [
        {
            description: "Import a personal certificate",
            options: "USER01 RING02 -l CERT03 -u PERSONAL -f /tmp/file.p12 -p secret",
        },
        {
            description: "Import a personal certificate from a data set",
            options: "USER01 RING02 -l CERT03 -u PERSONAL -p secret --dsn USER01.CERT03.P12",
        },
    ],
    positionals: [OWNER_POSITIONAL, KEYRING_POSITIONAL],
    options: [
        {
            name: "label",
            aliases: ["l"],
            description: "The certificate label to assign (used only when the certificate is new to the ESM database).",
            type: "string",
            required: true,
        },
        {
            name: "usage",
            aliases: ["u"],
            description: "The certificate usage.",
            type: "string",
            required: true,
            allowableValues: { values: ["PERSONAL", "CERTAUTH"] },
        },
        {
            name: "file",
            aliases: ["f"],
            description: "Path to the source PKCS#12 file on the z/OS server. Mutually exclusive with --dsn.",
            type: "string",
            conflictsWith: ["dsn"],
        },
        {
            name: "dsn",
            description:
                "Source PKCS#12 data set on the z/OS server (sequential or PDS/E member). Mutually " +
                "exclusive with --file.",
            type: "string",
            conflictsWith: ["file"],
        },
        { name: "password", aliases: ["p"], description: "PKCS#12 passphrase.", type: "string", required: true },
        {
            name: "skip-refresh",
            description: "Do not automatically REFRESH the DIGTCERT class if the ESM reports it is required.",
            type: "boolean",
        },
    ],
    profile: { optional: ["ssh"] },
};

const DeleteCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/delete/Delete.handler`,
    type: "command",
    name: "delete",
    aliases: ["del"],
    summary: "Disconnect a certificate from a key ring, or delete it from the ESM database",
    description:
        "Disconnect a certificate from a key ring, or delete it from the ESM database with --database " +
        "(which removes it entirely, not just from one ring). When the ESM reports that the DIGTCERT class " +
        "must be refreshed for the change to take effect, the refresh is issued automatically unless " +
        "--skip-refresh is specified.",
    examples: [
        { description: "Disconnect a certificate from a ring", options: "USER01 RING02 -l CERT03" },
        { description: "Delete a certificate from the ESM database", options: "USER01 -l CERT03 --database" },
    ],
    positionals: [
        OWNER_POSITIONAL,
        {
            name: "keyring",
            description:
                "The key ring to disconnect the certificate from. Omit and use --database to delete it from the ESM database.",
            type: "string",
        },
    ],
    options: [
        { name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true },
        {
            name: "database",
            aliases: ["db"],
            description: "Delete the certificate from the ESM database (removes it entirely, not just from one ring).",
            type: "boolean",
        },
        {
            name: "skip-refresh",
            description: "Do not automatically REFRESH the DIGTCERT class if the ESM reports it is required.",
            type: "boolean",
        },
    ],
    profile: { optional: ["ssh"] },
};

const ShowCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/show/Show.handler`,
    type: "command",
    name: "show",
    summary: "Show detailed information for a certificate in a key ring",
    description:
        "Show a certificate's owner, usage, trust status, default flag, key size, serial number, and " +
        "validity dates. Validity and serial come from decoding the certificate with System SSL.",
    examples: [{ description: "Show certificate detail", options: "USER01 RING02 -l CERT03" }],
    positionals: [OWNER_POSITIONAL, KEYRING_POSITIONAL],
    options: [{ name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true }],
    profile: { optional: ["ssh"] },
    outputFormatOptions: true,
};

const ConnectCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/connect/Connect.handler`,
    type: "command",
    name: "connect",
    summary: "Connect a certificate to a key ring (read from another ring or the database)",
    description:
        "Connect a certificate to a key ring. R_datalib DataPut requires the certificate bytes, so the " +
        "certificate is read from a ring it is already on (--from-ring) or from the ESM database " +
        "(--from-database) and reconnected to the target ring.",
    examples: [
        {
            description: "Copy a certificate from one ring to another",
            options: "USER01 RING02 -l CACERT --from-ring RING01",
        },
        {
            description: "Connect a certificate that is only in the database",
            options: "USER01 RING02 -l CACERT --from-database",
        },
    ],
    positionals: [
        OWNER_POSITIONAL,
        { name: "keyring", description: "The target key ring name.", type: "string", required: true },
    ],
    options: [
        { name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true },
        {
            name: "from-ring",
            description: "Source key ring the certificate is already on (its bytes are read from here).",
            type: "string",
        },
        {
            name: "from-database",
            aliases: ["from-db"],
            description:
                "Read the certificate from the ESM database instead of a ring (use when it is not on any ring).",
            type: "boolean",
        },
        {
            name: "usage",
            aliases: ["u"],
            description: "Certificate usage (default: the certificate's current usage).",
            type: "string",
            allowableValues: { values: ["PERSONAL", "CERTAUTH"] },
        },
        { name: "default", description: "Set this certificate as the target ring's default.", type: "boolean" },
    ],
    profile: { optional: ["ssh"] },
};

const SetDefaultCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/set-default/SetDefault.handler`,
    type: "command",
    name: "set-default",
    summary: "Make a certificate the key ring's default",
    description: "Set a certificate that is already connected to a key ring as that ring's default certificate.",
    examples: [{ description: "Set the ring default certificate", options: "USER01 RING02 -l CERT03" }],
    positionals: [OWNER_POSITIONAL, KEYRING_POSITIONAL],
    options: [{ name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true }],
    profile: { optional: ["ssh"] },
};

const TrustCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/trust/Trust.handler`,
    type: "command",
    name: "trust",
    summary: "Change a certificate's trust status",
    description:
        "Change a certificate's trust status (ESM DataAlter). A key ring is not required; the certificate is " +
        "identified by owner and label. HIGHTRUST is honored only for CERTAUTH certificates.",
    examples: [{ description: "Mark a certificate NOTRUST", options: "USER01 -l CERT03 -s NOTRUST" }],
    positionals: [OWNER_POSITIONAL],
    options: [
        { name: "label", aliases: ["l"], description: "The certificate label.", type: "string", required: true },
        {
            name: "status",
            aliases: ["s"],
            description: "The new trust status.",
            type: "string",
            required: true,
            allowableValues: { values: ["TRUST", "HIGHTRUST", "NOTRUST"] },
        },
    ],
    profile: { optional: ["ssh"] },
};

const RenameCertDefinition: ICommandDefinition = {
    handler: `${__dirname}/rename/Rename.handler`,
    type: "command",
    name: "rename",
    summary: "Change a certificate's label",
    description:
        "Change a certificate's label (ESM DataAlter). A key ring is not required; the certificate is " +
        "identified by owner and current label.",
    examples: [{ description: "Rename a certificate", options: "USER01 -l OLDLABEL -n NEWLABEL" }],
    positionals: [OWNER_POSITIONAL],
    options: [
        {
            name: "label",
            aliases: ["l"],
            description: "The current certificate label.",
            type: "string",
            required: true,
        },
        {
            name: "new-label",
            aliases: ["n"],
            description: "The new certificate label.",
            type: "string",
            required: true,
        },
    ],
    profile: { optional: ["ssh"] },
};

export const CertDefinition: ICommandDefinition = {
    name: "cert",
    aliases: ["certificate"],
    summary: "Manage certificates: export, import, delete, show, connect, set-default, trust, rename",
    description: "Certificate operations on a key ring or in the ESM database.",
    type: "group",
    children: [
        ExportCertDefinition,
        ImportCertDefinition,
        DeleteCertDefinition,
        ShowCertDefinition,
        ConnectCertDefinition,
        SetDefaultCertDefinition,
        TrustCertDefinition,
        RenameCertDefinition,
        RefreshDefinition,
    ],
};
