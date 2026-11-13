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

    public static final String ZOSMF_DEFAULT = "ISO8859-1";

    private static volatile boolean detectUtf8;

    private ContentCharsets() {
    }

    /** Opt-in (zo.upload.detect-utf8): treat charset-less bodies that are valid UTF-8 as UTF-8. */
    public static void setDetectUtf8(boolean enabled) {
        detectUtf8 = enabled;
    }

    /**
     * Charset for a text upload that did not declare one. z/OSMF specifies ISO8859-1, and that is
     * the default: valid UTF-8 bytes are also valid Latin-1, so guessing could change a compliant
     * upload. With detection enabled, a body that is valid UTF-8 is treated as UTF-8 (useful for
     * Zowe clients that send UTF-8 files with a bare "text/plain").
     */
    public static String defaultFor(byte[] body) {
        return detectUtf8 ? detect(body) : ZOSMF_DEFAULT;
    }

    static String detect(byte[] body) {
        try {
            StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(body == null ? new byte[0] : body));
            return "UTF-8";
        } catch (CharacterCodingException e) {
            return ZOSMF_DEFAULT;
        }
    }
}
