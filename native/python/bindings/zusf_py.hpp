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

#ifndef ZUSF_PY_H
#define ZUSF_PY_H

#include <string>
#include <stdexcept>
#include "../../c/zusf.hpp"
#include "conversion.hpp"

void create_uss_file(const std::string &file, const std::string &mode);

void create_uss_dir(const std::string &file, const std::string &mode);

std::string list_uss_dir(const std::string &path, ListOptions options = ListOptions(false, false));

void move_uss_file_or_dir(const std::string &source, const std::string &destination);

std::string read_uss_file(const std::string &file, const std::string &codepage = "");

void read_uss_file_streamed(const std::string &file, const std::string &pipe, const std::string &codepage = "", size_t *content_len = nullptr);

std::string write_uss_file(const std::string &file, const std::string &data, const std::string &codepage = "", const std::string &etag = "");

std::string write_uss_file_streamed(const std::string &file, const std::string &pipe, const std::string &codepage = "", const std::string &etag = "", size_t *content_len = nullptr);

void chmod_uss_item(const std::string &file, const std::string &mode, bool recursive = false);

void delete_uss_item(const std::string &file, bool recursive = false);

void chown_uss_item(const std::string &file, const std::string &owner, bool recursive = false);

void chtag_uss_item(const std::string &file, const std::string &tag, bool recursive = false);

#endif