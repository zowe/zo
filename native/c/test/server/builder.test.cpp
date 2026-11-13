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

#include "builder.test.hpp"
#include "../ztest.hpp"
#include "../../server/builder.hpp"
#include "../../server/dispatcher.hpp"
#include "../../server/rpcio.hpp"
#include "../../zlog_util.hpp"
#include "../../zlogger.hpp"

#include <cstdlib>
#include <fstream>
#include <limits.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace ztst;

namespace
{
struct FifoFaults
{
  int unlink_error = 0;
  int mkfifo_error = 0;
  int unlink_calls = 0;
  int mkfifo_calls = 0;
};

FifoFaults fifo_faults;

class ScopedFifoFaults
{
public:
  ScopedFifoFaults()
  {
    fifo_faults = {};
  }
  ~ScopedFifoFaults()
  {
    fifo_faults = {};
  }
};

int test_fifo_unlink(const char *path)
{
  ++fifo_faults.unlink_calls;
  if (fifo_faults.unlink_error)
  {
    errno = fifo_faults.unlink_error;
    return -1;
  }
  return ::unlink(path);
}

int test_fifo_mkfifo(const char *path, mode_t mode)
{
  ++fifo_faults.mkfifo_calls;
  if (fifo_faults.mkfifo_error)
  {
    errno = fifo_faults.mkfifo_error;
    return -1;
  }
  return ::mkfifo(path, mode);
}

} // namespace

// Keep the fault injection inside the temp-file and builder implementations.
#define unlink test_fifo_unlink
#define mkfifo test_fifo_mkfifo
#include "../../ztemp.hpp"
#undef mkfifo
#undef unlink

namespace
{
int finish_with_cleanup_failure(plugin::InvocationContext &context)
{
  fifo_faults.unlink_error = EPERM;
  const int result = static_cast<int>(context.get<long long>("handler-result", 0LL));
  context.output_stream() << "handler ran";
  if (result != 0)
  {
    context.errln("original handler error");
  }
  return result;
}

int throw_from_handler(plugin::InvocationContext &context)
{
  context.output_stream() << "handler ran";
  throw std::runtime_error("handler exception");
}

int record_handler_call(plugin::InvocationContext &context)
{
  context.output_stream() << "handler ran";
  return 0;
}

class TestFifoDirectory
{
public:
  TestFifoDirectory()
  {
    const char *previous = std::getenv("HOME");
    had_previous = previous != nullptr;
    if (previous)
    {
      previous_home = previous;
    }
    char resolved_home[PATH_MAX];
    const std::string home = zlog_util::get_home_dir();
    if (home.empty() || realpath(home.c_str(), resolved_home) == nullptr)
    {
      throw std::runtime_error("Failed to resolve home for transform tests");
    }
    static unsigned int sequence = 0;
    path = std::string(resolved_home) + "/.zowex_transform_" + std::to_string(getpid()) + "_" + std::to_string(++sequence);
    if (mkdir(path.c_str(), 0700) != 0)
    {
      throw std::runtime_error("Failed to create transform test home");
    }
    zowex_dir = path + "/.zowex";
    fifo_dir = zowex_dir + "/temp";
    fifo_path = fifo_dir + "/zowex_" + std::to_string(geteuid()) + "_" + std::to_string(getpid()) + "_1_fifo";
    setenv("HOME", path.c_str(), 1);
  }

