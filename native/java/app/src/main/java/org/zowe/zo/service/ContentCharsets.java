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

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;

/** Caller-side charset rules for text uploads. */
public final class ContentCharsets {

    private ContentCharsets() {
    }

    /**
     * Charset for a text upload that did not declare one. z/OSMF assumes ISO8859-1; Zowe clients
     * send UTF-8 files with a bare "text/plain". A body that is valid UTF-8 is treated as UTF-8 (ASCII
     * is identical either way, and Latin-1 text with accented characters is practically never valid
     * UTF-8); anything else keeps z/OSMF's ISO8859-1 default.
     */
    public static String defaultFor(byte[] body) {
        try {
            StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(body == null ? new byte[0] : body));
            return "UTF-8";
        } catch (CharacterCodingException e) {
            return "ISO8859-1";
        }
    }
}
