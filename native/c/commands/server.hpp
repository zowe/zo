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

#ifndef COMMANDS_SERVER_HPP
#define COMMANDS_SERVER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "../parser.hpp"

class WorkerPool;

namespace server
{

struct Options
{
  long long num_workers = 10;
  bool verbose = false;
  long long request_timeout = 60;
  std::string exec_dir = ".";
};

void register_commands(parser::Command &root_command);

} // namespace server

class ZServer
{
private:
  server::Options options;
  std::string exec_dir = ".";
  std::unique_ptr<WorkerPool> worker_pool;
  std::atomic<bool> shutdown_requested{false};
  std::once_flag shutdown_flag;
  // Joined during shutdown so it cannot outlive worker_pool
  std::thread worker_count_thread;

  static void signal_handler(int sig);
  void setup_signal_handlers();
  void request_shutdown();
  void print_ready_message();
  void log_worker_count();

  ZServer() = default;

public:
  ZServer(const ZServer &) = delete;
  ZServer &operator=(const ZServer &) = delete;
  ~ZServer();

  static ZServer &get_instance();
  void set_exec_dir(const std::string &dir)
  {
    exec_dir = dir;
  }
  const std::string &get_exec_dir() const
  {
    return exec_dir;
  }
  void run(const server::Options &opts);
};

#endif
