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

#include "ztest.hpp"
#include "zutils.hpp"
#include "../zut.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <random>

int execute_command_with_input(const std::string &command, const std::string &input, bool suppress_output)
{
  std::string final_command = command;
  if (suppress_output)
  {
    final_command += " > /dev/null";
  }

  FILE *pipe = popen(final_command.c_str(), "w");
  if (!pipe)
  {
    throw std::runtime_error("Failed to open pipe for writing");
  }

  if (!input.empty())
  {
    if (fprintf(pipe, "%s", input.c_str()) < 0)
    {
      pclose(pipe);
      throw std::runtime_error("Failed to write to pipe");
    }
  }

  int exit_status = pclose(pipe);
  return WEXITSTATUS(exit_status);
}

int execute_command_with_output(const std::string &command, std::string &output)
{
  output = "";

  // Open the pipe in "read" mode and redirect stderr to stdout
  FILE *pipe = popen((command + " 2>&1").c_str(), "r");
  if (!pipe)
  {
    throw std::runtime_error("Failed to open pipe for reading");
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
  {
    output += buffer;
  }

  int exit_status = pclose(pipe);
  return WEXITSTATUS(exit_status);
}

int execute_su_command_with_output(const std::string &command, std::string &output)
{
  std::string su_command = "echo '" + command + "' | su";
  return execute_command_with_output(su_command, output);
}

static std::mt19937 &get_rng()
{
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

std::string get_random_string(const int length, const bool allNumbers)
{
  auto &rng = get_rng();
  std::uniform_int_distribution<int> digit_dist(0, 9);
  std::uniform_int_distribution<int> letter_dist(0, 25);
  static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::string ret;
  ret.reserve(length);
  for (int i = 0; i < length; ++i)
  {
    if (allNumbers)
    {
      ret += std::to_string(digit_dist(rng));
    }
    else
    {
      ret += letters[letter_dist(rng)];
    }
  }
  return ret;
}

std::string get_basename(const std::string &fspath)
{
  if (fspath.empty())
    return "";

  auto lastSlash = fspath.find_last_of("/\\");

  return (lastSlash == std::string::npos) ? std::string(fspath) : fspath.substr(lastSlash + 1);
}

std::string get_random_uss(const std::string &base_dir)
{
  std::string ret{base_dir};
  if (ret.back() != '/')
  {
    ret.push_back('/');
  }

  ret += "test_" + get_random_string(10);

  return ret;
}

static std::string s_user = "";
std::string get_user()
{
  if (s_user.empty())
  {
    std::string user;
    // Note: using `basename $HOME` instead of `whoami` to get the current user
    // because `whoami` may be mapped to a kernel user instead of a real one.
    execute_command_with_output("basename $HOME | tr '[:lower:]' '[:upper:]'", user);
    s_user = ztst::TrimChars(user);
  }
  return s_user;
}

std::string get_random_ds(const int qualifier_count, const std::string &hlq)
{
  const auto q = hlq.length() == 0 ? get_user() : hlq;
  std::string ret{q + ".ZNP#TEST"};
  for (int i = 0; i < qualifier_count - 2; ++i)
  {
    ret.append(".Z").append(get_random_string());
  }
  return ret;
}

// Helper function to get etag from command response
std::string parse_etag_from_output(const std::string &output)
{
  const std::string label = "etag: ";
  size_t etag_label_pos = output.find(label);

  if (etag_label_pos == std::string::npos)
  {
    return "";
  }

  size_t start_value_pos = etag_label_pos + label.length();

  size_t end_value_pos = output.find_first_of("\r\n", start_value_pos);

  if (end_value_pos == std::string::npos)
  {
    end_value_pos = output.length();
  }

  std::string etag = output.substr(start_value_pos, end_value_pos - start_value_pos);

  return etag;
}

std::vector<std::string> parse_rfc_response(const std::string input, const char *delim)
{
  std::vector<std::string> ret;
  std::string current;
  char delimiter = delim[0];

  for (size_t i = 0; i < input.length(); ++i)
  {
    if (input[i] == delimiter)
    {
      ret.push_back(ztst::TrimChars(current));
      current.clear();
    }
    else
    {
      current += input[i];
    }
  }
  ret.push_back(ztst::TrimChars(current));

  return ret;
}

bool wait_for_job(const std::string &jobid, int max_retries, int delay_ms)
{
  std::string output;
  for (int i = 0; i < max_retries; ++i)
  {
    int rc = execute_command_with_output(zo_command + " job view-status " + jobid, output);
    if (rc == 0 && output.find(jobid) != std::string::npos)
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  return false;
}

// Data set creation helpers
void create_dsn_with_attrs(ZDS *zds, const std::string &dsn, const DS_ATTRIBUTES &attrs, const std::string &type_name)
{
  memset(zds, 0, sizeof(ZDS));
  std::string response;
  int rc = zds_create_dsn(zds, dsn, attrs, response);
  if (rc != 0)
  {
    std::string err = zds->diag.e_msg_len > 0 ? std::string(zds->diag.e_msg)
                      : response.length() > 0 ? response
                                              : "rc=" + std::to_string(rc);
    throw std::runtime_error("Failed to create " + type_name + ": " + err);
  }
}

void create_pds(ZDS *zds, const std::string &dsn)
{
  DS_ATTRIBUTES attrs{};
  attrs.dsorg = "PO";
  attrs.dsntype = "PDS";
  attrs.recfm = "F,B";
  attrs.lrecl = 80;
  attrs.blksize = 800;
  attrs.dirblk = 5;
  create_dsn_with_attrs(zds, dsn, attrs, "PDS");
}

void create_pdse(ZDS *zds, const std::string &dsn)
{
  DS_ATTRIBUTES attrs{};
  attrs.dsorg = "PO";
  attrs.dsntype = "LIBRARY";
  attrs.recfm = "F,B";
  attrs.lrecl = 80;
  attrs.blksize = 800;
  attrs.dirblk = 5;
  create_dsn_with_attrs(zds, dsn, attrs, "PDSE");
}

void create_seq(ZDS *zds, const std::string &dsn)
{
  DS_ATTRIBUTES attrs{};
  attrs.dsorg = "PS";
  attrs.recfm = "F,B";
  attrs.lrecl = 80;
  attrs.blksize = 800;
  attrs.primary = 1;
  attrs.secondary = 1;
  create_dsn_with_attrs(zds, dsn, attrs, "sequential data set");
}

TestVsamGuard::TestVsamGuard(const std::string &organization, bool retained)
    : m_dsn(get_random_ds(3) + ".VSAM")
{
  if (organization != "INDEXED" && organization != "NONINDEXED" && organization != "NUMBERED" && organization != "LINEAR")
    throw std::invalid_argument("Invalid VSAM organization");

  ZDS zds{};
  const std::string temporary_dsn = get_random_ds(3);
  create_seq(&zds, temporary_dsn);
  std::vector<ZDSEntry> entries;
  const int list_rc = zds_list_data_sets(&zds, temporary_dsn, entries, true);
  const int delete_rc = zds_delete_dsn(&zds, temporary_dsn);
  if (list_rc != 0 || entries.empty() || delete_rc != 0)
    throw std::runtime_error("Could not determine a volume for the VSAM test cluster");

  std::string input = " DEFINE CLUSTER ( -\n NAME(" + m_dsn + ") -\n " + organization + " -\n TRACKS(1 1) -\n VOLUMES(" + entries[0].volser + ") -\n ";
  if (organization != "LINEAR")
    input += "RECORDSIZE(80 80) -\n ";
  if (organization == "INDEXED")
    input += "KEYS(8 0) -\n ";
  if (retained)
    input += "FOR(1) -\n ";
  input += "SHAREOPTIONS(2 3))\n";
  std::string output;
  std::string error;
  const int rc = zds_idcams(input, output, error);
  if (rc != 0)
  {
    std::string cleanup_output;
    std::string cleanup_error;
    zds_idcams(" DELETE " + m_dsn + " CLUSTER NOERASE PURGE\n", cleanup_output, cleanup_error);
    throw std::runtime_error("Could not define VSAM test cluster: " + error + "\n" + output);
  }
}

TestVsamGuard::~TestVsamGuard()
{
  std::string output;
  std::string error;
  zds_idcams(" DELETE " + m_dsn + " CLUSTER NOERASE PURGE\n SET MAXCC = 0\n", output, error);
}

const std::string &TestVsamGuard::name() const
{
  return m_dsn;
}

void write_to_dsn(const std::string &dsn, const std::string &content)
{
  ZDS zds{};
  std::string data = content;
  ZDSWriteOpts write_opts{.zds = &zds, .dsname = dsn};
  zds_write(write_opts, data);
}

TestFileGuard::TestFileGuard(const char *_filename, const char &mode, const char *_link)
    : fp()
{
  if (mode == 'p')
  {
    mkfifo(_filename, 0666);
    _file = std::string(_filename);
  }
  else if (mode == 'l')
  {
    symlink(_link, _filename);
    _file = std::string(_filename);
  }
  else
  {
    _file = std::string(_filename);
    fp = FileGuard(_filename, std::string(1, mode).c_str());
  }
}

void TestFileGuard::reset(const char *_filename)
{
  struct stat file_stats;
  if (stat(_file.c_str(), &file_stats) == 0)
  {
    if (S_ISDIR(file_stats.st_mode))
    {
      rmdir(_file.c_str());
    }
    else
    {
      unlink(_file.c_str());
    }
  }
  _file = std::string(_filename);
  if (stat(_file.c_str(), &file_stats) == -1)
  {
    fp = FileGuard(_file.c_str(), "w");
  }
}

TestFileGuard::~TestFileGuard()
{
  unlink(_file.c_str());
}

TestFileGuard::operator FILE *() const
{
  return fp;
}

TestFileGuard::operator bool() const
{
  return fp != nullptr;
}

TestDirGuard::TestDirGuard(const char *_dirname, const mode_t mode)
    : _dir(std::string(_dirname))
{
  struct stat dir_stats;
  if (stat(_dir.c_str(), &dir_stats) == 0 && S_ISDIR(dir_stats.st_mode))
  {
    rmdir(_dir.c_str());
  }
  mkdir(_dir.c_str(), mode);
}

TestDirGuard::~TestDirGuard()
{
  struct stat dir_stats;
  if (stat(_dir.c_str(), &dir_stats) == 0)
  {
    if (S_ISDIR(dir_stats.st_mode))
    {
      rmdir(_dir.c_str());
    }
    else
    {
      unlink(_dir.c_str());
    }
  }
}

void TestDirGuard::reset(const char *_dirname)
{
  struct stat dir_stats;
  if (stat(_dir.c_str(), &dir_stats) == 0)
  {
    if (S_ISDIR(dir_stats.st_mode))
    {
      rmdir(_dir.c_str());
    }
    else
    {
      unlink(_dir.c_str());
    }
  }
  _dir = std::string(_dirname);
  if (stat(_dir.c_str(), &dir_stats) == -1)
  {
    mkdir(_dir.c_str(), 0755);
  }
}

TestDirGuard::operator std::string() const
{
  return _dir;
}

std::thread start_pipe_writer_thread(const std::string &pipe_path, const std::string &data_to_write)
{
  return std::thread([pipe_path, data_to_write]() -> void
                     {
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd == -1; ++attempt)
    {
      fd = open(pipe_path.c_str(), O_WRONLY | O_NONBLOCK);
      if (fd == -1)
      {
        usleep(10000);
      }
    }
    if (fd != -1)
    {
      write(fd, data_to_write.data(), data_to_write.size());
      close(fd);
    } });
}

std::thread start_pipe_reader_thread(const std::string &pipe_path, std::string *output_content)
{
  return std::thread([pipe_path, output_content]() -> void
                     {
    if (output_content == nullptr)
    {
      return;
    }
    int fd = open(pipe_path.c_str(), O_RDONLY);
    if (fd != -1)
    {
      char buffer[4096];
      ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
      if (bytes_read > 0)
      {
        buffer[bytes_read] = '\0';
        *output_content = std::string(buffer, bytes_read);
      }
      close(fd);
    } });
}
