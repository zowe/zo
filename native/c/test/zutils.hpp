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

#ifndef ZUTILS_HPP
#define ZUTILS_HPP
#include <string>
#include <vector>
#include <thread>
#include <sys/stat.h>
#include "../zds.hpp"

const std::string zo_command = "./../build-out/zo";
const std::string zoa_command = "./../build-out/zoa";

int execute_command_with_input(const std::string &command, const std::string &input, bool suppress_output = true);
int execute_command_with_output(const std::string &command, std::string &output);
int execute_su_command_with_output(const std::string &command, std::string &output);
std::string get_random_string(const int length = 7, const bool allNumbers = true);
std::string get_random_uss(const std::string &base_dir);
std::string get_random_ds(const int qualifier_count = 4, const std::string &hlq = "");
std::string get_user();
std::string get_basename(const std::string &fspath);
std::string parse_etag_from_output(const std::string &output);
std::vector<std::string> parse_rfc_response(const std::string input, const char *delim = ",");
// Wait for a job to be visible in JES (returns true if found, false if timeout)
bool wait_for_job(const std::string &jobid, int max_retries = 30, int delay_ms = 100);

/**
 * @brief RAII class to manage FILE* pointers
 *
 * Opens a file on construction and automatically closes it on destruction.
 * Provides implicit conversion to FILE* for easy use with C file APIs.
 */
class TestFileGuard
{
  FILE *fp;
  std::string _file;

public:
  TestFileGuard(const char *_filename, const char &mode = 'w', const char *_link = nullptr);
  ~TestFileGuard();

  TestFileGuard(const TestFileGuard &) = delete;
  TestFileGuard &operator=(const TestFileGuard &) = delete;
  TestFileGuard(TestFileGuard &&) = delete;
  TestFileGuard &operator=(TestFileGuard &&) = delete;

  void reset(const char *_filename);

  operator FILE *() const;
  operator bool() const;
};

/**
 * @brief RAII class to manage directory pointers
 *
 * Creates a directory on construction and automatically deletes it on destruction.
 */
class TestDirGuard
{
  std::string _dir;

public:
  TestDirGuard(const char *_dirname, const mode_t mode = 0755);
  ~TestDirGuard();

  TestDirGuard(const TestDirGuard &) = delete;
  TestDirGuard &operator=(const TestDirGuard &) = delete;
  TestDirGuard(TestDirGuard &&) = delete;
  TestDirGuard &operator=(TestDirGuard &&) = delete;

  void reset(const char *_dirname);

  operator std::string() const;
};

class TestVsamGuard
{
  std::string m_dsn;

public:
  explicit TestVsamGuard(const std::string &organization = "NONINDEXED", bool retained = false);
  ~TestVsamGuard();
  TestVsamGuard(const TestVsamGuard &) = delete;
  TestVsamGuard &operator=(const TestVsamGuard &) = delete;

  const std::string &name() const;
};

// Data set creation helpers - convenience wrappers around zds_* functions
// that use sensible defaults for test data sets
void create_dsn_with_attrs(ZDS *zds, const std::string &dsn, const DS_ATTRIBUTES &attrs, const std::string &type_name);
void create_pds(ZDS *zds, const std::string &dsn);
void create_pdse(ZDS *zds, const std::string &dsn);
void create_seq(ZDS *zds, const std::string &dsn);
void write_to_dsn(const std::string &dsn, const std::string &data);

// Named pipe (streaming) test helpers
std::thread start_pipe_writer_thread(const std::string &pipe_path, const std::string &data_to_write);
std::thread start_pipe_reader_thread(const std::string &pipe_path, std::string *output_content);

#endif
