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

#include "zo_content_c_api.h"
#include "../../c/zds.hpp"
#include "../../c/zusf.hpp"
#include "../../c/zut.hpp"
#include "../../python/bindings/conversion.hpp"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{

std::string ebcdic_arg(const char *ascii)
{
  std::string value = ascii ? ascii : "";
  a2e_inplace(value);
  return value;
}

char *ascii_copy(std::string value)
{
  if (value.empty())
  {
    return nullptr;
  }
  e2a_inplace(value);
  char *copy = static_cast<char *>(std::malloc(value.size() + 1));
  if (copy)
  {
    std::memcpy(copy, value.c_str(), value.size() + 1);
  }
  return copy;
}

ZoContentResult_C *new_result()
{
  return static_cast<ZoContentResult_C *>(std::calloc(1, sizeof(ZoContentResult_C)));
}

ZoContentResult_C *fail(ZoContentResult_C *result, int status, const std::string &message)
{
  result->status = status;
  result->error_message = ascii_copy(message);
  return result;
}

void set_data(ZoContentResult_C *result, const std::string &data)
{
  result->length = data.size();
  result->data = static_cast<char *>(std::malloc(data.size() > 0 ? data.size() : 1));
  if (result->data && !data.empty())
  {
    std::memcpy(result->data, data.data(), data.size());
  }
}

// Mirrors the zo view/write handlers: encoding goes through zut_prepare_encoding, the local
// encoding is the caller-side codepage.
template <typename T>
bool apply_encoding(T &target, const ZoContentOptions_C *options, std::string &error)
{
  if (!options)
  {
    return true;
  }
  if (options->encoding && *options->encoding)
  {
    zut_prepare_encoding(ebcdic_arg(options->encoding), &target.encoding_opts);
  }
  if (options->local_encoding && *options->local_encoding)
  {
    const std::string local = ebcdic_arg(options->local_encoding);
    if (local.size() >= sizeof(target.encoding_opts.source_codepage))
    {
      error = "local encoding is too long";
      return false;
    }
    std::memcpy(target.encoding_opts.source_codepage, local.c_str(), local.size() + 1);
  }
  return true;
}

template <size_t N>
bool apply_etag(char (&target)[N], const ZoContentOptions_C *options, std::string &error)
{
  if (!options || !options->etag || !*options->etag)
  {
    return true;
  }
  const std::string etag = ebcdic_arg(options->etag);
  if (etag.size() >= N)
  {
    error = "etag exceeds " + std::to_string(N - 1) + " character length limit";
    return false;
  }
  std::memcpy(target, etag.c_str(), etag.size() + 1);
  return true;
}

bool is_etag_mismatch(const char *e_msg)
{
  return std::strncmp(e_msg, "Etag mismatch", 13) == 0;
}

void split_member(const std::string &dsname, std::string &base, std::string &member)
{
  const auto open = dsname.find('(');
  const auto close = dsname.find(')', open == std::string::npos ? 0 : open);
  if (open == std::string::npos || close == std::string::npos)
  {
    base = dsname;
    member.clear();
    return;
  }
  base = dsname.substr(0, open);
  member = dsname.substr(open + 1, close - open - 1);
}

bool dataset_target_exists(const std::string &dsname)
{
  std::string base;
  std::string member;
  split_member(dsname, base, member);
  if (!zds_dataset_exists(base))
  {
    return false;
  }
  return member.empty() || zds_member_exists(base, member);
}

// A volser means the data set may be uncataloged: allocate it under a system-generated DD
// name. Fixed DD names are shared by every thread in the address space.
bool allocate_by_volser(const std::string &dsname, const ZoContentOptions_C *options, std::string &ddname,
                        std::vector<std::string> &dds, std::string &error)
{
  if (!options || !options->volser || !*options->volser)
  {
    return true;
  }
  const std::string volser = ebcdic_arg(options->volser);
  unsigned int code = 0;
  std::string response;
  const std::string cmd = "alloc da('" + dsname + "') shr vol(" + volser + ")";
  if (zut_bpxwdyn_rtdd(cmd, &code, response, ddname) != RTNCD_SUCCESS)
  {
    error = "allocation failed: " + response;
    return false;
  }
  dds.push_back("dd(" + ddname + ")");
  return true;
}

