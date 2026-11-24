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

/** The target already exists (mapped to HTTP 409 by the zo data set controller). */
public class DataSetConflictException extends RuntimeException {
    public DataSetConflictException(String message) {
        super(message);
    }
}
