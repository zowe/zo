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

package org.zowe.zo.ffm;

/**
 * Binary-safe content I/O for data sets and USS files over typed FFM shims.
 *
 * Semantics mirror zo's readDataset/writeDataset/readFile/writeFile RPCs:
 * {@code encoding} is the codepage of the z/OS resource (null = zo default, "binary" = no
 * conversion) and {@code localEncoding} is the codepage of the bytes exchanged with the
 * caller (null = UTF-8). ETags are opaque strings; pass the last one to a write to fail
 * with a conflict if the resource changed.
 */
public interface ZoContentIo {

    String BINARY = "binary";

    record Options(String encoding, String localEncoding, String volume) {
        public static final Options DEFAULT = new Options(null, null, null);

        public boolean isBinary() {
            return BINARY.equalsIgnoreCase(encoding);
        }
    }

    record Content(byte[] data, String etag) {
    }

    record WriteResult(String etag, String truncationWarning) {
    }

    /** @param dsname data set name, optionally with member: {@code HLQ.DATA(MEMBER)} */
    Content readDataset(String dsname, Options options) throws ZoRpcException;

    /** @param etag expected current ETag, or null to overwrite unconditionally */
    WriteResult writeDataset(String dsname, byte[] data, Options options, String etag) throws ZoRpcException;

    Content readFile(String path, Options options) throws ZoRpcException;

    WriteResult writeFile(String path, byte[] data, Options options, String etag) throws ZoRpcException;
}