void free_dds(const std::vector<std::string> &dds)
{
  if (!dds.empty())
  {
    ZDIAG diag{};
    zut_free_dynalloc_dds(diag, dds);
  }
}

bool valid_dsname_text(const std::string &dsname)
{
  return !dsname.empty() && dsname.find('\'') == std::string::npos;
}

bool valid_volser_text(const ZoContentOptions_C *options)
{
  if (!options || !options->volser)
  {
    return true;
  }
  const size_t len = std::strlen(options->volser);
  if (len == 0 || len > 6)
  {
    return len == 0;
  }
  for (size_t i = 0; i < len; ++i)
  {
    const char c = options->volser[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '#' || c == '$'))
    {
      return false;
    }
  }
  return true;
}

template <typename Fn>
ZoContentResult_C *guarded(Fn &&body)
{
  ZoContentResult_C *result = new_result();
  if (!result)
  {
    return nullptr;
  }
  try
  {
    body(result);
  }
  catch (const std::exception &e)
  {
    std::free(result->data);
    result->data = nullptr;
    result->length = 0;
    fail(result, ZO_CONTENT_FAILED, e.what());
  }
  return result;
}

} // namespace

extern "C"
{

  ZoContentResult_C *zo_c_read_dataset(const char *dsname_ascii, const ZoContentOptions_C *options)
  {
    return guarded([&](ZoContentResult_C *result)
                   {
      const std::string dsname = ebcdic_arg(dsname_ascii);
      if (!valid_dsname_text(dsname) || !valid_volser_text(options))
      {
        fail(result, ZO_CONTENT_INVALID, "invalid data set name or volume");
        return;
      }

      ZDS zds{};
      std::string error;
      if (!apply_encoding(zds, options, error))
      {
        fail(result, ZO_CONTENT_INVALID, error);
        return;
      }

      std::string ddname;
      std::vector<std::string> dds;
      if (!allocate_by_volser(dsname, options, ddname, dds, error))
      {
        fail(result, ZO_CONTENT_FAILED, error);
        return;
      }

      ZDSReadOpts read_opts{.zds = &zds, .ddname = ddname, .dsname = dsname};
      std::string content;
      const int rc = zds_read(read_opts, content);
      free_dds(dds);

      if (rc != RTNCD_SUCCESS)
      {
        if (dds.empty() && !dataset_target_exists(dsname))
        {
          fail(result, ZO_CONTENT_NOT_FOUND, "data set or member not found");
          return;
        }
        fail(result, ZO_CONTENT_FAILED, std::string("could not read data set: ") + zds.diag.e_msg);
        return;
      }

      std::stringstream etag;
      etag << std::hex << zut_calc_adler32_checksum(content);
      set_data(result, content);
      result->etag = ascii_copy(etag.str());
      result->status = ZO_CONTENT_OK; });
  }

  ZoContentResult_C *zo_c_write_dataset(const char *dsname_ascii, const char *data, size_t length,
                                        const ZoContentOptions_C *options)
  {
    return guarded([&](ZoContentResult_C *result)
                   {
      const std::string dsname = ebcdic_arg(dsname_ascii);
      if (!valid_dsname_text(dsname) || !valid_volser_text(options))
      {
        fail(result, ZO_CONTENT_INVALID, "invalid data set name or volume");
        return;
      }

      ZDS zds{};
      std::string error;
      if (!apply_encoding(zds, options, error) || !apply_etag(zds.etag, options, error))
      {
        fail(result, ZO_CONTENT_INVALID, error);
        return;
      }

      const bool by_volume = options && options->volser && *options->volser;
      std::string base;
      std::string member;
      split_member(dsname, base, member);
      if (!by_volume && !zds_dataset_exists(base))
      {
        fail(result, ZO_CONTENT_NOT_FOUND, "data set not found");
        return;
      }
      result->created = (!by_volume && !member.empty() && !zds_member_exists(base, member)) ? 1 : 0;

      std::string ddname;
      std::vector<std::string> dds;
      if (!allocate_by_volser(dsname, options, ddname, dds, error))
      {
        fail(result, ZO_CONTENT_FAILED, error);
        return;
      }

      ZDSWriteOpts write_opts{.zds = &zds, .ddname = ddname, .dsname = dsname};
      const std::string content(data ? data : "", data ? length : 0);
      const int rc = zds_write(write_opts, content);
      free_dds(dds);

      if (rc == RTNCD_WARNING && zds.diag.detail_rc == ZDS_RSNCD_TRUNCATION_WARNING)
      {
        result->warning = ascii_copy(zds.diag.e_msg);
      }
      else if (rc != RTNCD_SUCCESS)
      {
        const int status = is_etag_mismatch(zds.diag.e_msg) ? ZO_CONTENT_ETAG_MISMATCH : ZO_CONTENT_FAILED;
        fail(result, status, std::string("could not write data set: ") + zds.diag.e_msg);
        return;
      }

      result->etag = ascii_copy(zds.etag);
      result->status = ZO_CONTENT_OK; });
  }

  ZoContentResult_C *zo_c_read_file(const char *path_ascii, const ZoContentOptions_C *options)
  {
    return guarded([&](ZoContentResult_C *result)
                   {
      const std::string path = ebcdic_arg(path_ascii);
      if (path.empty() || path[0] != '/')
      {
        fail(result, ZO_CONTENT_INVALID, "path must be absolute");
        return;
      }

      ZUSF zusf{};
      std::string error;
      if (!apply_encoding(zusf, options, error))
      {
        fail(result, ZO_CONTENT_INVALID, error);
        return;
      }

      struct stat file_stats;
      if (stat(path.c_str(), &file_stats) == -1)
      {
        fail(result, ZO_CONTENT_NOT_FOUND, "path not found");
        return;
      }

      std::string content;
      if (zusf_read_from_uss_file(&zusf, path, content) != RTNCD_SUCCESS)
      {
        fail(result, ZO_CONTENT_FAILED, std::string("could not read file: ") + zusf.diag.e_msg);
        return;
      }

      set_data(result, content);
      result->etag = ascii_copy(zut_build_etag(file_stats.st_mtime, file_stats.st_size));
      result->status = ZO_CONTENT_OK; });
  }

  ZoContentResult_C *zo_c_write_file(const char *path_ascii, const char *data, size_t length,
                                     const ZoContentOptions_C *options)
  {
    return guarded([&](ZoContentResult_C *result)
                   {
      const std::string path = ebcdic_arg(path_ascii);
      if (path.empty() || path[0] != '/')
      {
        fail(result, ZO_CONTENT_INVALID, "path must be absolute");
        return;
      }

      ZUSF zusf{};
      std::string error;
      if (!apply_encoding(zusf, options, error) || !apply_etag(zusf.etag, options, error))
      {
        fail(result, ZO_CONTENT_INVALID, error);
        return;
      }

      struct stat before;
      result->created = lstat(path.c_str(), &before) == -1 ? 1 : 0;

      std::string content(data ? data : "", data ? length : 0);
      if (zusf_write_to_uss_file(&zusf, path, content) != RTNCD_SUCCESS)
      {
        const int status = is_etag_mismatch(zusf.diag.e_msg) ? ZO_CONTENT_ETAG_MISMATCH : ZO_CONTENT_FAILED;
        fail(result, status, std::string("could not write file: ") + zusf.diag.e_msg);
        return;
      }

      result->etag = ascii_copy(zusf.etag);
      result->status = ZO_CONTENT_OK; });
  }

  void zo_c_free_content_result(ZoContentResult_C *result)
  {
    if (!result)
    {
      return;
    }
    std::free(result->data);
    std::free(result->etag);
    std::free(result->warning);
    std::free(result->error_message);
    std::free(result);
  }
}
