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
#include <fcntl.h>
#include <fstream>
#include <limits.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "ztest.hpp"
#include "../zlogger.hpp"

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
  return stat(path.c_str(), &buffer) == 0 ? static_cast<long>(buffer.st_size) : -1;
}

std::string read_file_contents(const std::string &path)
{
  std::ifstream file(path);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void write_file_contents(const std::string &path, const std::string &contents)
{
  std::ofstream file(path);
  file << contents;
  file.close();
  if (!file)
  {
    throw std::runtime_error("Cannot seed logger test file: " + path);
  }
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
        logs_dir_env("ZO_LOGS_DIR"), level_env("ZO_LOG_LEVEL"), home_env("HOME")
  {
    static unsigned int sequence = 0;
    root = "/tmp/zo-server-logger-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
    if (mkdir(root.c_str(), 0700) != 0)
    {
      throw std::runtime_error("Cannot create isolated server logger test directory");
    }
    dir = root + "/logs";
    logger.shutdown();
    setenv("ZO_LOGS_DIR", dir.c_str(), 1);
    unsetenv("ZO_LOG_LEVEL");
  }

  ~ScopedLogger()
  {
    logger.shutdown();
    remove_test_tree(root);
    logs_dir_env.restore();
    level_env.restore();
    home_env.restore();
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

  void initialize(bool server)
  {
    if (server)
    {
      logger.initialize_server();
    }
    else
    {
      logger.initialize();
    }
  }

  std::string log_path(bool server = true) const
  {
    return dir + (server ? "/zowex_server.log" : "/zowex.log");
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
  ScopedEnvironment home_env;
};

class ScopedStderrSuppressor
{
public:
  ScopedStderrSuppressor()
  {
    std::fflush(stderr);
    saved_fd = dup(STDERR_FILENO);
    if (saved_fd == -1)
    {
      throw std::runtime_error("Cannot preserve stderr for logger failure test");
    }
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
    dup2(saved_fd, STDERR_FILENO);
    close(saved_fd);
  }

private:
  int saved_fd;
};

void rotation_boundary_tests(bool server)
{
  it("rotates only after accepted output exceeds the exact 100KiB boundary", [server]()
  {
    ScopedLogger scope;
    Expect(mkdir(scope.dir.c_str(), 0700)).ToBe(0);
    const std::string path = scope.log_path(server);
    const std::string seed(100 * 1024 - 1, 'x');
    write_file_contents(path, seed + "\n");
    scope.initialize(server);
    LOG_DEBUG("filtered boundary record");
    Expect(file_size(path)).ToBe(100L * 1024L);
    Expect(file_exists(path + ".1")).ToBe(false);

    LOG_INFO("crossed boundary record");
    ZLOG_INFO("new active record");
    scope.logger.shutdown();
    Expect(file_exists(path + ".1")).ToBe(true);
    const std::string archived = read_file_contents(path + ".1");
    Expect(archived).ToContain(seed);
    Expect(archived).ToContain("crossed boundary record");
    Expect(archived).Not().ToContain("filtered boundary record");
    Expect(read_file_contents(path)).ToContain("new active record");
  });
}

void rotation_retention_tests(bool server)
{

  it("archives output exceeding 100KiB and retains the first and latest records", [server]()
  {
    ScopedLogger scope;
    scope.initialize(server);
    const std::string padding(1024, 'x');
    for (int i = 0; i < 150; ++i)
    {
      LOG_ERROR("rotation record #%d: %s", i, padding.c_str());
    }
    scope.logger.shutdown();

    const std::string path = scope.log_path(server);
    Expect(file_exists(path)).ToBe(true);
    Expect(file_exists(path + ".1")).ToBe(true);
    Expect(file_size(path)).ToBeLessThan(100L * 1024L);
    Expect(file_size(path + ".1")).ToBeGreaterThan(100L * 1024L);
    Expect(file_size(path + ".1")).ToBeLessThan(110L * 1024L);
    Expect(read_file_contents(path + ".1")).ToContain("rotation record #0:");
    Expect(read_file_contents(path)).ToContain("rotation record #149:");

    struct stat permissions;
    Expect(stat(path.c_str(), &permissions)).ToBe(0);
    Expect(static_cast<int>(permissions.st_mode & 0777)).ToBe(0600);
    Expect(stat((path + ".1").c_str(), &permissions)).ToBe(0);
    Expect(static_cast<int>(permissions.st_mode & 0777)).ToBe(0600);
  });

  it("keeps only active plus nine FIFO backups and evicts the oldest records", [server]()
  {
    ScopedLogger scope;
    scope.initialize(server);
    const std::string padding(2048, 'x');
    const int message_count = 1500;
    for (int i = 0; i < message_count; ++i)
    {
      ZLOG_ERROR("fifo record #%d: %s", i, padding.c_str());
    }
    scope.logger.shutdown();

    const std::string path = scope.log_path(server);
    Expect(file_exists(path)).ToBe(true);
    Expect(file_size(path)).ToBeLessThan(110L * 1024L);
    std::string all_contents = read_file_contents(path);
    Expect(all_contents).ToContain("fifo record #1499:");
    int newer_first = message_count;
    for (int generation = 1; generation <= 9; ++generation)
    {
      const std::string backup = path + "." + std::to_string(generation);
      Expect(file_exists(backup)).ToBe(true);
      Expect(file_size(backup)).ToBeGreaterThan(100L * 1024L);
      Expect(file_size(backup)).ToBeLessThan(110L * 1024L);
      const std::string contents = read_file_contents(backup);
      const std::string marker = "fifo record #";
      const size_t first = contents.find(marker);
      const size_t last = contents.rfind(marker);
      Expect(first != std::string::npos).ToBe(true);
      Expect(last != std::string::npos).ToBe(true);
      const int first_record = std::stoi(contents.substr(first + marker.size()));
      const int last_record = std::stoi(contents.substr(last + marker.size()));
      Expect(last_record).ToBeLessThan(newer_first);
      newer_first = first_record;
      all_contents += contents;
    }
    Expect(file_exists(path + ".10")).ToBe(false);
    Expect(all_contents).Not().ToContain("fifo record #0:");
  });
}

void rotation_recovery_tests(bool server)
{

  it("preserves the oversized active log on archive failure and recovers when unblocked", [server]()
  {
    ScopedLogger scope;
    scope.initialize(server);
    const std::string path = scope.log_path(server);
    // Nonempty directories prevent backup shifting and active-file renaming,
    // including when tests run with privileges that bypass chmod restrictions.
    for (int generation = 1; generation <= 9; ++generation)
    {
      const std::string blocker = path + "." + std::to_string(generation);
      Expect(mkdir(blocker.c_str(), 0700)).ToBe(0);
      write_file_contents(blocker + "/keep", "archive blocker\n");
    }
    {
      ScopedStderrSuppressor suppress_stderr;
      const std::string padding(1024, 'x');
      for (int i = 0; i < 150; ++i)
      {
        LOG_ERROR("preserved record #%d: %s", i, padding.c_str());
      }
      scope.logger.shutdown();
    }

    Expect(file_size(path)).ToBeGreaterThan(100L * 1024L);
    const std::string contents = read_file_contents(path);
    Expect(contents).ToContain("preserved record #0:");
    Expect(contents).ToContain("preserved record #149:");
    for (int generation = 1; generation <= 9; ++generation)
    {
      const std::string blocker = path + "." + std::to_string(generation);
      Expect(read_file_contents(blocker + "/keep")).ToBe(std::string("archive blocker\n"));
      remove_test_tree(blocker);
    }

    scope.initialize(server);
    LOG_ERROR("archive recovery marker");
    ZLOG_ERROR("after archive recovery marker");
    scope.logger.shutdown();
    Expect(file_exists(path + ".1")).ToBe(true);
    const std::string archived = read_file_contents(path + ".1");
    Expect(archived).ToContain("preserved record #0:");
    Expect(archived).ToContain("preserved record #149:");
    Expect(read_file_contents(path) + archived).ToContain("archive recovery marker");
    Expect(read_file_contents(path)).ToContain("after archive recovery marker");
  });
}

void private_log_file_tests()
{
  it("creates private per-user log directories and command and server files", []()
  {
    ScopedLogger scope;
    scope.logger.initialize_server();
    LOG_INFO("private server output");
    scope.logger.initialize();
    ZLOG_INFO("private command output");
    scope.logger.shutdown();

    Expect(read_file_contents(scope.log_path())).ToContain("private server output");
    Expect(read_file_contents(scope.log_path(false))).ToContain("private command output");
    struct stat permissions;
    Expect(stat(scope.dir.c_str(), &permissions)).ToBe(0);
    Expect(static_cast<int>(permissions.st_mode & 0777)).ToBe(0700);
    Expect(stat(scope.log_path().c_str(), &permissions)).ToBe(0);
    Expect(static_cast<int>(permissions.st_mode & 0777)).ToBe(0600);
    Expect(stat(scope.log_path(false).c_str(), &permissions)).ToBe(0);
    Expect(static_cast<int>(permissions.st_mode & 0777)).ToBe(0600);
  });
}

void server_level_tests()
{

  it("uses INFO or verbose DEBUG defaults and gives nonempty environment levels precedence", []()
  {
    ScopedLogger scope;
    struct LevelCase
    {
      const char *tag;
      const char *environment;
      bool verbose;
      LogLevel threshold;
    };
    const LevelCase cases[] = {
        {"normal-default", nullptr, false, ZLOGLEVEL_INFO},
        {"verbose-default", nullptr, true, ZLOGLEVEL_DEBUG},
        {"empty-env", "", true, ZLOGLEVEL_DEBUG},
        {"error-env", "ERROR", true, ZLOGLEVEL_ERROR},
        {"trace-env", "tRaCe", false, ZLOGLEVEL_TRACE},
        {"off-env", "OFF", true, ZLOGLEVEL_OFF}};
    const LogLevel levels[] = {ZLOGLEVEL_TRACE, ZLOGLEVEL_DEBUG, ZLOGLEVEL_INFO,
                               ZLOGLEVEL_WARN, ZLOGLEVEL_ERROR, ZLOGLEVEL_FATAL};
    const char *names[] = {"trace", "debug", "info", "warn", "error", "fatal"};
    for (const LevelCase &test : cases)
    {
      if (test.environment)
      {
        setenv("ZO_LOG_LEVEL", test.environment, 1);
      }
      else
      {
        unsetenv("ZO_LOG_LEVEL");
      }
      scope.logger.initialize_server(test.verbose);
      LOG_TRACE("%s trace", test.tag);
      ZLOG_DEBUG("%s debug", test.tag);
      LOG_INFO("%s info", test.tag);
      ZLOG_WARN("%s warn", test.tag);
      LOG_ERROR("%s error", test.tag);
      ZLOG_FATAL("%s fatal", test.tag);
      scope.logger.shutdown();

      const std::string contents = read_file_contents(scope.log_path());
      for (int level = 0; level < 6; ++level)
      {
        const std::string marker = std::string(test.tag) + " " + names[level];
        const bool accepted = test.threshold != ZLOGLEVEL_OFF && levels[level] >= test.threshold;
        ExpectWithContext(contents.find(marker) != std::string::npos, marker).ToBe(accepted);
      }
    }
  });
}

void server_lifecycle_tests()
{

  it("appends by default and explicitly truncates only the active server file", []()
  {
    ScopedLogger scope;
    Expect(mkdir(scope.dir.c_str(), 0700)).ToBe(0);
    const std::string path = scope.log_path();
    write_file_contents(path, "existing active record\n");
    write_file_contents(path + ".1", "newest backup record\n");
    write_file_contents(path + ".9", "oldest backup record\n");
    write_file_contents(scope.log_path(false), "separate command record\n");

    scope.logger.initialize_server();
    ZLOG_INFO("appended server record");
    scope.logger.shutdown();
    const std::string appended = read_file_contents(path);
    Expect(appended).ToContain("existing active record");
    Expect(appended).ToContain("appended server record");

    scope.logger.initialize_server(false, true);
    LOG_INFO("fresh server record");
    scope.logger.shutdown();
    const std::string truncated = read_file_contents(path);
    Expect(truncated).ToContain("fresh server record");
    Expect(truncated).Not().ToContain("existing active record");
    Expect(truncated).Not().ToContain("appended server record");
    Expect(read_file_contents(path + ".1")).ToBe(std::string("newest backup record\n"));
    Expect(read_file_contents(path + ".9")).ToBe(std::string("oldest backup record\n"));
    Expect(read_file_contents(scope.log_path(false))).ToBe(std::string("separate command record\n"));
  });

  it("does not write to the old command sink after server initialization fails and can reinitialize", []()
  {
    ScopedLogger scope;
    scope.logger.initialize();
    ZLOG_INFO("command before failed server initialization");
    const std::string command_before = read_file_contents(scope.log_path(false));
    Expect(command_before).ToContain("command before failed server initialization");
    write_file_contents(scope.root + "/blocked", "not a directory\n");
    setenv("ZO_LOGS_DIR", (scope.root + "/blocked/child").c_str(), 1);
    {
      ScopedStderrSuppressor suppress_stderr;
      scope.logger.initialize_server();
      LOG_ERROR("failed server LOG record");
      ZLOG_ERROR("failed server ZLOG record");
    }
    Expect(read_file_contents(scope.log_path(false))).ToBe(command_before);
    Expect(file_exists(scope.log_path())).ToBe(false);

    setenv("ZO_LOGS_DIR", scope.dir.c_str(), 1);
    scope.logger.initialize_server();
    LOG_INFO("reinitialized server LOG record");
    ZLOG_INFO("reinitialized server ZLOG record");
    scope.logger.initialize();
    ZLOG_INFO("reinitialized command record");
    scope.logger.shutdown();

    const std::string server = read_file_contents(scope.log_path());
    const std::string command = read_file_contents(scope.log_path(false));
    Expect(server).ToContain("reinitialized server LOG record");
    Expect(server).ToContain("reinitialized server ZLOG record");
    Expect(server).Not().ToContain("failed server");
    Expect(command).ToContain("command before failed server initialization");
    Expect(command).ToContain("reinitialized command record");
    Expect(command).Not().ToContain("reinitialized server");
  });
}

void rotation_tests(bool server)
{
  describe(server ? "ZLogger server rotation" : "ZLogger command rotation", [server]() -> void
  {
    rotation_boundary_tests(server);
    rotation_retention_tests(server);
    rotation_recovery_tests(server);
  });
}

void log_directory_validation_tests()
{
  it("rejects parent traversal from either log environment variable before creating directories", []()
  {
    ScopedLogger scope;
    const std::string traversal = scope.root + "/nested/../escaped";
    setenv("ZO_LOGS_DIR", traversal.c_str(), 1);
    {
      ScopedStderrSuppressor suppress_stderr;
      scope.logger.initialize_server();
      LOG_ERROR("rejected override record");
      scope.logger.shutdown();
      unsetenv("ZO_LOGS_DIR");
      setenv("HOME", traversal.c_str(), 1);
      scope.logger.initialize();
      ZLOG_ERROR("rejected home record");
      scope.logger.shutdown();
    }
    Expect(file_exists(scope.root + "/nested")).ToBe(false);
    Expect(file_exists(scope.root + "/escaped")).ToBe(false);
  });

  it("accepts relative directories with spaces and trailing separators", []()
  {
    ScopedLogger scope;
    char previous_dir[PATH_MAX];
    Expect(getcwd(previous_dir, sizeof(previous_dir)) != nullptr).ToBe(true);
    Expect(chdir(scope.root.c_str())).ToBe(0);
    setenv("ZO_LOGS_DIR", "./relative logs///", 1);
    scope.logger.initialize_server();
    const int restored = chdir(previous_dir);
    LOG_INFO("relative directory record");
    scope.logger.shutdown();
    Expect(restored).ToBe(0);
    Expect(read_file_contents(scope.root + "/relative logs/zowex_server.log")).ToContain("relative directory record");
  });

  it("refuses log directories that another user can modify", []()
  {
    ScopedLogger scope;
    Expect(mkdir(scope.dir.c_str(), 0700)).ToBe(0);
    Expect(chmod(scope.dir.c_str(), 0777)).ToBe(0);
    {
      ScopedStderrSuppressor suppress_stderr;
      scope.logger.initialize_server();
      LOG_ERROR("untrusted directory record");
      scope.logger.shutdown();
    }
    Expect(file_exists(scope.log_path())).ToBe(false);
  });
}

void log_file_validation_tests()
{
  it("refuses symbolic and hard linked log files without truncating their targets", []()
  {
    ScopedLogger scope;
    Expect(mkdir(scope.dir.c_str(), 0700)).ToBe(0);
    const std::string target = scope.root + "/unrelated";
    const std::string path = scope.log_path();
    write_file_contents(target, "preserve unrelated data\n");
    for (bool symbolic : {true, false})
    {
      const int linked = symbolic ? symlink(target.c_str(), path.c_str()) : link(target.c_str(), path.c_str());
      Expect(linked).ToBe(0);
      {
        ScopedStderrSuppressor suppress_stderr;
        scope.logger.initialize_server(false, true);
        LOG_ERROR("must not reach unrelated file");
        scope.logger.shutdown();
      }
      Expect(read_file_contents(target)).ToBe(std::string("preserve unrelated data\n"));
      Expect(unlink(path.c_str())).ToBe(0);
    }
  });
}

} // namespace

void server_logger_tests()
{
  describe("ZLogger server lifecycle", []() -> void
  {
    private_log_file_tests();
    server_level_tests();
    server_lifecycle_tests();
  });
  describe("ZLogger path validation", []() -> void
  {
    log_directory_validation_tests();
    log_file_validation_tests();
  });

  rotation_tests(false);
  rotation_tests(true);
}
