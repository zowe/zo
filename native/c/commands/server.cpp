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

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include "core.hpp"
#include "server.hpp"
#include "../zjson.hpp"
#include "../server/rpc_server.hpp"
#include "../server/rpc_commands.hpp"
#include "../server/dispatcher.hpp"
#include "../zlogger.hpp"
#include "../server/worker.hpp"
#include "../server/plugin_bridge.hpp"

using namespace parser;

namespace
{

// Set from the signal handler only. Everything the handler touches must be
// async-signal-safe, so the real shutdown work runs on the main thread.
volatile sig_atomic_t &signal_shutdown_flag()
{
  static volatile sig_atomic_t flag = 0;
  return flag;
}

} // namespace

struct StatusMessage
{
  std::string status;
  std::string message;
  std::optional<zjson::Value> data;
};
ZJSON_DERIVE(StatusMessage, status, message, data);

// ZServer implementation

ZServer::~ZServer()
{
  // std::thread's destructor terminates the process if the thread is still
  // joinable, so make teardown independent of atexit ordering.
  shutdown_requested = true;
  if (worker_count_thread.joinable())
  {
    worker_count_thread.join();
  }
}

ZServer &ZServer::get_instance()
{
  static ZServer instance;
  return instance;
}

void ZServer::signal_handler(int sig __attribute__((unused)))
{
  // Only async-signal-safe operations belong here. Closing stdin wakes the main
  // input loop, which then performs the real shutdown. Calling into the worker
  // pool or the logger from a signal context can self-deadlock on a mutex the
  // interrupted thread already holds, leaving the process alive forever.
  signal_shutdown_flag() = 1;
  close(STDIN_FILENO);
}

void ZServer::setup_signal_handlers()
{
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);
  signal(SIGABRT, signal_handler);
  signal(SIGTERM, signal_handler);
}

void ZServer::request_shutdown()
{
  std::call_once(shutdown_flag, [this]()
                 {
          shutdown_requested = true;
          if (worker_count_thread.joinable()) {
              worker_count_thread.join();
          }
          if (worker_pool) {
              worker_pool->shutdown();
          }
          // If signal_shutdown_flag is nonzero, the signal handler already closed
          // stdin; closing it again here could clobber a descriptor that has since
          // been reused for fd 0. Only close it here when shutdown came from a
          // non-signal path.
          if (signal_shutdown_flag() == 0) {
              close(STDIN_FILENO);
          } });
}

void ZServer::print_ready_message()
{
  zjson::Value data = zjson::Value::create_object();

  data.add_to_object("version", zjson::Value(core::get_version()));

  StatusMessage status_msg{
      .status = "ready",
      .message = "zo server is ready to accept input",
      .data = std::optional<zjson::Value>(data),
  };

  std::string json_string = RpcServer::serialize_json(zjson::to_value(status_msg).value());
  std::cout << json_string << std::endl;
}

void ZServer::log_worker_count()
{
  if (!ZLogger::get_instance().is_verbose_logging())
    return;

  worker_count_thread = std::thread([this]()
                                    {
          while (!shutdown_requested) {
              if (worker_pool) {
                  int32_t count = worker_pool->get_available_workers_count();
                  LOG_DEBUG("Available workers: %d/%lld", count, options.num_workers);
                  std::this_thread::sleep_for(std::chrono::milliseconds(500));
                  if (count == options.num_workers) {
                      break;
                  }
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
          } });
}

void ZServer::run(const server::Options &opts)
{
  options = opts;

  ZLogger::get_instance().initialize_server(options.verbose);
  LOG_INFO("Starting zo server with %lld workers and %lld seconds until request timeout (verbose=%s)", options.num_workers, options.request_timeout, options.verbose ? "true" : "false");

  setup_signal_handlers();

  CommandDispatcher &dispatcher = CommandDispatcher::get_instance();

  LOG_DEBUG("Registering command handlers");
  register_all_commands(dispatcher);
  plugin::register_commands_with_server(dispatcher);

  worker_pool.reset(new WorkerPool(options.num_workers, std::chrono::seconds(options.request_timeout)));

  std::atexit([]()
              { get_instance().request_shutdown(); });

  log_worker_count();
  print_ready_message();

  LOG_DEBUG("Entering main input processing loop");
  std::string line{};
  while (std::getline(std::cin, line))
  {
    if (signal_shutdown_flag() != 0 || shutdown_requested)
      break;

    if (!line.empty())
    {
      worker_pool->distribute_request(line);
    }
  }

  if (signal_shutdown_flag() != 0)
  {
    LOG_INFO("Shutdown signal received, shutting down");
  }
  else
  {
    LOG_INFO("Input stream closed, shutting down");
  }
  request_shutdown();

  ZLogger::get_instance().shutdown();
}

// server namespace implementation

namespace server
{

static int handle_server(plugin::InvocationContext &context)
{
  server::Options opts;
  opts.num_workers = context.get<long long>("num-workers", opts.num_workers);
  opts.verbose = context.get<bool>("verbose", opts.verbose);
  opts.request_timeout = context.get<long long>("request-timeout", opts.request_timeout);
  opts.exec_dir = ZServer::get_instance().get_exec_dir();

  const auto *num_workers_env = getenv("ZO_NUM_WORKERS");
  if (num_workers_env != nullptr)
  {
    try
    {
      std::size_t pos{};
      auto value = std::stoll(num_workers_env, &pos);
      if (pos != std::string(num_workers_env).size())
      {
        context.error_stream() << "Invalid value for num workers: '" << num_workers_env << "'" << std::endl;
        return 1;
      }
      opts.num_workers = value;
    }
    catch (const std::invalid_argument &)
    {
      context.error_stream() << "Invalid value for num workers: '" << num_workers_env << "'" << std::endl;
      return 1;
    }
    catch (const std::out_of_range &)
    {
      context.error_stream() << "Value out of range for num workers: '" << num_workers_env << "'" << std::endl;
      return 1;
    }
  }

  if (opts.num_workers <= 0)
  {
    context.error_stream() << "Number of workers must be greater than 0" << std::endl;
    return 1;
  }

  if (opts.request_timeout <= 0)
  {
    context.error_stream() << "Request timeout must be greater than 0 seconds" << std::endl;
    return 1;
  }

  try
  {
    ZServer::get_instance().run(opts);
  }
  catch (const std::exception &e)
  {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    LOG_FATAL("Fatal error: %s", e.what());
    return 1;
  }

  return 0;
}

void register_commands(Command &root_command)
{
  auto server_cmd = std::make_shared<Command>("server", "start the Zowe Remote SSH I/O server");
  server_cmd->disable_json_capture();
  server_cmd->add_keyword_arg("num-workers",
                              make_aliases("-w", "--num-workers"),
                              "number of worker threads",
                              ArgType_Single, false,
                              ArgValue(10LL));
  server_cmd->add_keyword_arg("verbose",
                              make_aliases("-v", "--verbose"),
                              "enable verbose logging",
                              ArgType_Flag, false,
                              ArgValue(false));
  server_cmd->add_keyword_arg("request-timeout",
                              make_aliases("-t", "--request-timeout"),
                              "request timeout in seconds before a worker is restarted",
                              ArgType_Single, false,
                              ArgValue(60LL));
  server_cmd->set_handler(handle_server);
  root_command.add_command(server_cmd);
}

} // namespace server
