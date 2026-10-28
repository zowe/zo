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
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ztest.hpp"
#include "../zlogger.hpp"

using namespace ztst;

namespace
{

std::string read_file_contents(const std::string &path)
{
  std::ifstream file(path);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool file_exists(const std::string &path)
{
  struct stat buffer;
  return stat(path.c_str(), &buffer) == 0;
}

void remove_test_tree(const std::string &path)
{
  struct stat buffer;
  if (lstat(path.c_str(), &buffer) != 0)
  {
    return;
  }
  if (!S_ISDIR(buffer.st_mode))
  {
    unlink(path.c_str());
    return;
  }
  DIR *dir = opendir(path.c_str());
  if (dir)
  {
    while (struct dirent *entry = readdir(dir))
    {
      const std::string name(entry->d_name);
      if (name != "." && name != "..")
      {
        remove_test_tree(path + "/" + name);
      }
    }
    closedir(dir);
  }
  rmdir(path.c_str());
}

class ScopedEnvironment
{
public:
  explicit ScopedEnvironment(const char *name) : name(name)
  {
    const char *previous = std::getenv(name);
    had_previous = previous != nullptr;
    if (previous)
    {
      value = previous;
    }
  }

  void restore() const
  {
    if (had_previous)
    {
      setenv(name, value.c_str(), 1);
    }
    else
    {
      unsetenv(name);
    }
  }

private:
  const char *name;
  bool had_previous;
  std::string value;
};

class ScopedLogger
{
public:
  ScopedLogger()
      : logger(ZLogger::get_instance()), previous_level(logger.get_log_level()),
        previous_server(logger.is_server_mode()), previous_verbose(logger.is_verbose_logging()),
        logs_dir_env("ZO_LOGS_DIR"), level_env("ZO_LOG_LEVEL")
  {
    static unsigned int sequence = 0;
    root = "/tmp/zo-zlogger-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
    if (mkdir(root.c_str(), 0700) != 0)
    {
      throw std::runtime_error("Cannot create isolated logger test directory");
    }
    dir = root + "/logs";
    logger.shutdown();
    setenv("ZO_LOGS_DIR", dir.c_str(), 1);
    unsetenv("ZO_LOG_LEVEL");
    logger.initialize();
  }

  ~ScopedLogger()
  {
    logger.shutdown();
    remove_test_tree(root);
    logs_dir_env.restore();
    level_env.restore();
    if (previous_server)
    {
      logger.initialize_server(previous_verbose);
    }
    else
    {
      logger.initialize();
    }
    logger.set_log_level(static_cast<LogLevel>(previous_level));
  }

  std::string command_path() const
  {
    return dir + "/zowex.log";
  }
  std::string server_path() const
  {
    return dir + "/zowex_server.log";
  }

  ZLogger &logger;
  std::string root;
  std::string dir;

private:
  int previous_level;
  bool previous_server;
  bool previous_verbose;
  ScopedEnvironment logs_dir_env;
  ScopedEnvironment level_env;
};

void command_routing_tests()
{
  describe("ZLogger command logging", []() -> void
  {
    it("routes both macro families to the selected command or server file", []()
    {
      ScopedLogger scope;
      LOG_INFO("command LOG marker %d", 17);
      ZLOG_INFO("command ZLOG marker %s", "hello");

      scope.logger.initialize_server();
      LOG_INFO("server LOG marker");
      ZLOG_INFO("server ZLOG marker");

      scope.logger.initialize();
      LOG_WARN("returned command LOG marker");
      ZLOG_WARN("returned command ZLOG marker");
      scope.logger.shutdown();

      const std::string command = read_file_contents(scope.command_path());
      const std::string server = read_file_contents(scope.server_path());
      Expect(command).ToContain("command LOG marker 17");
      Expect(command).ToContain("command ZLOG marker hello");
      Expect(command).ToContain("returned command LOG marker");
      Expect(command).ToContain("returned command ZLOG marker");
      Expect(command).Not().ToContain("server LOG marker");
      Expect(command).Not().ToContain("server ZLOG marker");
      Expect(server).ToContain("server LOG marker");
      Expect(server).ToContain("server ZLOG marker");
      Expect(server).Not().ToContain("command");
    });

    it("filters command output by the environment and rereads it without truncating", []()
    {
      ScopedLogger scope;
      setenv("ZO_LOG_LEVEL", "wArNiNg", 1);
      scope.logger.initialize();
      LOG_DEBUG("filtered command debug");
      ZLOG_INFO("filtered command info");
      LOG_WARN("retained command warning");
      ZLOG_ERROR("retained command error");
      scope.logger.shutdown();

      setenv("ZO_LOG_LEVEL", "TRACE", 1);
      scope.logger.initialize();
      scope.logger.trace("retained command trace");
      scope.logger.debug("retained command debug");
      scope.logger.info("retained command info");
      scope.logger.warn("retained command warn");
      scope.logger.error("retained second command error");
      ZLOG_FATAL("retained nonterminating fatal %d", 42);
      scope.logger.log(ZLOGLEVEL_OFF, "filtered OFF record");
      scope.logger.shutdown();

      const std::string contents = read_file_contents(scope.command_path());
      Expect(contents).Not().ToContain("filtered");
      Expect(contents).ToContain("retained command warning");
      Expect(contents).ToContain("retained command error");
      Expect(contents).ToContain("retained command trace");
      Expect(contents).ToContain("retained command debug");
      Expect(contents).ToContain("retained command info");
      Expect(contents).ToContain("retained command warn");
      Expect(contents).ToContain("retained second command error");
      Expect(contents).ToContain("retained nonterminating fatal 42");
      Expect(file_exists(scope.server_path())).ToBe(false);
    });
  });
}

void command_level_and_format_tests()
{
  describe("ZLogger command levels and formatting", []() -> void
  {
    it("applies level changes to output and suppresses every severity at OFF", []()
    {
      ScopedLogger scope;
      LOG_DEBUG("filtered default command debug");
      ZLOG_INFO("retained default command info");
      scope.logger.set_log_level(ZLOGLEVEL_ERROR);
      LOG_WARN("filtered runtime warning");
      ZLOG_ERROR("retained runtime error");
      scope.logger.set_log_level(ZLOGLEVEL_OFF);
      LOG_TRACE("filtered OFF trace");
      ZLOG_DEBUG("filtered OFF debug");
      LOG_INFO("filtered OFF info");
      ZLOG_WARN("filtered OFF warning");
      LOG_ERROR("filtered OFF error");
      ZLOG_FATAL("filtered OFF fatal");
      scope.logger.shutdown();

      const std::string contents = read_file_contents(scope.command_path());
      Expect(contents).ToContain("retained default command info");
      Expect(contents).ToContain("retained runtime error");
      Expect(contents).Not().ToContain("filtered");
    });

    it("bounds oversized formatted records and preserves the following complete record", []()
    {
      ScopedLogger scope;
      const std::string oversized(12000, 'x');
      LOG_INFO("long-begin:%s:long-end", oversized.c_str());
      ZLOG_INFO("following record %d !@#$^&*()", 42);
      scope.logger.shutdown();

      const std::string contents = read_file_contents(scope.command_path());
      Expect(contents).ToContain("long-begin:" + std::string(512, 'x'));
      Expect(contents).Not().ToContain(":long-end");
      Expect(contents.size()).ToBeLessThan(static_cast<size_t>(8192));
      std::istringstream records(contents);
      std::string first;
      std::string second;
      std::string extra;
      Expect(static_cast<bool>(std::getline(records, first))).ToBe(true);
      Expect(static_cast<bool>(std::getline(records, second))).ToBe(true);
      Expect(second).ToContain("following record 42 !@#$^&*()");
      Expect(static_cast<bool>(std::getline(records, extra))).ToBe(false);
      Expect(contents.back()).ToBe('\n');
    });
  });
}

void concurrent_logging_tests()
{
  describe("ZLogger concurrent logging", []() -> void
  {
    it("writes concurrent formatted messages as complete unique records", []()
    {
      ScopedLogger scope;
      constexpr int thread_count = 4;
      constexpr int records_per_thread = 40;
      std::vector<std::thread> threads;
      for (int thread = 0; thread < thread_count; ++thread)
      {
        threads.emplace_back([thread]()
        {
          for (int record = 0; record < records_per_thread; ++record)
          {
            if (record % 2 == 0)
            {
              LOG_INFO("record:%d:%d:payload-complete", thread, record);
            }
            else
            {
              ZLOG_INFO("record:%d:%d:payload-complete", thread, record);
            }
          }
        });
      }
      for (auto &thread : threads)
      {
        thread.join();
      }
      scope.logger.shutdown();

      std::set<std::string> expected;
      for (int thread = 0; thread < thread_count; ++thread)
      {
        for (int record = 0; record < records_per_thread; ++record)
        {
          expected.insert("record:" + std::to_string(thread) + ":" + std::to_string(record) + ":payload-complete");
        }
      }
      std::istringstream records(read_file_contents(scope.command_path()));
      std::string line;
      int count = 0;
      while (std::getline(records, line))
      {
        const size_t marker = line.find("record:");
        Expect(marker != std::string::npos).ToBe(true);
        const std::string payload = line.substr(marker);
        Expect(expected.erase(payload)).ToBe(static_cast<size_t>(1));
        ++count;
      }
      Expect(count).ToBe(thread_count * records_per_thread);
      Expect(expected.empty()).ToBe(true);
    });
  });
}

void logger_shutdown_tests()
{
  describe("ZLogger shutdown", []() -> void
  {
    it("does not reopen after shutdown and resumes only through explicit initialization", []()
    {
      ScopedLogger scope;
      LOG_INFO("before shutdown marker");
      scope.logger.shutdown();
      const std::string before = read_file_contents(scope.command_path());
      LOG_ERROR("blocked command LOG marker");
      ZLOG_FATAL("blocked command ZLOG marker");
      Expect(read_file_contents(scope.command_path())).ToBe(before);

      setenv("ZO_LOGS_DIR", (scope.root + "/after-shutdown").c_str(), 1);
      ZLOG_INFO("blocked changed-directory marker");
      Expect(file_exists(scope.root + "/after-shutdown/zowex.log")).ToBe(false);
      setenv("ZO_LOGS_DIR", scope.dir.c_str(), 1);
      scope.logger.initialize_server();
      LOG_INFO("resumed server marker");
      scope.logger.shutdown();
      LOG_ERROR("blocked server marker");
      scope.logger.initialize();
      ZLOG_INFO("resumed command marker");
      scope.logger.shutdown();

      const std::string command = read_file_contents(scope.command_path());
      const std::string server = read_file_contents(scope.server_path());
      Expect(command).ToContain("before shutdown marker");
      Expect(command).ToContain("resumed command marker");
      Expect(command).Not().ToContain("blocked");
      Expect(server).ToContain("resumed server marker");
      Expect(server).Not().ToContain("blocked");
    });
  });
}

void fatal_message_tests()
{
  describe("ZLogger fatal messages", []() -> void
  {
    it("preserves percent sequences as text and exits with the requested status", []()
    {
      ScopedLogger scope;
      scope.logger.shutdown();
      std::string output;
      std::string errors;
      // A post-fork child of a threaded process must exec before using mutexes or stdio.
      const int status = execute_command("./build-out/ztest_runner --zlogger-fatal", output, errors);
      ExpectWithContext(status, errors).ToBe(7);
      Expect(read_file_contents(scope.command_path())).ToContain("literal %s %n 100%");
    });
  });
}

#if defined(ZLOG_ENABLE) && defined(__MVS__)
void metal_cleanup_tests()
{
  describe("ZLogger Metal cleanup", []()
  {
    it("filters cleanup diagnostics at the INFO boundary and at OFF", []()
    {
      for (LogLevel level : {ZLOGLEVEL_INFO, ZLOGLEVEL_WARN, ZLOGLEVEL_OFF})
      {
        ScopedLogger scope;
        scope.logger.set_log_level(level);
        Expect(ZLGGTLVL()).ToBe(static_cast<int>(level));
        scope.logger.shutdown();

        const std::string contents = read_file_contents(scope.dir + "/zowex_metal.log");
        if (level == ZLOGLEVEL_INFO)
        {
          Expect(contents).ToContain("Metal C logger cleanup");
        }
        else
        {
          Expect(contents).ToBe(std::string());
        }
      }
    });
  });
}
#endif

} // namespace

void zlogger_tests()
{
  command_routing_tests();
  command_level_and_format_tests();
  concurrent_logging_tests();
  logger_shutdown_tests();
  fatal_message_tests();
#if defined(ZLOG_ENABLE) && defined(__MVS__)
  metal_cleanup_tests();
#endif
}
