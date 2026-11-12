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

#ifndef ZUSF_HPP
#define ZUSF_HPP

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif
#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include "zusftype.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

struct CopyOptions
{
  bool recursive;
  bool follow_symlinks;
  bool preserve_attributes;
  bool force;
  explicit CopyOptions(bool recursive = false, bool follow_symlinks = false, bool preserve_attributes = false, bool force = false)
      : recursive(recursive), follow_symlinks(follow_symlinks), preserve_attributes(preserve_attributes), force(force)
  {
  }
};

struct CreateOptions
{
  bool create_dir;
  bool overwrite;
  explicit CreateOptions(bool create_dir = false, bool overwrite = false)
      : create_dir(create_dir), overwrite(overwrite)
  {
  }
};

struct ListOptions
{
  bool all_files;
  bool long_format;
  int max_depth;
  explicit ListOptions(bool all_files = false, bool long_format = false, int max_depth = 1)
      : all_files(all_files), long_format(long_format), max_depth(max_depth)
  {
  }
};

struct ZusfListEntry
{
  std::string name;
  std::string mode;
  int64_t links;
  std::string user;
  std::string group;
  int64_t size;
  std::string filetag;
  std::string mtime;
};

// The Python bindings compile this header EBCDIC and their SWIG wrappers ASCII. libc++ uses a
// distinct inline namespace per char mode (std::__1 vs std::__1_a), so a mangled name is
// unresolvable across that boundary -- everything the bindings call needs C linkage.
#ifdef SWIG
extern "C"
{
#endif
int zusf_copy_file_or_dir(ZUSF *zusf, const std::string &source_fs, const std::string &dest_fs, const CopyOptions &options);
int zusf_create_uss_file_or_dir(ZUSF *zusf, const std::string &file, mode_t mode, const CreateOptions &options);
int zusf_move_uss_file_or_dir(ZUSF *zusf, const std::string &source, const std::string &target, bool force = true);
int zusf_list_uss_file_path(ZUSF *zusf, const std::string &file, std::string &response, ListOptions options = ListOptions{}, bool use_csv_format = false, std::vector<ZusfListEntry> *entries = nullptr);
int zusf_read_from_uss_file(ZUSF *zusf, const std::string &file, std::string &response);
int zusf_read_from_uss_file_streamed(ZUSF *zusf, const std::string &file, const std::string &pipe, size_t *content_len);
int zusf_write_to_uss_file(ZUSF *zusf, const std::string &file, std::string &data);
int zusf_write_to_uss_file_streamed(ZUSF *zusf, const std::string &file, const std::string &pipe, size_t *content_len);
int zusf_chmod_uss_file_or_dir(ZUSF *zusf, const std::string &file, mode_t mode, bool recursive);
int zusf_delete_uss_item(ZUSF *zusf, const std::string &file, bool recursive);
int zusf_chown_uss_file_or_dir(ZUSF *zusf, const std::string &file, const std::string &owner, bool recursive);
int zusf_chtag_uss_file_or_dir(ZUSF *zusf, const std::string &file, const std::string &tag, bool recursive);
#ifdef SWIG
}
#endif

short zusf_get_id_from_user_or_group(const std::string &user_or_group, bool is_user);
int zusf_get_file_ccsid(ZUSF *zusf, const std::string &file);
std::string zusf_get_ccsid_display_name(int ccsid);
int zusf_get_ccsid_from_display_name(const std::string &display_name);
std::string zusf_get_owner_from_uid(uid_t uid);
std::string zusf_get_group_from_gid(gid_t gid);
std::string zusf_format_ls_time(time_t mtime, bool use_csv_format = false);
std::string zusf_format_file_entry(ZUSF *zusf, const struct stat &file_stats, const std::string &file_path, const std::string &display_name, ListOptions options, bool use_csv_format, std::vector<ZusfListEntry> *entries = nullptr);

#endif
