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

#include "../parser.hpp"

namespace plugin
{
class PluginManager;
} // namespace plugin

namespace core
{
parser::Command &setup_root_command(char *argv[], bool include_plugin_commands = true);
int execute_command(int argc, char *argv[]);
void disable_json_output();
int handle_version(plugin::InvocationContext &context);
void set_plugin_manager(plugin::PluginManager *manager);
plugin::PluginManager *get_plugin_manager();
void set_version(const std::string &version);
const std::string &get_version();
void set_program_name(const std::string &name);
} // namespace core
