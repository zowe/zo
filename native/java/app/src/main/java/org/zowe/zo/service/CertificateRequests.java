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

package org.zowe.zo.service;

import com.fasterxml.jackson.annotation.JsonProperty;

/** Request bodies for certificate operations. Records holding passwords never print them. */
public final class CertificateRequests {

    private CertificateRequests() {
    }

    public record ExportRequest(String format, String file, String dsn, String password) {
        @Override
        public String toString() {
            return "ExportRequest[format=" + format + "]";
        }
    }

    public record ImportRequest(String owner, String keyring, String label, String usage, String file, String dsn,
                                String password, Boolean skipRefresh) {
        @Override
        public String toString() {
            return "ImportRequest[owner=" + owner + ", keyring=" + keyring + "]";
        }
    }

    public record CertificateQuery(String label, String usage, Boolean labelOnly, Boolean ownerOnly, Integer maxEntries) {
    }

    /** Ring-scoped action: request is "connect" or "set-default". */
    public record RingCertificateAction(String request, String fromRing, Boolean fromDatabase, String usage,
                                        @JsonProperty("default") Boolean makeDefault) {
    }

    /** Owner-scoped action: request is "rename" or "trust". */
    public record OwnerCertificateAction(String request, String newLabel, String status) {
    }
}
