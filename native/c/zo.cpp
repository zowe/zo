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

#pragma runopts("TRAP(ON,NOSPIE)")

#define _UNIX03_SOURCE
#include <dirent.h>
#include <iostream>
#include <string>
#include "commands/core.hpp"
#include "commands/ds.hpp"
#include "commands/job.hpp"
#include "commands/server.hpp"
#include "commands/system.hpp"
#include "commands/tool.hpp"
#include "commands/tso.hpp"
#include "commands/uss.hpp"
#include "extend/plugin.hpp"
#include "zlogger.hpp"

// Version information
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

static std::string get_executable_dir(const char *argv0)
{
  std::string full_path(argv0);
  size_t last_slash = full_path.find_last_of('/');
  if (last_slash != std::string::npos)
    return full_path.substr(0, last_slash);
  return ".";
}

// Plugin loading is opt-in: ZO_PLUGINS_DIR must be explicitly set
static bool get_plugins_dir(std::string &plugins_path)
{
  const char *env_value = getenv("ZO_PLUGINS_DIR");
  if (env_value == nullptr || *env_value == '\0')
  {
    ZLOG_DEBUG("ZO_PLUGINS_DIR not set; skipping plug-in loading");
    return false;
  }

  plugins_path = env_value;
  if (plugins_path.back() == '/')
  {
    plugins_path.pop_back(); // Remove trailing slash if present
  }

  return true;
}

int main(int argc, char *argv[])
{
  const auto exec_dir = get_executable_dir(argv[0]);
  ZServer::get_instance().set_exec_dir(exec_dir);

  try
  {
    auto &root_cmd = core::setup_root_command(argv);
    core::set_version(PACKAGE_VERSION);
    core::set_program_name("zo");

    plugin::PluginManager pm;
    core::set_plugin_manager(&pm);
    std::string plugins_dir;
    if (get_plugins_dir(plugins_dir))
    {
      pm.load_plugins(plugins_dir);
    }

    ds::register_commands(root_cmd);
    job::register_commands(root_cmd);
    server::register_commands(root_cmd);
    sys::register_commands(root_cmd);
    tool::register_commands(root_cmd);
    tso::register_commands(root_cmd);
    uss::register_commands(root_cmd);

    pm.register_commands(root_cmd);

    return core::execute_command(argc, argv);
  }
  catch (const std::exception &e)
  {
    std::cerr << "Fatal error encountered in zo: " << e.what() << std::endl;
    return 1;
  }
}