  ~TestFifoDirectory()
  {
    if (had_previous)
    {
      setenv("HOME", previous_home.c_str(), 1);
    }
    else
    {
      unsetenv("HOME");
    }
    chmod(path.c_str(), 0700);
    struct stat info{};
    if (lstat(zowex_dir.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
    {
      chmod(zowex_dir.c_str(), 0700);
      if (lstat(fifo_dir.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
      {
        chmod(fifo_dir.c_str(), 0700);
        unlink(fifo_path.c_str());
        rmdir(fifo_dir.c_str());
      }
      else
      {
        unlink(fifo_dir.c_str());
      }
      rmdir(zowex_dir.c_str());
    }
    else
    {
      unlink(zowex_dir.c_str());
    }
    rmdir((path + "/target").c_str());
    rmdir(path.c_str());
  }

  std::string path;
  std::string zowex_dir;
  std::string fifo_dir;
  std::string fifo_path;

private:
  bool had_previous;
  std::string previous_home;
};

void expect_transform_failure(const CommandBuilder &builder, const plugin::ArgumentMap &args, const std::string &message)
{
  MiddlewareContext direct_context("transform-test", args);
  std::string exception_message;
  try
  {
    builder.apply_input_transforms(direct_context);
  }
  catch (const std::exception &e)
  {
    exception_message = e.what();
  }
  Expect(exception_message).ToContain(message);

  auto &dispatcher = CommandDispatcher::get_instance();
  const std::string command = "__test_input_transform_failure";
  Expect(dispatcher.register_command(command, builder)).ToBe(true);
  MiddlewareContext context(command, args);
  const int rc = dispatcher.dispatch(command, context);
  dispatcher.unregister_command(command);

  Expect(rc).ToBe(static_cast<int>(RpcErrorCode::INTERNAL_ERROR));
  Expect(context.get_error_content()).ToContain(message);
  Expect(context.get_output_content()).ToBe(std::string());
  Expect(context.has("after-transform")).ToBe(false);
  Expect(context.has("pipe-path")).ToBe(false);
  Expect(context.get_object() == nullptr).ToBe(true);
}

CommandBuilder fifo_builder()
{
  return CommandBuilder(record_handler_call)
      .handle_fifo("stream", "pipe-path", FifoMode::PUT, true)
      .set_default("after-transform", true)
      .read_stdout("data");
}
} // namespace

void server_builder_tests()
{
  describe("server input transforms", []()
  {
    it("throws on malformed base64 and prevents handler execution", []()
    {
      auto builder = CommandBuilder(record_handler_call)
                         .write_stdin("data", true)
                         .set_default("after-transform", true)
                         .read_stdout("data");
      for (const char *data : {"!", "!!!!"})
      {
        expect_transform_failure(builder, {{"data", plugin::Argument(data)}}, "Failed to process WriteStdin transform: Invalid base64");
      }
    });

    it("throws on unlink EPERM and never creates a replacement FIFO or calls the handler", []()
    {
      TestFifoDirectory directory;
      Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
      Expect(mkdir(directory.fifo_dir.c_str(), 0700)).ToBe(0);
      Expect(mkfifo(directory.fifo_path.c_str(), 0600)).ToBe(0);
      ScopedFifoFaults faults;
      fifo_faults.unlink_error = EPERM;
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Security error: permission denied deleting existing FIFO");
      Expect(fifo_faults.unlink_calls).ToBe(2);
      Expect(fifo_faults.mkfifo_calls).ToBe(0);
      struct stat info{};
      Expect(lstat(directory.fifo_path.c_str(), &info)).ToBe(0);
      Expect(S_ISFIFO(info.st_mode)).ToBe(true);
    });

    it("throws on mkfifo failure and prevents handler execution", []()
    {
      TestFifoDirectory directory;
      ScopedFifoFaults faults;
      fifo_faults.mkfifo_error = EIO;
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Failed to create FIFO pipe");
      Expect(fifo_faults.mkfifo_calls).ToBe(2);
      struct stat info{};
      Expect(lstat(directory.fifo_path.c_str(), &info)).ToBe(-1);
    });

    it("rejects a regular file at the FIFO path without changing its contents", []()
    {
      TestFifoDirectory directory;
      Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
      Expect(mkdir(directory.fifo_dir.c_str(), 0700)).ToBe(0);
      {
        std::ofstream file(directory.fifo_path);
        file << "preserve me";
        Expect(file.good()).ToBe(true);
      }
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Security error: existing FIFO path");
      std::ifstream file(directory.fifo_path);
      std::string contents;
      std::getline(file, contents);
      Expect(contents).ToBe(std::string("preserve me"));
    });

    it("rejects a symlink at the FIFO path without removing it", []()
    {
      TestFifoDirectory directory;
      Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
      Expect(mkdir(directory.fifo_dir.c_str(), 0700)).ToBe(0);
      Expect(symlink("missing-target", directory.fifo_path.c_str())).ToBe(0);
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Security error: existing FIFO path");
      struct stat info{};
      Expect(lstat(directory.fifo_path.c_str(), &info)).ToBe(0);
      Expect(S_ISLNK(info.st_mode)).ToBe(true);
    });

    it("rejects non-private FIFO directories without executing the handler", []()
    {
      for (bool parent : {true, false})
      {
        for (mode_t mode : {0755, 0777})
        {
          TestFifoDirectory directory;
          Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
          Expect(mkdir(directory.fifo_dir.c_str(), 0700)).ToBe(0);
          const std::string &unsafe_dir = parent ? directory.zowex_dir : directory.fifo_dir;
          Expect(chmod(unsafe_dir.c_str(), mode)).ToBe(0);
          expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Temporary directory must be user-owned, non-symlink, and mode 0700");
        }
      }
    });

    it("rejects symlinked FIFO directories without writing through them", []()
    {
      for (bool parent : {true, false})
      {
        TestFifoDirectory directory;
        const std::string target = directory.path + "/target";
        Expect(mkdir(target.c_str(), 0700)).ToBe(0);
        if (!parent)
        {
          Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
        }
        const std::string &unsafe_dir = parent ? directory.zowex_dir : directory.fifo_dir;
        Expect(symlink(target.c_str(), unsafe_dir.c_str())).ToBe(0);
        expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Temporary directory must be user-owned, non-symlink, and mode 0700");
        struct stat info{};
        const int stat_rc = lstat(directory.fifo_path.c_str(), &info);
        const int stat_errno = errno;
        Expect(stat_rc).ToBe(-1);
        Expect(stat_errno).ToBe(ENOENT);
      }
    });

    it("rejects a group-writable home directory", []()
    {
      TestFifoDirectory directory;
      Expect(chmod(directory.path.c_str(), 0770)).ToBe(0);
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Temporary files require a user-owned home");
    });

    it("fails when the home directory cannot be resolved", []()
    {
      TestFifoDirectory directory;
      const std::string missing_home = directory.path + "/missing";
      setenv("HOME", missing_home.c_str(), 1);
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument(1LL)}}, "Cannot resolve the home directory for temporary files");
    });

    it("throws on a non-integer stream ID and prevents handler execution", []()
    {
      expect_transform_failure(fifo_builder(), {{"stream", plugin::Argument("1")}}, "RPC ID argument is not an integer");
    });

    it("throws on invalid flattened objects and prevents handler execution", []()
    {
      auto builder = CommandBuilder(record_handler_call)
                         .flatten_obj("attributes")
                         .set_default("after-transform", true)
                         .read_stdout("data");
      for (const char *attributes : {"{", "[]"})
      {
        expect_transform_failure(builder, {{"attributes", plugin::Argument(attributes)}}, "Failed to process FlattenObj transform");
      }
    });

    it("allows an intentionally empty inline write to reach the handler", []()
    {
      auto &dispatcher = CommandDispatcher::get_instance();
      const std::string command = "__test_empty_inline_write";
      auto builder = CommandBuilder(record_handler_call)
                         .write_stdin("data", true)
                         .handle_fifo("stream", "pipe-path", FifoMode::PUT, true);
      Expect(dispatcher.register_command(command, builder)).ToBe(true);
      MiddlewareContext context(command, {{"data", plugin::Argument("")}});
      const int rc = dispatcher.dispatch(command, context);
      dispatcher.unregister_command(command);
      Expect(rc).ToBe(0);
      Expect(context.get_output_content()).ToBe(std::string("handler ran"));
      Expect(context.get_error_content()).ToBe(std::string());
      Expect(context.get_input_stream().str()).ToBe(std::string());
      Expect(context.has("data")).ToBe(false);
      Expect(context.has("pipe-path")).ToBe(false);
    });

    it("creates a private FIFO and sets the handler's pipe path", []()
    {
      TestFifoDirectory directory;
      MiddlewareContext context("transform-test", {{"stream", plugin::Argument(1LL)}});
      fifo_builder().apply_input_transforms(context);
      Expect(context.get<std::string>("pipe-path", "")).ToBe(directory.fifo_path);
      struct stat info{};
      for (const auto &path : {directory.zowex_dir, directory.fifo_dir})
      {
        Expect(lstat(path.c_str(), &info)).ToBe(0);
        Expect(S_ISDIR(info.st_mode)).ToBe(true);
        Expect(info.st_uid).ToBe(geteuid());
        Expect(info.st_mode & 07777).ToBe(0700);
      }
      Expect(stat(directory.fifo_path.c_str(), &info)).ToBe(0);
      Expect(S_ISFIFO(info.st_mode)).ToBe(true);
      Expect(info.st_mode & 0777).ToBe(0600);
      Expect(context.has("after-transform")).ToBe(true);
    });

    it("reuses the private directory, replaces stale FIFOs, and cleans up after dispatch", []()
    {
      TestFifoDirectory directory;
      Expect(mkdir(directory.zowex_dir.c_str(), 0700)).ToBe(0);
      Expect(mkdir(directory.fifo_dir.c_str(), 0700)).ToBe(0);
      Expect(mkfifo(directory.fifo_path.c_str(), 0600)).ToBe(0);
      auto &dispatcher = CommandDispatcher::get_instance();
      const std::string command = "__test_private_fifo_cleanup";
      Expect(dispatcher.register_command(command, fifo_builder())).ToBe(true);
      MiddlewareContext context(command, {{"stream", plugin::Argument(1LL)}});
      const int rc = dispatcher.dispatch(command, context);
      dispatcher.unregister_command(command);
      Expect(rc).ToBe(0);
      Expect(context.get_output_content()).ToBe(std::string("handler ran"));
      struct stat info{};
      const int stat_rc = lstat(directory.fifo_path.c_str(), &info);
      const int stat_errno = errno;
      Expect(stat_rc).ToBe(-1);
      Expect(stat_errno).ToBe(ENOENT);
      Expect(lstat(directory.fifo_dir.c_str(), &info)).ToBe(0);
    });

    it("keeps the FIFO until request destruction when the handler throws", []()
    {
      TestFifoDirectory directory;
      auto &dispatcher = CommandDispatcher::get_instance();
      const std::string command = "__test_fifo_exception_cleanup";
      auto builder = CommandBuilder(throw_from_handler)
                         .handle_fifo("stream", "pipe-path", FifoMode::PUT, true);
      Expect(dispatcher.register_command(command, builder)).ToBe(true);
      struct stat info{};
      {
        MiddlewareContext context(command, {{"stream", plugin::Argument(1LL)}});
        const int rc = dispatcher.dispatch(command, context);
        Expect(rc).ToBe(static_cast<int>(RpcErrorCode::INTERNAL_ERROR));
        Expect(context.get_error_content()).ToContain("handler exception");
        Expect(context.get_output_content()).ToBe(std::string("handler ran"));
        Expect(lstat(directory.fifo_path.c_str(), &info)).ToBe(0);
      }
      dispatcher.unregister_command(command);
      const int rc = lstat(directory.fifo_path.c_str(), &info);
      const int saved_errno = errno;
      Expect(rc).ToBe(-1);
      Expect(saved_errno).ToBe(ENOENT);
    });

    it("creates distinct private temporary files and removes them on scope exit", []()
    {
      TestFifoDirectory directory;
      std::string first_path, second_path;
      {
        ztemp::FileGuard first;
        ztemp::FileGuard second;
        first_path = first.path();
        second_path = second.path();
        Expect(first_path).Not().ToBe(second_path);
        Expect(first_path.find(directory.fifo_dir + "/")).ToBe(size_t(0));
        struct stat info{};
        Expect(lstat(first_path.c_str(), &info)).ToBe(0);
        Expect(S_ISREG(info.st_mode)).ToBe(true);
        Expect(info.st_mode & 0777).ToBe(0600);
        std::ofstream file(first_path);
        file << "temporary contents";
        Expect(file.good()).ToBe(true);
      }
      struct stat info{};
      Expect(lstat(first_path.c_str(), &info)).ToBe(-1);
      Expect(lstat(second_path.c_str(), &info)).ToBe(-1);
    });

    it("removes temporary files during exception unwinding", []()
    {
      TestFifoDirectory directory;
      std::string path;
      try
      {
        ztemp::FileGuard file;
        path = file.path();
        throw std::runtime_error("test unwind");
      }
      catch (const std::runtime_error &e)
      {
        Expect(std::string(e.what())).ToBe(std::string("test unwind"));
      }
      Expect(path.empty()).ToBe(false);
      struct stat info{};
      const int rc = lstat(path.c_str(), &info);
      const int saved_errno = errno;
      Expect(rc).ToBe(-1);
      Expect(saved_errno).ToBe(ENOENT);
    });

    it("preserves successful and failed handler results when cleanup unlink returns EPERM", []()
    {
      for (long long handler_result : {0LL, 8LL})
      {
        TestFifoDirectory directory;
        ScopedFifoFaults faults;
        auto &dispatcher = CommandDispatcher::get_instance();
        const std::string command = "__test_fifo_cleanup_failure";
        auto builder = CommandBuilder(finish_with_cleanup_failure)
                           .handle_fifo("stream", "pipe-path", FifoMode::PUT, true);
        Expect(dispatcher.register_command(command, builder)).ToBe(true);
        MiddlewareContext context(command, {{"stream", plugin::Argument(1LL)}, {"handler-result", plugin::Argument(handler_result)}});
        const int rc = dispatcher.dispatch(command, context);
        dispatcher.unregister_command(command);
        Expect(rc).ToBe(static_cast<int>(handler_result));
        Expect(context.get_output_content()).ToBe(std::string("handler ran"));
        Expect(context.get_error_content()).ToBe(std::string(handler_result == 0 ? "" : "original handler error\n"));
        Expect(fifo_faults.unlink_calls).ToBe(1);
        struct stat info{};
        Expect(lstat(directory.fifo_path.c_str(), &info)).ToBe(0);
        Expect(S_ISFIFO(info.st_mode)).ToBe(true);
      }
    });
  });
}

// Substitute only these syscalls in the builder implementation, not in test setup or cleanup.
#define unlink test_fifo_unlink
#define mkfifo test_fifo_mkfifo
#include "../../server/builder.cpp"
#undef mkfifo
#undef unlink
