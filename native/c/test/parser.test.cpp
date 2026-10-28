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

#include <string>
#include <vector>

#include "parser.test.hpp"
#include "test_utils.hpp"
#include "ztest.hpp"
#include "../parser.hpp"

using namespace parser;
using namespace ztst;

namespace
{

std::vector<char *> to_argv(std::vector<std::string> &storage)
{
  std::vector<char *> argv;
  argv.reserve(storage.size());
  for (std::string &arg : storage)
  {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  return argv;
}

bool g_handler_called = false;
std::string g_handler_arg;

int sample_handler(plugin::InvocationContext &context)
{
  g_handler_called = true;
  g_handler_arg = context.get<std::string>("name");
  return 7;
}

bool g_priv_handler_called = false;
bool g_plain_handler_called = false;

int priv_probe_handler(plugin::InvocationContext &)
{
  g_priv_handler_called = true;
  return 0;
}

int plain_probe_handler(plugin::InvocationContext &)
{
  g_plain_handler_called = true;
  return 0;
}

} // namespace

void parser_tests()
{
  describe("parser tests", []() -> void
  {
    describe("dynamic keyword arguments", []() -> void
    {
      it("captures single dynamic keyword values", []()
      {
        ArgumentParser arg_parser("prog", "dynamic sample");
        Command &root = arg_parser.get_root_command();
        root.enable_dynamic_keywords(ArgType_Single, "example", "placeholder description");
        root.add_keyword_arg("name", make_aliases("-n"), "name to record", ArgType_Single);

        std::vector<std::string> raw = {"prog", "--foo", "bar", "--name", "cli"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.has_dynamic("foo")).ToBe(true);

        const ArgValue *foo_value = result.get_dynamic("foo");
        Expect(foo_value != nullptr).ToBe(true);
        const std::string foo_string = foo_value ? *foo_value->get_string() : "";
        Expect(foo_string == "bar").ToBe(true);

        const std::string *name_value = result.get<std::string>("name");
        Expect(name_value != nullptr).ToBe(true);
        if (name_value)
        {
          Expect(*name_value == "cli").ToBe(true);
        }
      });

      it("collects multiple dynamic keyword values", []()
      {
        ArgumentParser arg_parser("prog", "dynamic sample");
        Command &root = arg_parser.get_root_command();
        root.enable_dynamic_keywords(ArgType_Multiple, "mult-example", "multiple placeholder example");

        std::vector<std::string> raw = {"prog", "--tags", "one", "two", "three"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.has_dynamic("tags")).ToBe(true);

        const ArgValue *tags_value = result.get_dynamic("tags");
        Expect(tags_value != nullptr).ToBe(true);
        const std::vector<std::string> *tags_list = tags_value ? tags_value->get_string_vector() : nullptr;
        Expect(tags_list != nullptr).ToBe(true);
        if (tags_list)
        {
          Expect(tags_list->size()).ToBe(static_cast<size_t>(3));
          Expect((*tags_list)[0] == "one").ToBe(true);
          Expect((*tags_list)[1] == "two").ToBe(true);
          Expect((*tags_list)[2] == "three").ToBe(true);
        }
      });

      it("requires values for dynamic single keywords", []()
      {
        ArgumentParser arg_parser("prog", "dynamic sample");
        Command &root = arg_parser.get_root_command();
        root.enable_dynamic_keywords(ArgType_Single, "val", "description");

        std::vector<std::string> raw = {"prog", "--missing"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("option --missing requires a value.")).Not().ToBe(std::string::npos);
      });

      it("enforces values for dynamic multi-value keywords", []()
      {
        ArgumentParser arg_parser("prog", "dynamic sample");
        Command &root = arg_parser.get_root_command();
        root.enable_dynamic_keywords(ArgType_Multiple, "items", "description");

        std::vector<std::string> raw = {"prog", "--tags"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("option --tags requires at least one value.")).Not().ToBe(std::string::npos);
      });

      it("errors on unknown keyword when dynamic support disabled", []()
      {
        ArgumentParser arg_parser("prog", "dynamic sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("name", make_aliases("-n"), "known option", ArgType_Single);

        std::vector<std::string> raw = {"prog", "--foo", "bar"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.has_dynamic("foo")).ToBe(false);
      });
    });

    describe("keyword arguments", []() -> void
    {
      it("captures values for named keywords", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("dataset", make_aliases("-d"), "dataset to process", ArgType_Single);

        std::vector<std::string> raw = {"prog", "--dataset", "HLQ.PDS"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<std::string>("dataset", "")).ToBe("HLQ.PDS");
      });

      it("accepts short aliases for keyword arguments", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("name", make_aliases("-n"), "name to capture", ArgType_Single);

        std::vector<std::string> raw = {"prog", "-n", "cli"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<std::string>("name", "")).ToBe("cli");
      });

      it("coerces numeric-looking keyword values to strings when requested", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("date", make_aliases("--date"), "date filter", ArgType_Single, false, ArgValue(), true);

        std::vector<std::string> raw = {"prog", "--date", "19900101"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<std::string>("date", "")).ToBe("19900101");
        Expect(result.get<long long>("date") == nullptr).ToBe(true);
      });

      it("keeps numeric-looking keyword values numeric by default", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("timeout", make_aliases("--timeout"), "timeout in seconds", ArgType_Single);

        std::vector<std::string> raw = {"prog", "--timeout", "30"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<long long>("timeout", 0)).ToBe(30);
        Expect(result.get<std::string>("timeout") == nullptr).ToBe(true);
      });

      it("collects multiple values for keyword arguments", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("members", make_aliases("-m"), "member names", ArgType_Multiple);

        std::vector<std::string> raw = {"prog", "--members", "alpha", "beta", "gamma"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        const std::vector<std::string> *members = result.get<std::vector<std::string>>("members");
        Expect(members != nullptr).ToBe(true);
        if (members)
        {
          Expect(members->size()).ToBe(static_cast<size_t>(3));
          Expect((*members)[0] == "alpha").ToBe(true);
          Expect((*members)[1] == "beta").ToBe(true);
          Expect((*members)[2] == "gamma").ToBe(true);
        }
      });

      it("allows explicit boolean values for keyword flags", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("force", make_aliases("-f"), "force execution", ArgType_Flag);

        std::vector<std::string> raw = {"prog", "--force", "false"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<bool>("force", true)).ToBe(false);
      });

      it("reports missing values for keyword arguments", []()
      {
        ArgumentParser arg_parser("prog", "keyword sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("target", make_aliases("-t"), "target data set", ArgType_Single);

        std::vector<std::string> raw = {"prog", "--target"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
      });
    });

    describe("flag parsing", []() -> void
    {
      it("supports combined short boolean flags", []()
      {
        ArgumentParser arg_parser("prog", "flag sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("verbose", make_aliases("-v"), "enable verbose output", ArgType_Flag);
        root.add_keyword_arg("force", make_aliases("-f"), "force execution", ArgType_Flag);

        std::vector<std::string> raw = {"prog", "-vf"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<bool>("verbose")).ToBe(true);
        Expect(result.get_value<bool>("force")).ToBe(true);
      });

      it("rejects combined short flags when a value is required", []()
      {
        ArgumentParser arg_parser("prog", "flag sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("verbose", make_aliases("-v"), "enable verbose output", ArgType_Flag);
        root.add_keyword_arg("output", make_aliases("-o"), "output file", ArgType_Single);

        std::vector<std::string> raw = {"prog", "-vo", "file.txt"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("option -o requires a value and cannot be combined"))
            .Not()
            .ToBe(std::string::npos);
      });

      it("honors automatically generated no- boolean flags", []()
      {
        ArgumentParser arg_parser("prog", "flag sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("debug", make_aliases("-d"), "enable debug output", ArgType_Flag, false, ArgValue(true));

        std::vector<std::string> raw = {"prog", "--no-debug"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<bool>("debug")).ToBe(false);
      });
    });

    describe("positional arguments", []() -> void
    {
      it("reports missing required positional arguments", []()
      {
        ArgumentParser arg_parser("prog", "positional sample");
        Command &root = arg_parser.get_root_command();
        root.add_positional_arg("dataset", "dataset to process", ArgType_Single, true);

        std::vector<std::string> raw = {"prog"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("missing required positional argument: dataset"))
            .Not()
            .ToBe(std::string::npos);
      });

      it("applies defaults when optional positional arguments are omitted", []()
      {
        ArgumentParser arg_parser("prog", "positional sample");
        Command &root = arg_parser.get_root_command();
        root.add_positional_arg("dataset", "dataset to process", ArgType_Single, true);
        root.add_positional_arg("target", "optional target", ArgType_Single, false, ArgValue("default"));

        std::vector<std::string> raw = {"prog", "hlq.dataset"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        const std::string *dataset = result.get<std::string>("dataset");
        Expect(dataset != nullptr).ToBe(true);
        if (dataset)
        {
          Expect(*dataset == "hlq.dataset").ToBe(true);
        }
        Expect(result.get_value<std::string>("target", "fallback")).ToBe("default");
      });

      it("collects multiple positional values", []()
      {
        ArgumentParser arg_parser("prog", "positional sample");
        Command &root = arg_parser.get_root_command();
        root.add_positional_arg("dataset", "dataset to process", ArgType_Single, true);
        root.add_positional_arg("members", "one or more members", ArgType_Multiple, false);

        std::vector<std::string> raw = {"prog", "hlq.dataset", "mem1", "mem2", "mem3"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        const std::vector<std::string> *members = result.get<std::vector<std::string>>("members");
        Expect(members != nullptr).ToBe(true);
        if (members)
        {
          Expect(members->size()).ToBe(static_cast<size_t>(3));
          Expect((*members)[0] == "mem1").ToBe(true);
          Expect((*members)[1] == "mem2").ToBe(true);
          Expect((*members)[2] == "mem3").ToBe(true);
        }
      });

      it("rejects unexpected trailing positional arguments", []()
      {
        ArgumentParser arg_parser("zo", "command sample");
        Command &root = arg_parser.get_root_command();

        command_ptr job_cmd(new Command("job", "job operations"));
        command_ptr list_cmd(new Command("list", "list jobs"));
        job_cmd->add_command(list_cmd);
        root.add_command(job_cmd);

        std::vector<std::string> raw = {"zo", "job", "list", "extra"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("unexpected argument") != std::string::npos).ToBe(true);
      });
    });

    describe("conflict handling", []() -> void
    {
      it("flags conflicting keyword arguments", []()
      {
        ArgumentParser arg_parser("prog", "conflict sample");
        Command &root = arg_parser.get_root_command();

        root.add_argument("primary")
            .aliases(make_aliases("-p"))
            .help("primary action")
            .type(ArgType_Flag)
            .conflicts_with("secondary");
        root.add_argument("secondary")
            .aliases(make_aliases("-s"))
            .help("secondary action")
            .type(ArgType_Flag)
            .conflicts_with("primary");

        std::vector<std::string> raw = {"prog", "--primary", "--secondary"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("conflicting options provided: --primary conflicts with --secondary"))
            .Not()
            .ToBe(std::string::npos);
      });
    });

    describe("required options", []() -> void
    {
      it("demands presence of required keyword arguments", []()
      {
        ArgumentParser arg_parser("prog", "required sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("source", make_aliases("-s"), "source file", ArgType_Single, true);

        std::vector<std::string> raw = {"prog"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("missing required option: -s, --source <value>"))
            .Not()
            .ToBe(std::string::npos);
      });
    });

    describe("command handlers", []() -> void
    {
      it("invokes registered handlers on successful parse", []()
      {
        g_handler_called = false;
        g_handler_arg.clear();

        ArgumentParser arg_parser("prog", "handler sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("name", make_aliases("-n"), "name to capture", ArgType_Single, true);
        root.set_handler(&sample_handler);

        std::vector<std::string> raw = {"prog", "--name", "cli"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.exit_code).ToBe(7);
        Expect(g_handler_called).ToBe(true);
        Expect(g_handler_arg == "cli").ToBe(true);
        Expect(result.get_value<std::string>("name", "")).ToBe("cli");
      });
    });

    describe("pre-command hooks", []() -> void
    {
      it("executes hooks before command handler", []()
      {
        g_handler_called = false;
        bool hook_called = false;
        bool hook_is_help = true;

        ArgumentParser arg_parser("prog", "hook sample");
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook_called = true;
          hook_is_help = is_help_request;
          return true;
        });

        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("name", make_aliases("-n"), "name to capture", ArgType_Single, true);
        root.set_handler(&sample_handler);

        std::vector<std::string> raw = {"prog", "--name", "cli"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(hook_called).ToBe(true);
        Expect(hook_is_help).ToBe(false);
        Expect(g_handler_called).ToBe(true);
      });

      it("aborts execution if hook returns false", []()
      {
        g_handler_called = false;
        bool hook1_called = false;
        bool hook2_called = false;

        ArgumentParser arg_parser("prog", "hook abort sample");
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook1_called = true;
          return false; // abort
        });
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook2_called = true;
          return true;
        });

        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("name", make_aliases("-n"), "name to capture", ArgType_Single, true);
        root.set_handler(&sample_handler);

        std::vector<std::string> raw = {"prog", "--name", "cli"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        // Exit code is 1, status is Success but handler wasn't run
        Expect(result.exit_code).ToBe(1);
        Expect(hook1_called).ToBe(true);
        Expect(hook2_called).ToBe(false);     // Second hook shouldn't run
        Expect(g_handler_called).ToBe(false); // Handler shouldn't run
      });

      it("exposes the privileged flag of the executing subcommand to hooks", []()
      {
        bool hook_called = false;
        bool hook_saw_privileged = false;

        ArgumentParser arg_parser("prog", "privileged flag sample");
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook_called = true;
          hook_saw_privileged = cmd.is_privileged();
          return true;
        });

        auto group = command_ptr(new Command("secure", "privileged group"));
        auto sub_before = command_ptr(new Command("early", "added before set_privileged"));
        sub_before->set_handler(&priv_probe_handler);
        group->add_command(sub_before);
        group->set_privileged(true);
        auto sub_after = command_ptr(new Command("late", "added after set_privileged"));
        sub_after->set_handler(&priv_probe_handler);
        group->add_command(sub_after);
        arg_parser.get_root_command().add_command(group);

        {
          g_priv_handler_called = false;
          std::vector<std::string> raw = {"prog", "secure", "early"};
          std::vector<char *> argv = to_argv(raw);
          ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          Expect(result.exit_code).ToBe(0);
          Expect(hook_called).ToBe(true);
          Expect(hook_saw_privileged).ToBe(true);
          Expect(g_priv_handler_called).ToBe(true);
        }

        {
          g_priv_handler_called = false;
          hook_called = false;
          hook_saw_privileged = false;
          std::vector<std::string> raw = {"prog", "secure", "late"};
          std::vector<char *> argv = to_argv(raw);
          ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          Expect(result.exit_code).ToBe(0);
          Expect(hook_called).ToBe(true);
          Expect(hook_saw_privileged).ToBe(true);
          Expect(g_priv_handler_called).ToBe(true);
        }
      });

      it("supports fail-closed authorization hooks: privileged bypasses, non-privileged aborts", []()
      {
        // mirrors the zo authorization-drop hook when the drop fails:
        // privileged commands skip the drop and run; everything else aborts
        ArgumentParser arg_parser("prog", "fail closed sample");
        arg_parser.add_pre_command_hook([](const Command &cmd, bool is_help_request) -> bool
        {
          if (!is_help_request && cmd.is_privileged())
          {
            return true;
          }
          return false; // simulated failure to relinquish authorization
        });

        auto group = command_ptr(new Command("secure", "privileged group"));
        auto sub = command_ptr(new Command("run", "privileged action"));
        sub->set_handler(&priv_probe_handler);
        group->add_command(sub);
        group->set_privileged(true);
        arg_parser.get_root_command().add_command(group);

        auto plain = command_ptr(new Command("list", "non-privileged action"));
        plain->set_handler(&plain_probe_handler);
        arg_parser.get_root_command().add_command(plain);

        {
          g_priv_handler_called = false;
          std::vector<std::string> raw = {"prog", "secure", "run"};
          std::vector<char *> argv = to_argv(raw);
          ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          Expect(result.exit_code).ToBe(0);
          Expect(g_priv_handler_called).ToBe(true);
        }

        {
          g_plain_handler_called = false;
          std::vector<std::string> raw = {"prog", "list"};
          std::vector<char *> argv = to_argv(raw);
          ParseResult result;
          {
            test_utils::ErrorStreamCapture error_capture;
            result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          }
          Expect(result.exit_code).ToBe(1);
          Expect(g_plain_handler_called).ToBe(false); // fail closed: handler never ran
        }
      });

      it("executes hooks before help is shown", []()
      {
        bool hook_called = false;
        bool hook_is_help = false;

        ArgumentParser arg_parser("prog", "hook help sample");
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook_called = true;
          hook_is_help = is_help_request;
          return true;
        });

        std::vector<std::string> raw = {"prog", "--help"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture out_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_HelpRequested);
        Expect(hook_called).ToBe(true);
        Expect(hook_is_help).ToBe(true);
      });

      it("executes hooks for command groups without handler when help is shown implicitly", []()
      {
        bool hook_called = false;
        bool hook_is_help = false;

        ArgumentParser arg_parser("prog", "hook group sample");
        arg_parser.add_pre_command_hook([&](const Command &cmd, bool is_help_request) -> bool
        {
          hook_called = true;
          hook_is_help = is_help_request;
          return true;
        });

        Command &root = arg_parser.get_root_command();
        command_ptr sub_cmd(new Command("sub", "sub command"));
        root.add_command(sub_cmd);

        std::vector<std::string> raw = {"prog"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture out_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_HelpRequested);
        Expect(hook_called).ToBe(true);
        Expect(hook_is_help).ToBe(true); // Treated as help request
      });
    });

