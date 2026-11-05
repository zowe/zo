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

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "ztest.hpp"
#include "../server/logger.hpp"

using namespace ztst;

namespace
{

bool file_exists(const std::string &path)
{
  struct stat buffer;
  return stat(path.c_str(), &buffer) == 0;
}

long file_size(const std::string &path)
{
  struct stat buffer;
  if (stat(path.c_str(), &buffer) != 0)
  {
    return -1;
  }
  return static_cast<long>(buffer.st_size);
}

std::string read_file_contents(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    return "";
  }
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void cleanup_test_dir(const std::string &dir)
{
  const std::string log_path = dir + "/zowex_server.log";
  unlink(log_path.c_str());
  for (int i = 1; i <= 9; i++)
  {
    unlink((log_path + "." + std::to_string(i)).c_str());
  }
  rmdir(dir.c_str());
}

// Temporarily overrides ZO_LOGS_DIR for the duration of a test, restoring
// the previous value (set by ztest_runner.cpp) on destruction.
class ScopedLogsDirOverride
{
public:
  explicit ScopedLogsDirOverride(const std::string &dir)
  {
    const char *previous = std::getenv("ZO_LOGS_DIR");
    if (previous)
    {
      had_previous = true;
      previous_value = previous;
    }
    setenv("ZO_LOGS_DIR", dir.c_str(), 1);
  }

  ~ScopedLogsDirOverride()
  {
    if (had_previous)
    {
      setenv("ZO_LOGS_DIR", previous_value.c_str(), 1);
    }
    else
    {
      unsetenv("ZO_LOGS_DIR");
    }
  }

private:
  bool had_previous = false;
  std::string previous_value;
};

// Temporarily redirects fd 2 (stderr) to /dev/null, restoring the original
// fd on destruction. Used to keep intentionally-triggered error logging
// (e.g. simulated permission failures) out of the test runner's output.
class ScopedStderrSuppressor
{
public:
  ScopedStderrSuppressor()
  {
    std::fflush(stderr);
    saved_fd = dup(STDERR_FILENO);
    const int devnull_fd = open("/dev/null", O_WRONLY);
    if (devnull_fd != -1)
    {
      dup2(devnull_fd, STDERR_FILENO);
      close(devnull_fd);
    }
  }

  ~ScopedStderrSuppressor()
  {
    std::fflush(stderr);
    if (saved_fd != -1)
    {
      dup2(saved_fd, STDERR_FILENO);
      close(saved_fd);
    }
  }

private:
  int saved_fd = -1;
};

} // namespace

