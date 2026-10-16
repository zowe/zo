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

#include "zkrio.hpp"
#include "zds.hpp"
#include "ztype.h"
#include "zut.hpp"

// Read a PKCS#12 blob out of a sequential data set or PDS/E member. Binary mode so
// LE concatenates V-format record data with no RDWs and no code-page conversion --
// byte-identical to `cp -B "//'DSN'"`.
int zkrio_read_dsn(const std::string &dsn, std::string &data, std::string &err)
{
  if (!zds_dataset_exists(dsn))
  {
    err = "Could not access data set: " + dsn;
    return RTNCD_FAILURE;
  }

  ZDS zds{};
  zut_prepare_encoding("binary", &zds.encoding_opts);
  const int rc = zds_read(ZDSReadOpts{.zds = &zds, .dsname = dsn}, data);
  if (rc != RTNCD_SUCCESS)
  {
    err = zds.diag.e_msg;
    return RTNCD_FAILURE;
  }
  if (data.empty())
  {
    err = "PKCS#12 data set is empty: " + dsn;
    return RTNCD_FAILURE;
  }
  return RTNCD_SUCCESS;
}

// Write exported certificate bytes to a data set, creating it when absent.
// is_binary=true  (p12): raw bytes, chunked into V-format records (zds_write_binary).
// is_binary=false (pem): the EBCDIC PEM text is written as records via zds_write in
//                        text mode -- one line per record.
int zkrio_write_dsn(const std::string &dsn, const std::string &data, bool is_binary, std::string &err)
{
  if (!zds_dataset_exists(dsn))
  {
    const auto open_paren = dsn.find('(');
    const auto close_paren = dsn.find(')');
    const bool has_member = open_paren != std::string::npos && close_paren != std::string::npos && close_paren > open_paren;
    const std::string base_dsn = has_member ? dsn.substr(0, open_paren) : dsn;

    if (has_member)
    {
      const std::string member = dsn.substr(open_paren + 1, close_paren - open_paren - 1);
      if (!zds_is_valid_member_name(member))
      {
        err = "Invalid member name: " + member;
        return RTNCD_FAILURE;
      }
    }

    // RACDCERT FORMAT(PKCS12DER) parity: PS/VB/LRECL(84)/BLKSIZE(27998).
    DS_ATTRIBUTES a{}; // zero-init: zds_create_dsn preserves 0, only negative means "unset"
    a.dsorg = has_member ? "PO" : "PS";
    a.recfm = "V,B"; // mandatory -- FB pads records with blanks, which would change the byte count
    a.lrecl = 84;
    a.blksize = 27998;
    a.alcunit = "TRK";
    a.primary = 5;
    a.secondary = 5;
    if (has_member)
    {
      a.dirblk = 5;
      a.dsntype = ZDS_DSNTYPE_LIBRARY;
    }

    std::string response;
    if (zds_create_dsn(nullptr, base_dsn, a, response) != RTNCD_SUCCESS)
    {
      err = "Could not create data set '" + base_dsn + "': " + response;
      return RTNCD_FAILURE;
    }
  }

  ZDS zds{}; // eDataTypeText + empty codepage -> zds_use_codepage() is false -> no iconv for PEM
  const int rc = is_binary
                     ? zds_write_binary(ZDSWriteOpts{.zds = &zds, .dsname = dsn}, data)
                     : zds_write(ZDSWriteOpts{.zds = &zds, .dsname = dsn}, data);
  if (rc != RTNCD_SUCCESS)
  {
    err = zds.diag.e_msg;
    return RTNCD_FAILURE;
  }
  return RTNCD_SUCCESS;
}

int zkrio_write_file(const std::string &path, const std::string &data, std::string &err)
{
  return zut_write_file_private(path, data, err);
}
