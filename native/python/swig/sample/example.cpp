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

#include "example.h"
#include <string>
#include <unistd.h>

extern "C" {
    #include "hello.h"
}

std::string hello_ascii(std::string name) {
    __a2e_s(&name[0]);
    std::string message;
    hello_ebcdic(name, message);
    __e2a_s(&message[0]);
    return message;
}
