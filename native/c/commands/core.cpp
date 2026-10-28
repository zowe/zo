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

#include "core.hpp"
#include "../extend/plugin.hpp"
#include "../zmetal.h"
#include "../zutm.h"
#include <dirent.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

using namespace parser;

#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

namespace core
{
std::shared_ptr<ArgumentParser> g_arg_parser;
namespace
{
std::string g_version("unknown");
plugin::PluginManager *g_plugin_manager = nullptr;

std::string &program_name_ref()
{
  static std::string program_name("zo");
  return program_name;
}
} // namespace

void set_version(const std::string &version)
{
  g_version = version;
}

void set_program_name(const std::string &name)
{
  program_name_ref() = name;
}

const std::string &get_version()
{
  return g_version;
}

void disable_json_output()
{
  if (g_arg_parser)
  {
    g_arg_parser->disable_json_output();
  }
}

void set_plugin_manager(plugin::PluginManager *manager)
{
  g_plugin_manager = manager;
}

plugin::PluginManager *get_plugin_manager()
{
  return g_plugin_manager;
}

bool should_quit(const std::string &input)
{
  return (input == "quit" || input == "exit" ||
          input == "QUIT" || input == "EXIT");
}

int interactive_mode(const plugin::InvocationContext &context)
{
  context.output_stream() << "Started, enter command or 'quit' to quit..." << std::endl;

  std::string command;
  int rc = 0;
  int is_tty = isatty(fileno(stdin));

  do
  {
    if (is_tty)
      context.output_stream() << "\r> " << std::flush;

    std::getline(context.input_stream(), command);

    if (should_quit(command))
      break;

    // Parse and execute the command
    ParseResult result = g_arg_parser->parse(command);
    rc = result.exit_code;

    if (!is_tty)
    {
      context.output_stream() << "[" << rc << "]" << std::endl;
      // EBCDIC \x37 = ASCII \x04 = End of Transmission (Ctrl+D)
      context.output_stream() << '\x37' << std::flush;
      context.error_stream() << '\x37' << std::flush;
    }

  } while (!should_quit(command));

  context.output_stream() << "...terminated" << std::endl;

  return rc;
}

int handle_version(plugin::InvocationContext &context)
{
  context.output_stream() << "Zowe Z Open CLI (" << program_name_ref() << ")" << std::endl;
  context.output_stream() << "Version: " << g_version << std::endl;
  context.output_stream() << "Build Date: " << BUILD_DATE << " " << BUILD_TIME << std::endl;
  context.output_stream() << "Copyright Contributors to the Zowe Project." << std::endl;

  const auto result = ast::obj();
  result->set("version", ast::str(g_version));
  result->set("buildDate", ast::str(BUILD_DATE));
  context.set_object(result);

  return 0;
}

int handle_version_simple(plugin::InvocationContext &context)
{
  context.output_stream() << g_version << std::endl;
  return 0;
}

int handle_plugins_list(plugin::InvocationContext &context)
{
  std::ostream &out = context.output_stream();
  std::vector<std::string> plugin_files;

  auto *manager = get_plugin_manager();
  if (manager == nullptr)
  {
    context.error_stream() << "Error: Plugin manager not initialized" << std::endl;
    return 1;
  }

  DIR *plugins_dir = opendir(manager->get_plugins_path().c_str());
  if (plugins_dir != nullptr)
  {
    struct dirent *entry;
    while ((entry = readdir(plugins_dir)) != nullptr)
    {
      std::string name(entry->d_name);
      if (name == "." || name == ".." || name.empty())
        continue;
      plugin_files.push_back(name);
    }
    closedir(plugins_dir);
  }

  std::sort(plugin_files.begin(), plugin_files.end());

  std::set<std::string> registered_files;
  if (manager != nullptr)
  {
    const auto &loaded_plugins = manager->get_loaded_plugins();
    for (auto it = loaded_plugins.begin(); it != loaded_plugins.end(); ++it)
    {
      auto &plugin = *it;
      registered_files.insert(plugin.metadata.filename);

      const auto &metadata = plugin.metadata;
      out << metadata.display_name;
      if (!metadata.filename.empty())
      {
        out << " (" << metadata.filename << ")";
      }
      out << std::endl;
      out << "  Version: " << (metadata.version.empty() ? "n/a" : metadata.version) << std::endl;
      out << std::endl;
    }
  }

  bool has_unregistered = false;
  for (auto it = plugin_files.begin(); it != plugin_files.end(); ++it)
  {
    if (registered_files.find(*it) != registered_files.end())
    {
      continue;
    }
    if (!has_unregistered)
    {
      out << "The following unregistered plug-ins were found in the plugins/ dir:" << std::endl;
      has_unregistered = true;
    }
    out << *it << std::endl;
    out << std::endl;
  }

  return 0;
}

int handle_command(plugin::InvocationContext &context)
{
  const auto is_interactive = context.get<bool>("interactive", false);
  if (context.get<bool>("version", false))
  {
    const auto version_rc = handle_version_simple(context);
    if (!is_interactive)
    {
      return version_rc;
    }
  }

  if (is_interactive)
  {
    return interactive_mode(context);
  }

  // If no interactive mode and no subcommands were invoked, show help
  g_arg_parser->get_root_command().generate_help(context.output_stream());
  return 0;
}

int execute_command(int argc, char *argv[])
{
  auto result = g_arg_parser->parse(argc, argv);
  return result.exit_code;
}

Command &setup_root_command(char *argv[], bool include_plugin_commands)
{
  g_arg_parser = std::make_shared<ArgumentParser>(argv[0], "Zowe Z Open CLI");
  g_arg_parser->add_pre_command_hook([](const Command &command, bool is_help_request)
  {
    if (!is_help_request && command.is_privileged())
    {
      return true;
    }

    // Serialize against concurrent callers and fail closed: never run a
    // non-privileged command while the job step is still APF-authorized
    static std::mutex noaut_mutex;
    std::lock_guard<std::mutex> lock(noaut_mutex);
    return 0 == ZUTNOAUT(); });
  auto &root_command = g_arg_parser->get_root_command();

  root_command.add_keyword_arg("interactive",
                               make_aliases("--interactive", "--it"),
                               "interactive (REPL) mode", ArgType_Flag, false,
                               ArgValue(false));
  root_command.add_keyword_arg("version",
                               make_aliases("--version", "-v"),
                               "display version information", ArgType_Flag, false,
                               ArgValue(false));
  // The root handler prints help or enters the REPL, which owns stdout for its
  // lifetime and frames each result with [rc] plus an EOT byte. Neither is a
  // result worth wrapping -- but --json still has to parse here so that
  // `zo --json ds list` reaches the subcommand.
  root_command.disable_json_capture();
  root_command.set_handler(handle_command);

  // Core commands
  {
    auto version_cmd = command_ptr(new Command("version", "display version information"));
    version_cmd->set_handler(handle_version);
    root_command.add_command(version_cmd); // Should provide more info here, if command is enhanced later.

    if (include_plugin_commands)
    {
      auto plugins_cmd = command_ptr(new Command("plugins", "plug-in management commands"));
      auto list_cmd = command_ptr(new Command("list", "list available plug-ins"));
      list_cmd->set_handler(handle_plugins_list);
      plugins_cmd->add_command(list_cmd);
      root_command.add_command(plugins_cmd);
    }
  }

  return root_command;
}
} // namespace core
