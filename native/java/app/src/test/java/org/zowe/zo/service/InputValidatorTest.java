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

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class InputValidatorTest {

    @Test
    void pathIsNormalizedLexically() {
        assertEquals("/u/a/b", InputValidator.path("/u//a/./b/"));
        assertEquals("/u/b", InputValidator.path("/u/a/../b"));
        assertEquals("/", InputValidator.path("/.."));
    }

    @Test
    void pathRejectsRelativeAndControlCharacters() {
        assertThrows(IllegalArgumentException.class, () -> InputValidator.path("u/a"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.path("/u/a\0b"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.path("/u/a\nb"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.path(""));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.path("/" + "a".repeat(1100)));
    }

    @Test
    void modesAreValidatedAndSymbolicConverted() {
        assertEquals("755", InputValidator.mode("755"));
        assertEquals("0644", InputValidator.mode("0644"));
        assertEquals("750", InputValidator.mode("rwxr-x---"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.mode("789"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.mode("u+x"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.mode("755; rm"));
    }

    @Test
    void jobNamesAndIds() {
        assertEquals("MY$JOB#1", InputValidator.jobName("my$job#1"));
        assertEquals("MY*", InputValidator.jobNamePattern("my*"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.jobName("MY*"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.jobName("TOOLONGNAME"));
        assertEquals("J0123456", InputValidator.jobId("j0123456"));
        assertEquals("STC00123", InputValidator.jobId("STC00123"));
        assertEquals("TSU00123", InputValidator.jobId("TSU00123"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.jobId("XYZ00123"));
    }

    @Test
    void ownerGroupAndCodeset() {
        assertEquals("IBMUSER", InputValidator.owner("IBMUSER"));
        assertEquals("0", InputValidator.owner("0"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.owner("a:b"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.owner("toolongname"));
        assertEquals("IBM-1047", InputValidator.codeset("IBM-1047"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.codeset("a b"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.codeset("$(x)"));
    }

    @Test
    void correlatorIsBoundedPrintable() {
        assertEquals("J0000001SY1.....ABCDEF", InputValidator.correlator("J0000001SY1.....ABCDEF"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.correlator("short"));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.correlator("x".repeat(65)));
        assertThrows(IllegalArgumentException.class, () -> InputValidator.correlator("abc def ghi jkl"));
    }
}
