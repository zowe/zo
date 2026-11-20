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

#include "sample_plugin.h"

int hello_command(plugin::InvocationContext &context)
{
  std::string str = "hello from sample command";
  const auto name = context.get<std::string>("name", "");
  if (!name.empty())
  {
    str += " to " + name;
  }
  const auto times = context.get<int>("times", 1);
  for (int i = 0; i < times; i++)
  {
    context.println(str.c_str());
  }
  auto result = ast::obj();
  result->set("message", ast::str(str));
  context.set_object(result);
  return 0;
}

void BasicCommandRegistry::register_commands(CommandProviderImpl::CommandRegistrationContext &ctx)
{
  auto root = ctx.get_root_command();
  auto sample_group = ctx.create_command("sample", "Sample command group for plug-in operations");
  auto hello = ctx.create_command("hello", "says hi to the caller");
  ctx.add_alias(hello, "hi");
  ctx.add_positional_arg(hello, "name", "the name of the person calling the command", CommandProviderImpl::CommandRegistrationContext::ArgumentType_Single, 0, nullptr);
  const auto times_default = CommandDefaultValue(1ll);
  ctx.add_keyword_arg(hello, "times", nullptr, 0, "number of times to say hi", CommandProviderImpl::CommandRegistrationContext::ArgumentType_Single, 0, &times_default);
  ctx.set_handler(hello, hello_command);
  ctx.add_subcommand(sample_group, hello);
  ctx.add_subcommand(root, sample_group);
  ctx.add_to_server(sample_group);
}

// Declares which zowex plug-in ABI this .so was compiled against. Required: zowex rejects any
// plug-in that does not export it, or that reports a version it was not built for.
ZO_PLUGIN_DECLARE_ABI()

void register_plugin(plugin::PluginManager &pm)
{
  pm.register_plugin_metadata("Sample Plug-in", "1.0.0");
  pm.register_command_provider(std::make_unique<BasicCommandProvider>());
}
