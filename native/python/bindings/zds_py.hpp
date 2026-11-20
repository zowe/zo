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

#ifndef ZDS_PY_HPP
#define ZDS_PY_HPP

#include <string>
#include <vector>
#include "../../c/zdstype.h"
#include "../../c/zds.hpp"
#include "conversion.hpp"

void create_data_set(std::string dsn, const DS_ATTRIBUTES &attributes);

std::vector<ZDSEntry> list_data_sets(std::string dsn, bool show_attributes = false);

std::string read_data_set(std::string dsn, std::string codepage = "");

std::string write_data_set(std::string dsn, std::string data, std::string codepage = "", std::string etag = "");

void delete_data_set(std::string dsn);

void create_member(std::string dsn);

std::vector<ZDSMem> list_members(std::string dsn);

#endif