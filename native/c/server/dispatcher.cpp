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

#include "dispatcher.hpp"
#include "../zlogger.hpp"
#include "rpcio.hpp"
#include "rpc_server.hpp"
#include <algorithm>
#include <vector>

using std::string;

bool CommandDispatcher::register_command(const string &command_name, const CommandBuilder &builder)
{
  if (command_name.empty() || builder.get_handler() == nullptr)
  {
    LOG_ERROR("Cannot register command: invalid name or handler");
    return false;
  }

  // Check if command already exists
  if (m_commands.find(command_name) != m_commands.end())
  {
    LOG_ERROR("Command already registered: %s", command_name.c_str());
    return false;
  }

  m_commands.insert(std::make_pair(command_name, builder));

  LOG_DEBUG("Registered command: %s", command_name.c_str());
  return true;
}

int CommandDispatcher::dispatch(const string &command_name, MiddlewareContext &context)
{
  auto it = m_commands.find(command_name);
  if (it == m_commands.end())
  {
    string errMsg = "Command not found: " + command_name;
    context.errln(errMsg.c_str());
    LOG_ERROR("%s", errMsg.c_str());
    return RpcErrorCode::METHOD_NOT_FOUND;
  }

  const CommandBuilder &builder = it->second;
  CommandHandler handler = builder.get_handler();
  if (handler == nullptr)
  {
    context.errln("Invalid command handler");
    LOG_ERROR("Invalid command handler for: %s", command_name.c_str());
    return RpcErrorCode::INTERNAL_ERROR;
  }

  try
  {
    LOG_DEBUG("Dispatching command: %s", command_name.c_str());

    // Apply input transforms
    builder.apply_input_transforms(context);

    // Call the command handler with the context
    int result = handler(context);

    // Apply output transforms
    builder.apply_output_transforms(context);

    if (result != 0)
    {
      LOG_ERROR("Command failed with code %d: %s", result, command_name.c_str());
    }

    return result;
  }
  catch (const std::exception &e)
  {
    string errMsg = string("Command execution failed: ") + e.what();
    context.err("Command execution failed: ");
    context.errln(e.what());
    LOG_ERROR("%s (command: %s)", errMsg.c_str(), command_name.c_str());
    return RpcErrorCode::INTERNAL_ERROR;
  }
  catch (...)
  {
    context.errln("Command execution failed with unknown error");
    LOG_ERROR("Command execution failed with unknown error: %s", command_name.c_str());
    return RpcErrorCode::INTERNAL_ERROR;
  }
}

bool CommandDispatcher::has_command(const string &command_name) const
{
  return m_commands.find(command_name) != m_commands.end();
}

std::vector<string> CommandDispatcher::get_registered_commands() const
{
  std::vector<string> commands;
  commands.reserve(m_commands.size());

  for (auto it = m_commands.begin(); it != m_commands.end(); ++it)
  {
    commands.push_back(it->first);
  }

  // Sort for consistent ordering
  std::sort(commands.begin(), commands.end());
  return commands;
}

bool CommandDispatcher::unregister_command(const string &command_name)
{
  auto it = m_commands.find(command_name);
  if (it == m_commands.end())
  {
    LOG_ERROR("Cannot unregister command (not found): %s", command_name.c_str());
    return false;
  }

  m_commands.erase(it);

  LOG_DEBUG("Unregistered command: %s", command_name.c_str());
  return true;
}

void CommandDispatcher::clear()
{
  LOG_DEBUG("Clearing all registered commands");
  m_commands.clear();
}
