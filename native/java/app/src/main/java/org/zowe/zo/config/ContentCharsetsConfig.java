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

package org.zowe.zo.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import org.zowe.zo.service.ContentCharsets;

/** Applies zo.upload.detect-utf8 (default false: z/OSMF's ISO8859-1 default for charset-less uploads). */
@Component
public class ContentCharsetsConfig {

    public ContentCharsetsConfig(@Value("${zo.upload.detect-utf8:false}") boolean detectUtf8) {
        ContentCharsets.setDetectUtf8(detectUtf8);
    }
}
