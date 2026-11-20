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

#include "console.hpp"
#include "../zcn.hpp"
#include "../zut.hpp"

using namespace parser;

namespace console
{

int handle_console_issue(plugin::InvocationContext &context)
{
  int rc = 0;
  ZcnSession session;
  ZCN &zcn = session.control_block();

  std::string console_name = context.get<std::string>("console-name", "");
  if (console_name.empty() && 0 != zcn_build_default_console_name(console_name))
  {
    console_name = "zowex";
  }
  const long long timeout = context.get<long long>("timeout", 0);

  const std::string command = context.get<std::string>("command", "");
  const bool wait = context.get<bool>("wait", true);

  if (timeout > 0)
  {
    zcn.timeout = timeout;
  }

  rc = session.activate(console_name);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not activate console: '" << console_name << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zcn.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  rc = session.put(command);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not write to console: '" << console_name << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zcn.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  if (wait)
  {
    std::string response = "";
    rc = session.get(response);
    if (0 != rc)
    {
      context.error_stream() << "Error: could not get from console: '" << console_name << "' rc: '" << rc << "'" << std::endl;
      context.error_stream() << "  Details: " << zcn.diag.e_msg << std::endl;
      return RTNCD_FAILURE;
    }
    context.output_stream() << response << std::endl;
  }

  rc = session.deactivate();
  if (0 != rc)
  {
    context.error_stream() << "Error: could not deactivate console: '" << console_name << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zcn.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }
  return rc;
}

void register_commands(parser::Command &root_command)
{
  auto console_group = command_ptr(new Command("console", "z/OS console operations"));
  console_group->add_alias("cn");
  console_group->set_privileged(true);
  // zoweax runs APF-authorized and these commands stay privileged, so the
  // authorization-drop hook is skipped for them. Serializing a --json envelope
  // would call the HWTJ services from an authorized job step, which needs a
  // security review before being enabled here.
  console_group->disable_json_output();
  {
    auto issue_cmd = command_ptr(new Command("issue", "issue a console command"));
    issue_cmd->add_keyword_arg("console-name",
                               make_aliases("--cn", "--console-name"),
                               "extended console name (default: current user ID plus a digit)",
                               ArgType_Single, false);
    issue_cmd->add_keyword_arg("wait",
                               make_aliases("--wait"),
                               "wait for responses", ArgType_Flag, false,
                               ArgValue(true));
    issue_cmd->add_keyword_arg("timeout",
                               make_aliases("--timeout"),
                               "timeout in seconds", ArgType_Single, false);
    issue_cmd->add_positional_arg("command", "command to run, e.g. 'D IPLINFO'",
                                  ArgType_Single, true);
    issue_cmd->disable_json_output();
    issue_cmd->set_handler(handle_console_issue);

    console_group->add_command(issue_cmd);
  }
  root_command.add_command(console_group);
}
} // namespace console
