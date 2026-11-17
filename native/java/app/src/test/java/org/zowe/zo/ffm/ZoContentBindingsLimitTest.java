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

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** Oversized native results are rejected from their length alone, before any Java array exists. */
class ZoContentBindingsLimitTest {

    @Test
    void rejectsOversizedLengthBeforeAllocation() {
        var e = assertThrows(ZoRpcException.class,
                () -> ZoContentBindings.requireWithinLimit("readSpool", ZoContentBindings.MAX_CONTENT_BYTES + 1));
        assertEquals(ZoRpcException.INVALID_PARAMS, e.getCode());
        assertThrows(ZoRpcException.class, () -> ZoContentBindings.requireWithinLimit("readSpool", Long.MAX_VALUE));
        assertThrows(ZoRpcException.class, () -> ZoContentBindings.requireWithinLimit("readSpool", -1));
    }

    @Test
    void acceptsLengthsUpToTheLimit() {
        assertDoesNotThrow(() -> ZoContentBindings.requireWithinLimit("readFile", 0));
        assertDoesNotThrow(() -> ZoContentBindings.requireWithinLimit("readFile", ZoContentBindings.MAX_CONTENT_BYTES));
    }
}
