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

#include "hello.h"
#include "zmetal.h"
#include <string>

int hello_ebcdic(std::string name, std::string& message) {
    char buffer[64];
    int bufsize = sizeof(buffer);
    HELLO(name.c_str(), buffer, &bufsize);
    message += buffer;
    return 0;
}