void server_logger_tests()
{
  describe("server::Logger ZO_LOGS_DIR tests", []() -> void
  {
    it("should write logs to the directory specified by ZO_LOGS_DIR", []()
    {
      const std::string test_dir = "logs/server_logger_env_test";
      const std::string log_path = test_dir + "/zowex_server.log";

      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(false, true);
      LOG_WARN("Message written for ZO_LOGS_DIR test");
      server::Logger::shutdown();

      Expect(file_exists(log_path)).ToBe(true);

      const std::string contents = read_file_contents(log_path);
      Expect(contents).ToContain("Message written for ZO_LOGS_DIR test");

      cleanup_test_dir(test_dir);
    });

    it("should prefer an explicit log directory over ZO_LOGS_DIR", []()
    {
      const std::string test_dir = "logs/server logger explicit test";
      const std::string env_dir = test_dir + "/unused";
      const std::string log_path = test_dir + "/nested/zowex_server.log";
      ScopedLogsDirOverride override_dir(env_dir);

      server::Logger::init_logger(false, true, test_dir + "/nested///");
      LOG_WARN("Explicit log directory test");
      server::Logger::shutdown();

      Expect(read_file_contents(log_path)).ToContain("Explicit log directory test");
      Expect(file_exists(env_dir)).ToBe(false);
      struct stat st;
      Expect(stat(log_path.c_str(), &st)).ToBe(0);
      Expect(st.st_mode & 0777).ToBe(0600);

      cleanup_test_dir(test_dir + "/nested");
      rmdir(test_dir.c_str());
    });
  });

  describe("server::Logger verbosity tests", []() -> void
  {
    it("should only log warnings and errors by default", []()
    {
      const std::string test_dir = "logs/server_logger_quiet_test";
      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(false, true);
      LOG_DEBUG("Debug message");
      LOG_INFO("Info message");
      LOG_WARN("Warning message");
      LOG_ERROR("Error message");
      server::Logger::shutdown();

      const std::string contents = read_file_contents(test_dir + "/zowex_server.log");
      Expect(contents).Not().ToContain("[DEBUG]");
      Expect(contents).Not().ToContain("[INFO]");
      Expect(contents).ToContain("[WARN] Warning message");
      Expect(contents).ToContain("[ERROR] Error message");
      cleanup_test_dir(test_dir);
    });

    it("should include info and debug messages when verbose", []()
    {
      const std::string test_dir = "logs/server_logger_verbose_test";
      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(true, true);
      LOG_DEBUG("Debug message");
      LOG_INFO("Info message");
      server::Logger::shutdown();

      const std::string contents = read_file_contents(test_dir + "/zowex_server.log");
      Expect(contents).ToContain("[DEBUG] Debug message");
      Expect(contents).ToContain("[INFO] Info message");
      cleanup_test_dir(test_dir);
    });
  });

  describe("server::Logger log rolling tests", []() -> void
  {
    it("should roll the active log file once it exceeds 100KB", []()
    {
      const std::string test_dir = "logs/server_logger_rolling_test";
      const std::string log_path = test_dir + "/zowex_server.log";

      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(false, true);

      // Each message is padded so a modest number of writes pushes the
      // log file past the 100KB-per-file rotation threshold.
      const std::string padding(1024, 'x');
      for (int i = 0; i < 150; i++)
      {
        LOG_ERROR("Simulated failure #%d: %s", i, padding.c_str());
      }

      server::Logger::shutdown();

      // The active file should have rolled at least once, leaving it
      // small, with the overflow preserved in the first backup slot.
      Expect(file_exists(log_path)).ToBe(true);
      Expect(file_size(log_path)).ToBeLessThan(100L * 1024L);
      Expect(file_exists(log_path + ".1")).ToBe(true);
      Expect(read_file_contents(log_path)).Not().ToContain("[INFO]");

      const std::string backup_contents = read_file_contents(log_path + ".1");
      Expect(backup_contents).ToContain("Simulated failure #0:");

      cleanup_test_dir(test_dir);
    });

    it("should retain at most 10 generations via FIFO rotation, dropping the oldest", []()
    {
      const std::string test_dir = "logs/server_logger_fifo_test";
      const std::string log_path = test_dir + "/zowex_server.log";

      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(false, true);

      // Write far more than the full 10-file/100KB-per-file budget (~1MB)
      // so several rotations occur, forcing the oldest backups to be
      // evicted rather than accumulating indefinitely.
      const std::string padding(2048, 'x');
      const int message_count = 1500;
      for (int i = 0; i < message_count; i++)
      {
        LOG_ERROR("Simulated failure #%d: %s", i, padding.c_str());
      }

      server::Logger::shutdown();

      // Active file plus backups .1 through .9 should all exist and stay
      // bounded (never allowed to grow past a single generation's worth)
      Expect(file_exists(log_path)).ToBe(true);
      Expect(file_size(log_path)).ToBeLessThan(150L * 1024L);
      for (int i = 1; i <= 9; i++)
      {
        const std::string backup_path = log_path + "." + std::to_string(i);
        Expect(file_exists(backup_path)).ToBe(true);
        Expect(file_size(backup_path)).ToBeLessThan(150L * 1024L);
      }

      // No 10th backup should ever be created - FIFO caps total files at 10
      Expect(file_exists(log_path + ".10")).ToBe(false);

      // The very first messages should have been evicted from every
      // retained file, while the most recent message should survive
      std::string all_contents = read_file_contents(log_path);
      for (int i = 1; i <= 9; i++)
      {
        all_contents += read_file_contents(log_path + "." + std::to_string(i));
      }

      Expect(all_contents).Not().ToContain("Simulated failure #0:");
      char last_message[64];
      snprintf(last_message, sizeof(last_message), "Simulated failure #%d:", message_count - 1);
      Expect(all_contents).ToContain(std::string(last_message));

      cleanup_test_dir(test_dir);
    });
  });

  describe("server::Logger rotation failure handling", []() -> void
  {
    it("should preserve the active log if archiving it during rotation fails", []()
    {
      const std::string test_dir = "logs/server_logger_rotate_failure_test";
      const std::string log_path = test_dir + "/zowex_server.log";

      ScopedLogsDirOverride override_dir(test_dir);

      server::Logger::init_logger(false, true);

      // Revoking write on the directory makes rename() fail
      // while still allowing reads/opens on the existing log file
      chmod(test_dir.c_str(), 0500);

      {
        // The rotation failure below is expected and intentionally
        // triggered; suppress its stderr output so it doesn't get
        // mixed into the test runner's output.
        ScopedStderrSuppressor suppress_stderr;

        const std::string padding(1024, 'x');
        for (int i = 0; i < 150; i++)
        {
          LOG_ERROR("Simulated failure #%d: %s", i, padding.c_str());
        }

        server::Logger::shutdown();
      }

      // Restore write permission so cleanup can remove the directory
      chmod(test_dir.c_str(), 0700);

      // The active log must survive intact rather than being wiped
      // out just because the archival step failed.
      Expect(file_exists(log_path)).ToBe(true);
      const std::string contents = read_file_contents(log_path);
      Expect(contents).ToContain("Simulated failure #0:");
      Expect(contents).ToContain("Simulated failure #149:");

      unlink(log_path.c_str());
      rmdir(test_dir.c_str());
    });
  });
}
