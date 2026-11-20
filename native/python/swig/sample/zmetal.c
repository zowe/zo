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

#include "zmetal.h"
#include <stdio.h>

#pragma prolog(HELLO, "&CCN_MAIN SETB 1 \n MYPROLOG")

int HELLO(const char* name, char* output, int* outsize) {
    snprintf(output, *outsize, "Hello, %s!", name);
    return 0;
}