    describe("passthrough arguments", []() -> void
    {
      it("captures arguments after -- delimiter", []()
      {
        ArgumentParser arg_parser("prog", "passthrough sample");
        Command &root = arg_parser.get_root_command();
        root.add_positional_arg("target", "target to run", ArgType_Single, true);
        root.enable_passthrough("Arguments to pass through");

        std::vector<std::string> raw = {"prog", "my-target", "--", "ls", "-la"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<std::string>("target", "")).ToBe("my-target");
        Expect(result.has_passthrough()).ToBe(true);
        const std::vector<std::string> &pass = result.get_passthrough_args();
        Expect(pass.size()).ToBe(static_cast<size_t>(2));
        Expect(pass[0] == "ls").ToBe(true);
        Expect(pass[1] == "-la").ToBe(true);
      });

      it("preserves flag-like arguments after -- as literals", []()
      {
        ArgumentParser arg_parser("prog", "passthrough sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("verbose", make_aliases("-v"), "verbose mode", ArgType_Flag);
        root.enable_passthrough("Passthrough args");

        std::vector<std::string> raw = {"prog", "-v", "--", "--help", "-x"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.get_value<bool>("verbose")).ToBe(true);
        const std::vector<std::string> &pass = result.get_passthrough_args();
        Expect(pass.size()).ToBe(static_cast<size_t>(2));
        Expect(pass[0] == "--help").ToBe(true);
        Expect(pass[1] == "-x").ToBe(true);
      });

      it("allows empty passthrough arguments after --", []()
      {
        ArgumentParser arg_parser("prog", "passthrough sample");
        Command &root = arg_parser.get_root_command();
        root.enable_passthrough("Passthrough args");

        std::vector<std::string> raw = {"prog", "--"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(result.has_passthrough()).ToBe(false); // empty but valid
        Expect(result.get_passthrough_args().empty()).ToBe(true);
      });

      it("errors when -- is used without passthrough enabled", []()
      {
        ArgumentParser arg_parser("prog", "passthrough sample");
        Command &root = arg_parser.get_root_command();
        root.add_positional_arg("target", "target", ArgType_Single, true);
        // Note: passthrough NOT enabled

        std::vector<std::string> raw = {"prog", "target", "--", "extra"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.error_message.find("--")).Not().ToBe(std::string::npos);
      });

      it("treats subsequent -- as literal strings after first", []()
      {
        ArgumentParser arg_parser("prog", "passthrough sample");
        Command &root = arg_parser.get_root_command();
        root.enable_passthrough("Passthrough args");

        std::vector<std::string> raw = {"prog", "--", "arg1", "--", "arg2"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        const std::vector<std::string> &pass = result.get_passthrough_args();
        Expect(pass.size()).ToBe(static_cast<size_t>(3));
        Expect(pass[0] == "arg1").ToBe(true);
        Expect(pass[1] == "--").ToBe(true);
        Expect(pass[2] == "arg2").ToBe(true);
      });

      it("makes passthrough arguments accessible in command handler", []()
      {
        static bool handler_called = false;
        static std::vector<std::string> captured_passthrough;

        handler_called = false;
        captured_passthrough.clear();

        auto handler = [](plugin::InvocationContext &context) -> int
        {
          handler_called = true;
          captured_passthrough = context.get_passthrough_args();
          return 0;
        };

        ArgumentParser arg_parser("prog", "passthrough handler sample");
        Command &root = arg_parser.get_root_command();
        root.add_keyword_arg("verbose", make_aliases("-v"), "verbose mode", ArgType_Flag);
        root.enable_passthrough("Arguments to pass through");
        root.set_handler(handler);

        std::vector<std::string> raw = {"prog", "-v", "--", "ls", "-la", "/tmp"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(handler_called).ToBe(true);
        Expect(captured_passthrough.size()).ToBe(static_cast<size_t>(3));
        Expect(captured_passthrough[0] == "ls").ToBe(true);
        Expect(captured_passthrough[1] == "-la").ToBe(true);
        Expect(captured_passthrough[2] == "/tmp").ToBe(true);
      });
    });

    describe("json envelope", []() -> void
    {
      it("injects --json into every command in the tree", []()
      {
        static bool leaf_called = false;
        leaf_called = false;

        auto leaf_handler = [](plugin::InvocationContext &context) -> int
        {
          leaf_called = true;
          context.output_stream() << "leaf ran" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto group = command_ptr(new Command("grp", "a group"));
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(leaf_handler);
        group->add_command(leaf);
        root.add_command(group);

        std::vector<std::string> raw = {"prog", "grp", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(leaf_called).ToBe(true);
        Expect(result.json_requested).ToBe(true);
        Expect(result.json_output).ToContain("\"success\":true");
      });

      it("accepts --json before the subcommand without consuming it", []()
      {
        static bool leaf_called = false;
        leaf_called = false;

        auto leaf_handler = [](plugin::InvocationContext &context) -> int
        {
          leaf_called = true;
          context.output_stream() << "leaf ran" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto group = command_ptr(new Command("grp", "a group"));
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(leaf_handler);
        group->add_command(leaf);
        root.add_command(group);

        // A flag whose value is omitted used to swallow the next token,
        // which turned this into "unexpected argument: leaf".
        std::vector<std::string> raw = {"prog", "--json", "grp", "leaf"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(leaf_called).ToBe(true);
        Expect(result.json_requested).ToBe(true);
      });

      it("captures handler output instead of printing it", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          context.output_stream() << "human readable line" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        std::string printed;
        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          printed = output_capture.get_output();
        }

        Expect(result.json_stdout).ToContain("human readable line");
        // Only the envelope reaches stdout; the plain text is inside it.
        Expect(printed).ToContain("\"data\"");
        Expect(printed).ToContain("human readable line");
        Expect(printed.find("\"success\":true")).Not().ToBe(std::string::npos);
      });

      it("uses captured output as the payload when a handler sets no object", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          context.output_stream() << "Data set created" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.json_output).ToContain("Data set created");
      });

      it("discards captured output when the handler already reported fields", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          context.output_stream() << "MY.DATA.SET" << std::endl;
          const auto items = ast::arr();
          items->push(ast::str("MY.DATA.SET"));
          const auto payload = ast::obj();
          payload->set("items", items);
          payload->set("returnedRows", ast::i64(1));
          context.set_object(payload);
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.json_output).ToContain("\"returnedRows\":1");
        // The aligned text is a rendering of items[], not extra payload.
        // Match on the nested key with a string value: the envelope always
        // has a top-level "data" whose value is an object.
        Expect(result.json_output).Not().ToContain("\"data\":\"");
      });

      it("keeps captured output for a command marked as stdout payload", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          context.output_stream() << "//JOBCARD JOB";
          const auto payload = ast::obj();
          payload->set("etag", ast::str("abc123"));
          context.set_object(payload);
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        leaf->mark_stdout_as_payload();
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        // Both the handler's own field and the stdout payload survive.
        Expect(result.json_output).ToContain("\"etag\":\"abc123\"");
        Expect(result.json_output).ToContain("//JOBCARD JOB");
      });

      it("reports a failing handler as unsuccessful with its exit code", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          context.error_stream() << "Error: could not list data set" << std::endl;
          return 8;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.exit_code).ToBe(8);
        Expect(result.json_output).ToContain("\"success\":false");
        Expect(result.json_output).ToContain("\"exitCode\":8");
        Expect(result.json_output).ToContain("could not list data set");
      });

      it("escapes quotes and backslashes in the payload", []()
      {
        auto handler = [](plugin::InvocationContext &context) -> int
        {
          const auto payload = ast::obj();
          payload->set("note", ast::str("say \"hi\" and a back\\slash"));
          context.set_object(payload);
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        // ast::Ast::as_json() would emit these raw and produce invalid JSON.
        Expect(result.json_output).ToContain("say \\\"hi\\\"");
        Expect(result.json_output).ToContain("back\\\\slash");
      });

      it("prints help rather than an envelope when help is requested", []()
      {
        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json", "--help"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        std::string printed;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          printed = output_capture.get_output();
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_HelpRequested);
        Expect(result.json_output.empty()).ToBe(true);
        Expect(printed).Not().ToContain("\"success\"");
      });

      it("honors disable_json_output through add_command", []()
      {
        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->disable_json_output();
        // add_command re-runs the injector, so the opt-out has to stick.
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::ErrorStreamCapture error_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        // The flag is gone, so this is an unknown option. The raw-input
        // pre-scan still saw it, which is deliberate: a usage error under
        // --json should be reported as JSON rather than as bare stderr.
        Expect(result.status).ToBe(ParseResult::ParserStatus_ParseError);
        Expect(result.json_requested).ToBe(false);
        Expect(result.json_output).ToContain("\"success\":false");
      });

      it("suppresses the envelope for a capture-disabled command", []()
      {
        static bool handler_called = false;
        handler_called = false;

        auto handler = [](plugin::InvocationContext &context) -> int
        {
          handler_called = true;
          context.output_stream() << "help-like text" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        root.disable_json_capture();
        root.set_handler(handler);

        std::vector<std::string> raw = {"prog", "--json"};
        std::vector<char *> argv = to_argv(raw);

        std::string printed;
        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
          printed = output_capture.get_output();
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(handler_called).ToBe(true);
        // The flag parsed, but this handler owns stdout.
        Expect(result.json_suppressed).ToBe(true);
        Expect(result.json_output.empty()).ToBe(true);
        Expect(printed).ToContain("help-like text");
        Expect(printed).Not().ToContain("\"success\"");
      });

      it("threads --json from a capture-disabled parent into a subcommand", []()
      {
        static bool leaf_called = false;
        leaf_called = false;

        auto leaf_handler = [](plugin::InvocationContext &context) -> int
        {
          leaf_called = true;
          context.output_stream() << "leaf output" << std::endl;
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        // Mirrors the real root command: accepts --json, never captured.
        root.disable_json_capture();
        auto group = command_ptr(new Command("grp", "a group"));
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->set_handler(leaf_handler);
        group->add_command(leaf);
        root.add_command(group);

        std::vector<std::string> raw = {"prog", "--json", "grp", "leaf"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(leaf_called).ToBe(true);
        Expect(result.json_suppressed).ToBe(false);
        Expect(result.json_requested).ToBe(true);
        Expect(result.json_output).ToContain("leaf output");
      });
      it("leaves --json after a passthrough delimiter to the child", []()
      {
        static std::vector<std::string> captured;
        captured.clear();

        auto handler = [](plugin::InvocationContext &context) -> int
        {
          captured = context.get_passthrough_args();
          return 0;
        };

        ArgumentParser arg_parser("prog", "json sample");
        Command &root = arg_parser.get_root_command();
        auto leaf = command_ptr(new Command("leaf", "a leaf"));
        leaf->enable_passthrough("Arguments to pass through");
        leaf->set_handler(handler);
        root.add_command(leaf);

        std::vector<std::string> raw = {"prog", "leaf", "--", "child", "--json"};
        std::vector<char *> argv = to_argv(raw);

        ParseResult result;
        {
          test_utils::OutputStreamCapture output_capture;
          result = arg_parser.parse(static_cast<int>(argv.size()), argv.data());
        }

        Expect(result.status).ToBe(ParseResult::ParserStatus_Success);
        Expect(captured.size()).ToBe(static_cast<size_t>(2));
        Expect(captured[1] == "--json").ToBe(true);
        // The pre-scan must not treat the child's flag as ours.
        Expect(result.json_requested).ToBe(false);
        Expect(result.json_output.empty()).ToBe(true);
      });
    });
  });
}
