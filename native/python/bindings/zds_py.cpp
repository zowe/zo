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

#include "zds_py.hpp"
#include <unistd.h>
#include <stdexcept>

/**
 * Dataset Functions
 */

void create_data_set(std::string dsn, const DS_ATTRIBUTES &attributes)
{
  ZDS zds = {0};
  std::string response;

  DS_ATTRIBUTES attrs_copy = attributes;

  a2e_inplace(dsn);

  a2e_inplace(attrs_copy.alcunit);
  a2e_inplace(attrs_copy.dsorg);
  a2e_inplace(attrs_copy.recfm);
  a2e_inplace(attrs_copy.dataclass);
  a2e_inplace(attrs_copy.unit);
  a2e_inplace(attrs_copy.dsntype);
  a2e_inplace(attrs_copy.mgntclass);
  a2e_inplace(attrs_copy.dsname);
  a2e_inplace(attrs_copy.storclass);
  a2e_inplace(attrs_copy.vol);

  int rc = zds_create_dsn(&zds, dsn, attrs_copy, response);
  if (rc != 0)
  {
    // zds_create_dsn reports failures through `response`; it never touches zds.diag.
    e2a_inplace(response);
    throw std::runtime_error(response);
  }
}

std::vector<ZDSEntry> list_data_sets(std::string dsn, bool show_attributes)
{
  std::vector<ZDSEntry> entries;
  ZDS zds = {0};

  a2e_inplace(dsn);
  // dsorg, volser, recfm and migrated stay empty unless attributes are requested.
  int rc = zds_list_data_sets(&zds, dsn, entries, show_attributes);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }

  for (auto &e : entries)
  {
    e2a_inplace(e.name);
    e2a_inplace(e.dsorg);
    e2a_inplace(e.volser);
    e2a_inplace(e.recfm);
  }
  return entries;
}

std::string read_data_set(std::string dsn, std::string codepage)
{
  ZDS zds{};

  if (!codepage.empty())
  {
    zds.encoding_opts.data_type = codepage == "binary" ? eDataTypeBinary : eDataTypeText;
    strncpy(zds.encoding_opts.codepage, codepage.c_str(), sizeof(zds.encoding_opts.codepage) - 1);
  }

  a2e_inplace(dsn);
  ZDSReadOpts read_opts{.zds = &zds, .dsname = dsn};
  std::string response;
  int rc = zds_read(read_opts, response);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }

  e2a_inplace(response);
  return response;
}

std::string write_data_set(std::string dsn, std::string data, std::string codepage, std::string etag)
{
  ZDS zds = {0};

  if (!codepage.empty())
  {
    zds.encoding_opts.data_type = codepage == "binary" ? eDataTypeBinary : eDataTypeText;
    strncpy(zds.encoding_opts.codepage, codepage.c_str(), sizeof(zds.encoding_opts.codepage) - 1);
  }

  if (!etag.empty())
  {
    strncpy(zds.etag, etag.c_str(), sizeof(zds.etag) - 1);
  }

  a2e_inplace(dsn);
  a2e_inplace(data);
  int rc = zds_write({.zds = &zds, .dsname = dsn}, data);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }

  std::string new_etag(zds.etag);
  e2a_inplace(new_etag);
  return new_etag;
}

void delete_data_set(std::string dsn)
{
  ZDS zds = {0};
  a2e_inplace(dsn);
  int rc = zds_delete_dsn(&zds, dsn);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }
}

/**
 * Dataset Member Functions
 */

void create_member(std::string dsn)
{
  ZDS zds = {0};
  a2e_inplace(dsn);
  std::string empty_data = "";
  a2e_inplace(empty_data);
  int rc = zds_write({.zds = &zds, .dsname = dsn}, empty_data);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }
}

std::vector<ZDSMem> list_members(std::string dsn)
{
  std::vector<ZDSMem> members;
  ZDS zds = {0};

  a2e_inplace(dsn);
  int rc = zds_list_members(&zds, dsn, members);

  if (rc != 0)
  {
    std::string diag(zds.diag.e_msg, zds.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }

  for (auto &m : members)
  {
    e2a_inplace(m.name);
  }
  return members;
}
