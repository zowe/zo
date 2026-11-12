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

#include "tso.hpp"
#include "../ztso.hpp"

using namespace parser;

namespace tso
{
int handle_tso_issue(InvocationContext &context)
{
  int rc = 0;
  std::string command = context.get<std::string>("command", "");
  std::string response;

  rc = ztso_issue(command, response);

  if (0 != rc)
  {
    context.error_stream() << "Error running command, rc '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << response << std::endl;
  }

  context.output_stream() << response << '\n';

  return rc;
}

void register_commands(parser::Command &root_command)
{
  auto tso_group = command_ptr(new Command("tso", "TSO operations"));
  {
    auto tso_issue_cmd = command_ptr(new Command("issue", "issue TSO command"));
    tso_issue_cmd->mark_stdout_as_payload();
    tso_issue_cmd->add_positional_arg("command", "command to issue", ArgType_Single, true);
    tso_issue_cmd->set_handler(handle_tso_issue);

    tso_group->add_command(tso_issue_cmd);
  }
  root_command.add_command(tso_group);
}
} // namespace tso