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

#ifndef ZTEMP_HPP
#define ZTEMP_HPP

#include "zlog_util.hpp"
#include "zlogger.hpp"
#include <cerrno>
#include <cstdlib>
#include <limits.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace ztemp
{
// Creates or validates ~/.zowex/temp as a private, user-owned directory.
// Throws on unsafe paths or filesystem failures. Callers own their files' cleanup.
inline std::string prepare_dir()
{
  const std::string home = zlog_util::get_home_dir();
  char resolved_home[PATH_MAX];
  if (home.empty() || realpath(home.c_str(), resolved_home) == nullptr)
  {
    throw std::runtime_error("Cannot resolve the home directory for temporary files");
  }

  // Validate from the root down so another account cannot replace a parent directory.
  for (char *end = resolved_home + 1;; ++end)
  {
    if (*end != '/' && *end != '\0')
      continue;
    const char separator = *end;
    *end = '\0';
    struct stat info{};
    const bool trusted = lstat(resolved_home, &info) == 0 && S_ISDIR(info.st_mode) &&
                         (info.st_uid == geteuid() || (separator != '\0' && info.st_uid == 0)) &&
                         (info.st_mode & (S_IWGRP | S_IWOTH)) == 0;
    *end = separator;
    if (!trusted)
    {
      throw std::runtime_error("Temporary files require a user-owned home with trusted, non-group/world-writable directories");
    }
    if (separator == '\0')
      break;
  }

  std::string path = zlog_util::strip_trailing_slashes(resolved_home);
  for (const char *suffix : {"/.zowex", "/temp"})
  {
    path += suffix;
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
    {
      throw std::runtime_error("Failed to create temporary directory: " + path + ", errno: " + std::to_string(errno));
    }
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 07777) != 0700)
    {
      throw std::runtime_error("Temporary directory must be user-owned, non-symlink, and mode 0700: " + path);
    }
  }
  return path;
}
namespace detail
{
inline void remove_owned_file(const std::string &path, bool fifo)
{
  const std::string kind = fifo ? "FIFO" : "temporary file";
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0)
  {
    const int saved_errno = errno;
    if (saved_errno == ENOENT)
      return;
    throw std::runtime_error("Failed to inspect existing " + kind + ": " + path + ", errno: " + std::to_string(saved_errno));
  }
  if ((fifo ? !S_ISFIFO(info.st_mode) : !S_ISREG(info.st_mode)) || info.st_uid != geteuid())
  {
    throw std::runtime_error("Security error: existing " + kind + " path is not a user-owned " + kind + ": " + path);
  }
  if (unlink(path.c_str()) != 0 && errno != ENOENT)
  {
    const int saved_errno = errno;
    const std::string prefix = saved_errno == EPERM ? "Security error: permission denied deleting existing "
                                                    : "Failed to delete existing ";
    throw std::runtime_error(prefix + kind + ": " + path + ", errno: " + std::to_string(saved_errno));
  }
}
} // namespace detail

inline void remove_fifo(const std::string &path)
{
  detail::remove_owned_file(path, true);
}

// Owns a newly created file or FIFO. Destruction logs cleanup failures without throwing.
class FileGuard
{
public:
  FileGuard() : m_path(prepare_dir() + "/file.XXXXXX")
  {
    const int fd = mkstemp(m_path.data());
    if (fd < 0)
    {
      throw std::runtime_error("Failed to create temporary file, errno: " + std::to_string(errno));
    }
    if (close(fd) != 0)
    {
      const int saved_errno = errno;
      unlink(m_path.c_str());
      throw std::runtime_error("Failed to close temporary file, errno: " + std::to_string(saved_errno));
    }
  }

  static std::shared_ptr<FileGuard> create_fifo(const std::string &path)
  {
    return std::shared_ptr<FileGuard>(new FileGuard(path));
  }

  ~FileGuard() noexcept
  {
    const int saved_errno = errno;
    try
    {
      detail::remove_owned_file(m_path, m_fifo);
    }
    catch (const std::exception &e)
    {
      try
      {
        LOG_ERROR("Failed to clean up temporary file: %s", e.what());
      }
      catch (...)
      {
        // Cleanup must remain non-throwing even if logging fails during unwinding.
      }
    }
    errno = saved_errno;
  }

  FileGuard(const FileGuard &) = delete;
  FileGuard &operator=(const FileGuard &) = delete;

  const std::string &path() const
  {
    return m_path;
  }

private:
  explicit FileGuard(const std::string &path) : m_path(path), m_fifo(true)
  {
    if (mkfifo(m_path.c_str(), 0600) != 0)
    {
      throw std::runtime_error("Failed to create FIFO pipe: " + m_path + ", errno: " + std::to_string(errno));
    }
  }

  std::string m_path;
  bool m_fifo = false;
};
} // namespace ztemp

#endif // ZTEMP_HPP
