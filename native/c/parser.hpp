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

#ifndef PARSER_HPP
#define PARSER_HPP

#include "extend/plugin.hpp"
#include "json_output.hpp"
#include "lexer.hpp"
#include "zlogger.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace parser
{

// Levenshtein distance for suggestions
// Measures the similarity between strings a and b
inline size_t levenshtein_distance(const std::string &a, const std::string &b)
{
  const size_t len_a = a.size();
  const size_t len_b = b.size();
  std::vector<size_t> prev(len_b + 1, 0);
  std::vector<size_t> curr(len_b + 1, 0);

  for (size_t j = 0; j <= len_b; ++j)
    prev[j] = j;

  for (size_t i = 1; i <= len_a; ++i)
  {
    curr[0] = i;
    for (size_t j = 1; j <= len_b; ++j)
    {
      size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      size_t del = prev[j] + 1;
      size_t ins = curr[j - 1] + 1;
      size_t sub = prev[j - 1] + cost;
      size_t min_val = del < ins ? del : ins;
      min_val = min_val < sub ? min_val : sub;
      curr[j] = min_val;
    }
    prev = curr;
  }
  return prev[len_b];
}

// Helper for environments without initializer_list support
inline std::vector<std::string> make_aliases(const char *a1 = 0,
                                             const char *a2 = 0,
                                             const char *a3 = 0,
                                             const char *a4 = 0)
{
  std::vector<std::string> v;
  v.reserve(4);
  if (a1)
    v.push_back(a1);
  if (a2)
    v.push_back(a2);
  if (a3)
    v.push_back(a3);
  if (a4)
    v.push_back(a4);
  return v;
}

class Command;
using command_ptr = std::shared_ptr<Command>;
using enable_shared_command = std::enable_shared_from_this<Command>;

class ArgumentParser;
class ParseResult;

using ArgValue = plugin::Argument;

// Return value signals whether the hook succeeded
using PreCommandHook = std::function<bool(const Command &command, bool is_help_request)>;

enum ArgType
{
  ArgType_Flag,     // boolean switch (e.g., --verbose)
  ArgType_Single,   // expects a single value (e.g., --output file.txt)
  ArgType_Multiple, // expects one or more values (e.g., --input a.txt b.txt)
};

struct ArgTemplate
{
  std::string name;
  std::vector<std::string> aliases;
  std::string help;
  ArgType type;
  bool required;
  ArgValue default_value;
  std::vector<std::string> conflicts_with;
  bool hidden;
  bool coerce_as_string;
};

struct ArgumentDef
{
  std::string name; // internal name to access the parsed value
  std::vector<std::string>
      aliases;      // all flag aliases (e.g., "-f", "--file", "--alias")
  std::string help; // help text description
  ArgType type;     // type of argument (flag, single, etc.)
  bool positional;
  bool required;                 // is this argument mandatory?
  bool hidden = false;           // is this argument meant to be hidden / for developers only?
  bool coerce_as_string = false; // treat numeric-looking values as strings when parsing
  ArgValue default_value;        // default value if argument is not provided
  bool is_help_flag;             // internal flag to identify the help argument
  bool is_json_flag = false;     // internal flag to identify the JSON output flag
  std::vector<std::string>
      conflicts_with; // names of other arguments this one conflicts with

  // Main constructor: accepts a vector of aliases
  ArgumentDef(std::string n, const std::vector<std::string> &als, std::string h,
              ArgType t = ArgType_Flag, bool pos = false, bool req = false,
              ArgValue def_val = ArgValue(), bool help_flag = false)
      : name(n), aliases(als), help(h), type(t), positional(pos), required(req),
        default_value(def_val), is_help_flag(help_flag)
  {
  }

  // Convenience constructor: single alias
  ArgumentDef(std::string n, std::string alias, std::string h,
              ArgType t = ArgType_Flag, bool pos = false, bool req = false,
              ArgValue def_val = ArgValue(), bool help_flag = false)
      : name(n), help(h), type(t), positional(pos), required(req),
        default_value(def_val), is_help_flag(help_flag)
  {
    if (!alias.empty())
    {
      aliases.push_back(alias);
    }
  }

  // Default constructor
  ArgumentDef()
      : type(ArgType_Flag), positional(false), required(false),
        is_help_flag(false)
  {
  }

  // get display name (e.g., "-f, --file")
  std::string get_display_name() const
  {
    std::string display;
    // Add all aliases
    for (size_t i = 0; i < aliases.size(); ++i)
    {
      if (!aliases[i].empty())
      {
        if (!display.empty())
          display += ", ";
        display += aliases[i];
      }
    }
    // Add canonical name as long flag if not already present
    if (!name.empty() && !positional)
    {
      std::string long_flag = "--" + name;
      bool found = false;
      for (size_t i = 0; i < aliases.size(); ++i)
      {
        if (aliases[i] == long_flag)
        {
          found = true;
          break;
        }
      }
      if (!found)
      {
        if (!display.empty())
          display += ", ";
        display += long_flag;
      }
    }
    if (type != ArgType_Flag)
    {
      display += " <value>";
      if (type == ArgType_Multiple)
      {
        display += "...";
      }
    }
    return display;
  }
};

class ArgumentFactory
{
public:
  static ArgumentDef create(const ArgTemplate &tpl, bool positional)
  {
    ArgumentDef arg(tpl.name,
                    positional ? std::vector<std::string>() : tpl.aliases,
                    tpl.help, tpl.type, positional, tpl.required,
                    tpl.default_value);
    arg.hidden = tpl.hidden;
    arg.coerce_as_string = tpl.coerce_as_string;
    arg.conflicts_with = tpl.conflicts_with;
    return arg;
  }
};

class ArgumentBuilder
{
public:
  ArgumentBuilder(Command *cmd, const std::string &name)
      : m_command(cmd), m_arg_def(), m_is_registered(false)
  {
    m_arg_def.name = name;
  }

  ArgumentBuilder(ArgumentBuilder &&other);

  ~ArgumentBuilder();

  ArgumentBuilder &operator=(ArgumentBuilder &&other);

  ArgumentBuilder &alias(const std::string &als)
  {
    m_arg_def.aliases.push_back(als);
    return *this;
  }

  ArgumentBuilder &aliases(const std::vector<std::string> &als)
  {
    m_arg_def.aliases.insert(m_arg_def.aliases.end(), als.begin(), als.end());
    return *this;
  }

  ArgumentBuilder &help(const std::string &h)
  {
    m_arg_def.help = h;
    return *this;
  }

  ArgumentBuilder &type(ArgType t)
  {
    m_arg_def.type = t;
    return *this;
  }

  ArgumentBuilder &positional(bool val = true)
  {
    m_arg_def.positional = val;
    return *this;
  }

  ArgumentBuilder &required(bool val = true)
  {
    m_arg_def.required = val;
    return *this;
  }

  ArgumentBuilder &hidden(bool val = true)
  {
    m_arg_def.hidden = val;
    return *this;
  }

  ArgumentBuilder &coerce_as_string(bool val = true)
  {
    m_arg_def.coerce_as_string = val;
    return *this;
  }

  ArgumentBuilder &default_value(const ArgValue &val)
  {
    m_arg_def.default_value = val;
    return *this;
  }

  ArgumentBuilder &conflicts_with(const std::string &other_arg)
  {
    m_arg_def.conflicts_with.push_back(other_arg);
    return *this;
  }

  ArgumentBuilder &
  conflicts_with(const std::vector<std::string> &other_args)
  {
    m_arg_def.conflicts_with.insert(m_arg_def.conflicts_with.end(),
                                    other_args.begin(), other_args.end());
    return *this;
  }

private:
  Command *m_command;
  ArgumentDef m_arg_def;
  bool m_is_registered;

  // Prohibit copying
  ArgumentBuilder(const ArgumentBuilder &);
  ArgumentBuilder &operator=(const ArgumentBuilder &);
};

struct CmdExample
{
  std::string title;
  std::string command;
};

// represents a command or subcommand with its arguments
class Command : public enable_shared_command
{
public:
  using CommandHandler = int (*)(plugin::InvocationContext &context);

  Command(std::string name, std::string help)
      : m_name(name), m_help(help), m_handler(nullptr),
        m_allow_dynamic_keywords(false), m_allow_passthrough(false), m_privileged(false)
  {
    ensure_help_argument();
    ensure_json_argument();
  }

  // destructor to clean up subcommand pointers
  ~Command()
  {
    m_commands.clear();
  }

  // mark command as privileged (and propagate to children)
  Command &set_privileged(bool privileged = true)
  {
    m_privileged = privileged;
    for (auto &pair : m_commands)
    {
      if (pair.second)
      {
        pair.second->set_privileged(privileged);
      }
    }
    return *this;
  }

  bool is_privileged() const
  {
    return m_privileged;
  }

  // propagate pre_hooks recursively
  Command &set_pre_hooks(std::shared_ptr<std::vector<PreCommandHook>> hooks)
  {
    m_pre_hooks = hooks;
    for (auto &pair : m_commands)
    {
      if (pair.second)
      {
        pair.second->set_pre_hooks(hooks);
      }
    }
    return *this;
  }

  // add an argument
  Command &add_arg(std::string name, const std::vector<std::string> &aliases,
                   std::string help, ArgType type = ArgType_Flag,
                   bool positional = false, bool required = false,
                   ArgValue default_value = ArgValue(), bool hidden = false,
                   bool coerce_as_string = false)
  {
    ArgumentDef arg(name, aliases, help, type, positional, required, default_value);
    arg.hidden = hidden;
    arg.coerce_as_string = coerce_as_string;
    register_argument(arg);
    return *this;
  }

  ArgumentBuilder add_argument(const std::string &name);

  Command &add_example(const std::string &title, const std::string &command)
  {
    CmdExample example;
    example.title = title;
    example.command = command;
    m_examples.push_back(example);
    return *this;
  }

  Command &enable_dynamic_keywords(
      ArgType dynamic_type, const std::string &placeholder, const std::string &description)
  {
    if (dynamic_type == ArgType_Flag)
    {
      throw std::invalid_argument(
          "dynamic keyword arguments cannot be configured as flags.");
    }
    m_allow_dynamic_keywords = true;
    m_dynamic.keyword_type = dynamic_type;
    m_dynamic.description = description;
    m_dynamic.placeholder = placeholder;
    return *this;
  }

  bool allow_dynamic_keywords() const
  {
    return m_allow_dynamic_keywords;
  }

  ArgType get_dynamic_keyword_type() const
  {
    return m_dynamic.keyword_type;
  }

  Command &enable_passthrough(const std::string &description = "Arguments to pass through")
  {
    m_allow_passthrough = true;
    m_passthrough_description = description;
    return *this;
  }

  bool allow_passthrough() const
  {
    return m_allow_passthrough;
  }

  const std::string &get_passthrough_description() const
  {
    return m_passthrough_description;
  }

  // add a keyword/option argument (e.g., --file, -f)
  // New add_keyword_arg: accepts a vector of aliases for maximum flexibility
  Command &add_keyword_arg(std::string name,
                           const std::vector<std::string> &aliases,
                           std::string help, ArgType type = ArgType_Flag,
                           bool required = false,
                           ArgValue default_value = ArgValue(),
                           bool coerce_as_string = false)
  {
    return add_arg(name, aliases, help, type, false, required, default_value,
                   false, coerce_as_string);
  }

  // add a positional argument (defined by order)
  Command &add_positional_arg(std::string name, std::string help,
                              ArgType type = ArgType_Single,
                              bool required = true,
                              ArgValue default_value = ArgValue(),
                              bool coerce_as_string = false)
  {
    if (type == ArgType_Flag)
    {
      throw std::invalid_argument("positional arguments cannot be flags.");
    }
    return add_arg(name, std::vector<std::string>(), help, type, true, required,
                   default_value, false, coerce_as_string);
  }

  Command &add_keyword_arg(const ArgTemplate &tpl)
  {
    register_argument(ArgumentFactory::create(tpl, false));
    return *this;
  }

  Command &add_positional_arg(const ArgTemplate &tpl)
  {
    if (tpl.type == ArgType_Flag)
    {
      throw std::invalid_argument("positional arguments cannot be flags.");
    }
    register_argument(ArgumentFactory::create(tpl, true));
    return *this;
  }

  // add a command under this command (making this command act as a group)
  Command &add_command(command_ptr sub)
  {
    if (!sub)
      return *this;

    sub->ensure_help_argument();
    sub->ensure_json_argument();

    // Only propagate privileges if the parent is privileged
    if (m_privileged)
    {
      sub->set_privileged(true);
    }

    if (m_pre_hooks)
    {
      sub->set_pre_hooks(m_pre_hooks);
    }

    std::string sub_name = sub->get_name();
    // check for conflicts with existing names and aliases
    if (m_commands.count(sub_name))
    {
      throw std::invalid_argument("subcommand name '" + sub_name +
                                  "' already exists.");
    }
    for (const auto &pair : m_commands)
    {
      if (pair.second->has_alias(sub_name))
      {
        throw std::invalid_argument("subcommand name '" + sub_name +
                                    "' conflicts with an existing alias.");
      }
      const std::vector<std::string> &aliases = sub->get_aliases();
      for (const auto &alias : aliases)
      {
        if (pair.first == alias || pair.second->has_alias(alias))
        {
          throw std::invalid_argument(
              "subcommand alias '" + alias +
              "' conflicts with an existing name or alias.");
        }
      }
    }
    // also check the new subcommand's aliases against its own name
    auto &aliases = sub->get_aliases();
    for (auto alias = aliases.begin(); alias != aliases.end(); alias++)
    {
      if (*alias == sub_name)
      {
        throw std::invalid_argument("subcommand alias '" + *alias +
                                    "' cannot be the same as its name '" +
                                    sub_name + "'.");
      }
    }

    m_commands[sub_name] = sub;
    return *this;
  }

  // add an alias to this command
  Command &add_alias(const std::string &alias)
  {
    if (alias == m_name)
    {
      throw std::invalid_argument(
          "alias cannot be the same as the command name '" + m_name + "'.");
    }
    // could add checks here to ensure alias doesn't conflict with other sibling
    // commands/aliases if added to a parent
    m_aliases.push_back(alias);
    return *this;
  }

  // set the function pointer handler
  Command &set_handler(CommandHandler handler)
  {
    m_handler = handler;
    return *this;
  }

  const std::string &get_name() const
  {
    return m_name;
  }
  const std::string &get_help() const
  {
    return m_help;
  }
  const std::map<std::string, command_ptr> &get_commands() const
  {
    return m_commands;
  }
  const std::vector<ArgumentDef> &get_args() const
  {
    return m_args;
  }
  const std::vector<std::string> &get_aliases() const
  {
    return m_aliases;
  }
  CommandHandler get_handler() const
  {
    return m_handler;
  }

  ParseResult parse(const std::vector<lexer::Token> &tokens,
                    size_t &current_token_index,
                    const std::string &command_path_prefix,
                    bool json_inherited = false) const;

  // generate help text for this command and its subcommands
  void generate_help(std::ostream &os,
                     const std::string &command_path_prefix = "") const
  {
    std::vector<ArgumentDef> pos_args;
    std::vector<ArgumentDef> kw_args;
    for (const auto &arg : m_args)
    {
      if (arg.positional)
        pos_args.push_back(arg);
      else
        kw_args.push_back(arg);
    }
    // calculate max widths for alignment
    size_t max_pos_arg_width = 0;
    for (const auto &arg : pos_args)
    {
      if (arg.hidden)
        continue;
      max_pos_arg_width = std::max(max_pos_arg_width, arg.name.length());
    }

    size_t max_kw_arg_width = 0;
    for (const auto &arg : kw_args)
    {
      if (arg.hidden)
        continue;
      max_kw_arg_width =
          std::max(max_kw_arg_width, arg.get_display_name().length());
    }

    if (m_allow_dynamic_keywords)
    {
      const auto dynamic_placeholder =
          (m_dynamic.keyword_type == ArgType_Multiple) ? "--<" + m_dynamic.placeholder + "> " + "<value...>"
                                                       : "--<" + m_dynamic.placeholder + "> " + "<value>";
      max_kw_arg_width =
          std::max(max_kw_arg_width, std::strlen(dynamic_placeholder.c_str()));
    }

    size_t max_cmd_width = 0;
    for (const auto &cmd_pair : m_commands)
    {
      size_t current_cmd_width = cmd_pair.first.length();
      const std::vector<std::string> &aliases = cmd_pair.second->get_aliases();
      if (!aliases.empty())
      {
        current_cmd_width += 3; // for " (" and ")"
        for (size_t i = 0; i < aliases.size(); ++i)
        {
          current_cmd_width += aliases[i].length();
          if (i < aliases.size() - 1)
          {
            current_cmd_width += 2; // for ", "
          }
        }
      }
      max_cmd_width = std::max(max_cmd_width, current_cmd_width);
    }

    // add a buffer for spacing
    const size_t width_buffer = 2;
    max_pos_arg_width += width_buffer;
    max_kw_arg_width += width_buffer;
    max_cmd_width += width_buffer;

    std::string full_command_path =
        command_path_prefix.empty() ? m_name : command_path_prefix + m_name;
    if (!command_path_prefix.empty() &&
        command_path_prefix[command_path_prefix.length() - 1] != ' ')
    {
      full_command_path = command_path_prefix + " " + m_name;
    }
    else
    {
      full_command_path = command_path_prefix + m_name;
    }

    os << "Usage: " << full_command_path;
    // collect display names for positional args
    std::string positional_usage;
    for (const auto &pos_arg : pos_args)
    {
      positional_usage += " ";
      positional_usage += (pos_arg.required ? "<" : "[");
      positional_usage += pos_arg.name;
      positional_usage += (pos_arg.required ? ">" : "]");
      if (pos_arg.type == ArgType_Multiple)
        positional_usage += "...";
    }

    if (!m_commands.empty())
      os << " <command>";

    os << positional_usage;

    if (!kw_args.empty() || m_allow_dynamic_keywords)
      os << " [options]";

    if (m_allow_passthrough)
      os << " [-- <passthrough_args>...]";

    os << "\n\n";

    if (!m_help.empty())
    {
      os << m_help << "\n\n";
    }

    // info about positional arguments
    if (!pos_args.empty())
    {
      os << "Arguments:\n";
      for (const auto &arg : pos_args)
      {
        os << "  " << std::left << std::setw(max_pos_arg_width) << arg.name
           << arg.help;
        // check default value is not none
        if (!arg.default_value.is_none())
        {
          os << " (default: ";
          arg.default_value.print(os);
          os << ")";
        }
        if (!arg.required)
          os << " [optional]";
        os << "\n";
      }
      os << "\n";
    }

    // info about keyword arguments
    if (!kw_args.empty() || m_allow_dynamic_keywords)
    {
      os << "Options:\n";
      for (const auto &arg : kw_args)
      {
        if (arg.hidden)
          continue;
        os << "  " << std::left << std::setw(max_kw_arg_width)
           << arg.get_display_name() << arg.help;
        if (!arg.default_value.is_none())
        {
          // only show non-default bools (true) or non-bool defaults
          const bool *b_val = arg.default_value.get_bool();
          if (!b_val ||
              *b_val == true)
          { // show default if it's true, or if not a bool
            os << " (default: ";
            arg.default_value.print(os);
            os << ")";
          }
        }
        if (arg.required)
          os << " [required]";
        os << "\n";
      }
      if (m_allow_dynamic_keywords)
      {
        const auto dynamic_placeholder =
            (m_dynamic.keyword_type == ArgType_Multiple) ? "--<" + m_dynamic.placeholder + "> " + "<values...>"
                                                         : "--<" + m_dynamic.placeholder + "> " + "<value>";
        os << "  " << std::left << std::setw(max_kw_arg_width)
           << dynamic_placeholder
           << m_dynamic.description << "\n";
      }
      os << "\n";
    }

    // passthrough arguments info
    if (m_allow_passthrough)
    {
      os << "Passthrough:\n";
      os << "  --                    " << m_passthrough_description << "\n";
      os << "                        All arguments after -- are passed through without parsing.\n\n";
    }

    // available subcommands
    if (!m_commands.empty())
    {
      os << "Commands:\n";
      for (const auto &cmd_pair : m_commands)
      {
        std::string cmd_display = cmd_pair.first;
        const std::vector<std::string> &aliases = cmd_pair.second->get_aliases();
        if (!aliases.empty())
        {
          cmd_display += " (";
          for (size_t i = 0; i < aliases.size(); ++i)
          {
            cmd_display += aliases[i];
            if (i < aliases.size() - 1)
            {
              cmd_display += ", ";
            }
          }
          cmd_display += ")";
        }
        os << "  " << std::left << std::setw(max_cmd_width) << cmd_display
           << cmd_pair.second->get_help() << "\n";
      }
      os << "\nRun '" << full_command_path
         << " <command> --help' for more information on a command.\n";
    }

    if (!m_examples.empty())
    {
      os << "Examples:\n\n";
      for (auto it = m_examples.begin(); it != m_examples.end(); ++it)
      {
        os << "   - " << (*it).title << ":\n\n";
        os << "      $ " << (*it).command << "\n\n";
      }
    }
  }

  void set_name(const std::string &new_name)
  {
    m_name = new_name;
  }

private:
  // private copy constructor and assignment operator to prevent copying
  Command(const Command &);
  Command &operator=(const Command &);

  std::string m_name;
  std::string m_help;
  std::vector<ArgumentDef> m_args;
  std::map<std::string, command_ptr> m_commands;
  std::vector<std::string> m_aliases;
  std::vector<CmdExample> m_examples;

  struct DynamicKw
  {
    ArgType keyword_type;
    std::string description;
    std::string placeholder;
  };
  DynamicKw m_dynamic;

public:
  CommandHandler m_handler;

private:
  bool m_allow_dynamic_keywords;
  bool m_allow_passthrough;
  bool m_privileged;
  bool m_json_registered = true;
  bool m_json_capture = true;
  bool m_stdout_is_payload = false;
  std::string m_passthrough_description;
  std::shared_ptr<std::vector<PreCommandHook>> m_pre_hooks;

  // helper to execute pre-command hooks
  bool execute_pre_hooks(bool is_help_request, const char *context_msg) const
  {
    if (m_pre_hooks)
    {
      for (size_t i = 0; i < m_pre_hooks->size(); ++i)
      {
        const auto &hook = (*m_pre_hooks)[i];
        if (!hook(*this, is_help_request))
        {
          ZLOG_TRACE("Pre-command hook aborted execution for %s '%s'", context_msg, m_name.c_str());
          return false;
        }
      }
    }
    return true;
  }

  // helper to check if the token at the given index is a flag/option token
  bool is_flag_token(const std::vector<lexer::Token> &tokens,
                     size_t index) const
  {
    if (index >= tokens.size())
      return false;
    lexer::TokenKind kind = tokens[index].get_kind();
    return kind == lexer::TokFlagShort || kind == lexer::TokFlagLong;
  }

  // find keyword argument definition by flag name
  const ArgumentDef *find_keyword_arg(
      const std::string &flag_name_value, // name part only (e.g., "f", "force")
      bool is_short_flag_kind) const
  {
    for (const auto &arg : m_args)
    {
      if (arg.positional)
        continue;
      if (arg.name == flag_name_value)
      {
        return &arg;
      }
      for (size_t i = 0; i < arg.aliases.size(); ++i)
      {
        const std::string &alias = arg.aliases[i];
        if (is_short_flag_kind)
        {
          // match "-f" for short flag "f"
          if (alias.length() == 2 && alias[0] == '-' &&
              alias.substr(1) == flag_name_value)
          {
            return &arg;
          }
        }
        else
        {
          // match "--force" for long flag "force"
          if (alias.length() > 2 && alias.substr(0, 2) == "--" &&
              alias.substr(2) == flag_name_value)
          {
            return &arg;
          }
        }
      }
    }
    return nullptr;
  }

  // helper to parse a single token into an ArgValue based on expected type
  // returns ArgValue (which manages memory for string/vector)
  std::string stringify_token_value(const lexer::Token &token) const
  {
    std::ostringstream ss;
    token.print(ss);
    return ss.str();
  }

  // Returns the numeric literal token as a string or its native type,
  // depending on whether string coercion is preferred.
  template <typename T>
  ArgValue coerce_numeric_to_str(const lexer::Token &token, T native_val,
                                 bool prefer_string) const
  {
    if (prefer_string)
      return ArgValue(stringify_token_value(token));
    return ArgValue(native_val);
  }

  ArgValue parse_token_value(const lexer::Token &token,
                             ArgType expected_type,
                             bool coerce_as_string = false) const
  {
    lexer::TokenKind kind = token.get_kind();

    // Coerce identifier/strlit values for flags to boolean values
    if (expected_type == ArgType_Flag && (kind == lexer::TokId || kind == lexer::TokStrLit))
    {
      std::string s_val = (kind == lexer::TokId) ? token.get_id_value()
                                                 : token.get_str_lit_value();
      return ArgValue(s_val == "true");
    }

    // allow broader range of tokens to be interpreted as strings if expected
    bool expect_string =
        (expected_type == ArgType_Single || expected_type == ArgType_Multiple);
    bool prefer_string = (expected_type == ArgType_Multiple) || coerce_as_string;

    switch (kind)
    {
    case lexer::TokIntLit:
      if (expect_string)
        return coerce_numeric_to_str(token, token.get_int_value(), prefer_string);
      break;
    case lexer::TokFloatLit:
      if (expect_string)
        return coerce_numeric_to_str(token, token.get_float_value(), prefer_string);
      break;
    case lexer::TokTrue:
      return expect_string ? ArgValue("true") : ArgValue(true);
    case lexer::TokFalse:
      return expect_string ? ArgValue("false") : ArgValue(false);
    case lexer::TokStrLit:
      if (expect_string)
        return ArgValue(token.get_str_lit_value());
      break;
    case lexer::TokId:
      if (expect_string)
        return ArgValue(token.get_id_value());
      break;
    case lexer::TokTimes:
      if (expect_string)
        return ArgValue("*");
      break;
    default:
      break;
    }

    return ArgValue();
  }

  // helper to add the standard help argument
  void ensure_help_argument()
  {
    bool help_exists = false;
    for (const auto &arg : m_args)
    {
      if (arg.name == "help")
      {
        help_exists = true;
        break;
      }
    }
    if (!help_exists)
    {
      std::vector<std::string> help_aliases;
      help_aliases.reserve(2);
      help_aliases.push_back("-h");
      help_aliases.push_back("--help");
      m_args.push_back(
          ArgumentDef("help", help_aliases, "show this help message and exit",
                      ArgType_Flag, false, false, ArgValue(false), true));
    }
  }

  // helper to add the standard JSON output argument
  void ensure_json_argument()
  {
    if (!m_json_registered)
    {
      return;
    }
    for (const auto &arg : m_args)
    {
      if (arg.name == "json")
      {
        return;
      }
      for (const auto &alias : arg.aliases)
      {
        if (alias == "--json" || alias == "--rfj")
        {
          return;
        }
      }
    }

    ArgumentDef json_arg("json", make_aliases("--json", "--rfj"),
                         "print the result as a single line of JSON",
                         ArgType_Flag, false, false, ArgValue(false), false);
    json_arg.is_json_flag = true;
    m_args.push_back(json_arg);
  }

  // Declare that this command's stdout carries its payload rather than a
  // human-readable rendering of fields it also reports through
  // set_object() -- the CLI equivalent of the server's read_stdout()
  // transform. Without it, captured stdout is dropped for any handler that
  // already produced fields of its own.
public:
  Command &mark_stdout_as_payload()
  {
    m_stdout_is_payload = true;
    return *this;
  }

  bool stdout_is_payload() const
  {
    return m_stdout_is_payload;
  }

  // Refuse --json entirely: the argument is not accepted here, and an
  // inherited --json from a parent command will not capture this handler
  // either. For commands that must never serialize a result at all.
  Command &disable_json_output()
  {
    m_json_registered = false;
    m_json_capture = false;
    for (auto it = m_args.begin(); it != m_args.end();)
    {
      it = it->is_json_flag ? m_args.erase(it) : it + 1;
    }
    return *this;
  }

  // Keep accepting --json -- so it parses here and still reaches
  // subcommands -- but never capture this command's own handler. For the
  // root command, whose handler prints help or enters the REPL, and for
  // `server`, which owns stdout for the life of the process.
  Command &disable_json_capture()
  {
    m_json_capture = false;
    return *this;
  }

private:
  // check if the command has a specific alias
  bool has_alias(const std::string &alias) const
  {
    return std::find(m_aliases.begin(), m_aliases.end(), alias) !=
           m_aliases.end();
  }

public:
  void register_argument(const ArgumentDef &arg)
  {
    // prevent adding another argument named "help" or starting with "no-"
    if (arg.name == "help")
    {
      throw std::invalid_argument(
          "argument name 'help' is reserved for the automatic help flag.");
    }
    if (arg.name.rfind("no-", 0) == 0)
    {
      throw std::invalid_argument(
          "argument name cannot start with 'no-'. this prefix is reserved for "
          "automatic negation flags.");
    }
    for (size_t i = 0; i < arg.aliases.size(); ++i)
    {
      if (!arg.aliases[i].empty() && arg.aliases[i].find("--no-") == 0)
      {
        throw std::invalid_argument(
            "alias cannot start with '--no-'. this prefix is reserved for "
            "automatic negation flags.");
      }
    }

    // if it's a flag and the default value is still the initial avk_none,
    // set the actual default to false. otherwise, use the provided default.
    ArgValue final_default_value = arg.default_value;
    if (arg.type == ArgType_Flag && arg.default_value.is_none())
    {
      final_default_value = ArgValue(false);
    }

    // ensure the new argument name is unique
    for (const auto &existing : m_args)
    {
      if (existing.name == arg.name)
      {
        throw std::invalid_argument("argument name '" + arg.name +
                                    "' already exists.");
      }
    }
    // ensure aliases are unique across all args
    for (const auto &existing_arg : m_args)
    {
      for (size_t i = 0; i < arg.aliases.size(); ++i)
      {
        for (size_t j = 0; j < existing_arg.aliases.size(); ++j)
        {
          if (!arg.aliases[i].empty() &&
              arg.aliases[i] == existing_arg.aliases[j])
          {
            throw std::invalid_argument("alias '" + arg.aliases[i] +
                                        "' already exists.");
          }
        }
      }
    }

    // add the primary argument definition
    ArgumentDef final_arg = arg;
    final_arg.default_value = final_default_value;
    m_args.push_back(final_arg);

    // check if we need to add an automatic --no-<flag>
    const bool *default_bool = final_default_value.get_bool();
    // only add --no-<flag> for boolean flags with a true default
    if (arg.type == ArgType_Flag && default_bool && *default_bool == true)
    {
      std::string no_flag_name = "no-" + arg.name;
      std::string no_flag_help = "disable the --" + arg.name + " flag.";

      // ensure the generated --no- name/alias doesn't conflict
      for (const auto &existing : m_args)
      {
        if (existing.name == no_flag_name)
        {
          throw std::invalid_argument("automatic negation flag name '" +
                                      no_flag_name +
                                      "' conflicts with an existing argument.");
        }
      }

      // add the negation argument definition
      m_args.push_back(ArgumentDef(no_flag_name, make_aliases(), no_flag_help,
                                   ArgType_Flag, false, false, ArgValue(false),
                                   false));
    }
  }
};

// stores the outcome of the parsing process
class ParseResult
{
public:
  enum ParserStatus
  {
    ParserStatus_Success,
    ParserStatus_HelpRequested,
    ParserStatus_ParseError
  };

  ParserStatus status = ParserStatus_Success;
  int exit_code = 0;         // exit code returned by handler or set by parser
  std::string error_message; // error message if status is parse_error
  std::string command_path;  // full path of the executed command (e.g., "git
                             // remote add")

  // map storing parsed argument values (name -> value)
  std::map<std::string, ArgValue> m_values;
  std::map<std::string, ArgValue> m_dynamic_values;

  // passthrough arguments (after -- delimiter)
  std::vector<std::string> m_passthrough_args;

  // pointer to the command definition for default value lookup
  const Command *m_command = nullptr;

  // --json state. Collected here rather than printed by Command::parse so
  // that ArgumentParser::parse can emit the envelope from a single place,
  // covering the handler path and every parser-level error return alike.
  bool json_requested = false;
  bool json_suppressed = false;
  ast::Node json_data;
  std::string json_stdout;
  std::string json_stderr;
  bool json_stdout_is_payload = false;
  std::string json_output;

  ParseResult() = default;

  // check if an arg was provided
  bool has(const std::string &name) const
  {
    return m_values.find(name) != m_values.end();
  }

  bool has_dynamic(const std::string &name) const
  {
    return m_dynamic_values.find(name) != m_dynamic_values.end();
  }

  // get a pointer to the value, or nullptr if missing/wrong type
  template <typename T>
  const T *get(const std::string &name) const
  {
    auto it = m_values.find(name);
    if (it == m_values.end())
      return nullptr;
    return plugin::ArgGetter<T>::get(it->second);
  }

  const ArgValue *get_dynamic(const std::string &name) const
  {
    auto it = m_dynamic_values.find(name);
    if (it == m_dynamic_values.end())
      return nullptr;
    return &it->second;
  }

  const std::map<std::string, ArgValue> &get_dynamic_values() const
  {
    return m_dynamic_values;
  }

  // check if passthrough arguments were provided (after --)
  bool has_passthrough() const
  {
    return !m_passthrough_args.empty();
  }

  // get the passthrough arguments (arguments after -- delimiter)
  const std::vector<std::string> &get_passthrough_args() const
  {
    return m_passthrough_args;
  }

  // get the value, or a default if missing/wrong type
  template <typename T>
  T get_value(const std::string &name, const T &default_value = T()) const
  {
    const T *ptr = get<T>(name);
    if (ptr)
      return *ptr;

    // if not found in parsed values, check for command's default
    if (m_command)
    {
      const std::vector<ArgumentDef> &defs = m_command->get_args();
      for (size_t i = 0; i < defs.size(); ++i)
      {
        if (defs[i].name == name)
        {
          const T *def_ptr = plugin::ArgGetter<T>::get(defs[i].default_value);
          if (def_ptr)
            return *def_ptr;
          break; // found the arg def, no need to look further
        }
      }
    }

    return default_value;
  }
};

// Specialization for bool
template <>
inline bool ParseResult::get_value<bool>(const std::string &name,
                                         const bool &default_value) const
{
  const std::string no_name = "no-" + name;
  if (has(no_name))
  {
    // The presence of the --no-flag means we should return false.
    return false;
  }

  // Check for the positive flag.
  const bool *ptr = get<bool>(name);
  if (ptr)
    return *ptr;

  // if not found in parsed values, check for command's default
  if (m_command)
  {
    const std::vector<ArgumentDef> &defs = m_command->get_args();
    for (size_t i = 0; i < defs.size(); ++i)
    {
      if (defs[i].name == name)
      {
        const bool *def_ptr = plugin::ArgGetter<bool>::get(defs[i].default_value);
        if (def_ptr)
          return *def_ptr;
        break; // found the arg def, no need to look further
      }
    }
  }

  return default_value;
}

inline ParseResult
Command::parse(const std::vector<lexer::Token> &tokens,
               size_t &current_token_index,
               const std::string &command_path_prefix,
               bool json_inherited) const
{
  ZLOG_TRACE("Command::parse entry: command='%s', prefix='%s', tokens=%zu, current_index=%zu",
             m_name.c_str(), command_path_prefix.c_str(), tokens.size(), current_token_index);

  ParseResult result;
  result.m_command = this;
  result.command_path = command_path_prefix + m_name;
  // initialize exit code for potential errors or help requests
  result.exit_code = 0; // default to 0 for success before handler runs

  std::map<std::string, bool> args_seen;
  size_t current_positional_arg_index = 0;

  std::vector<ArgumentDef> pos_args;
  for (const auto &arg : m_args)
  {
    if (arg.is_help_flag || arg.is_json_flag)
    {
      continue;
    }
    result.m_values[arg.name] = arg.default_value;

    if (arg.positional)
    {
      pos_args.push_back(arg);
    }
  }

  while (current_token_index < tokens.size())
  {
    const lexer::Token &token = tokens[current_token_index];
    lexer::TokenKind kind = token.get_kind();

    ZLOG_TRACE("Processing token[%zu]: kind=%d, current_pos_arg_index=%zu",
               current_token_index, (int)kind, current_positional_arg_index);

    // check for passthrough delimiter (--)
    if (kind == lexer::TokDoubleMinus)
    {
      if (!m_allow_passthrough)
      {
        result.status = ParseResult::ParserStatus_ParseError;
        result.error_message = "unexpected delimiter '--'. This command does not accept passthrough arguments.";
        std::cerr << "Error: " << result.error_message << "\n\n";
        generate_help(std::cerr, command_path_prefix);
        result.exit_code = 1;
        return result;
      }

      ZLOG_TRACE("Passthrough delimiter found, collecting remaining tokens");
      current_token_index++; // consume the -- token

      // collect all remaining tokens as passthrough arguments
      while (current_token_index < tokens.size())
      {
        const lexer::Token &pass_token = tokens[current_token_index];
        std::ostringstream ss;
        pass_token.print(ss);
        result.m_passthrough_args.push_back(ss.str());
        current_token_index++;
      }
      break; // exit the main parsing loop
    }

    // check for keyword arguments/flags (TokFlagShort, TokFlagLong)
    if (kind == lexer::TokFlagShort || kind == lexer::TokFlagLong)
    {
      std::string flag_name_str =
          token.get_id_value(); // get name without dashes
      bool is_short_flag_kind = (kind == lexer::TokFlagShort);

      ZLOG_TRACE("Processing flag: '%s', short=%s",
                 flag_name_str.c_str(), is_short_flag_kind ? "true" : "false");

      if (is_short_flag_kind && flag_name_str.length() > 1)
      {
        ZLOG_TRACE("Processing combined short flags: '%s'", flag_name_str.c_str());
        current_token_index++;

        for (size_t i = 0; i < flag_name_str.length(); ++i)
        {
          std::string single_flag_char(1, flag_name_str[i]);
          const ArgumentDef *matched_arg =
              find_keyword_arg(single_flag_char, true);

          if (!matched_arg)
          {
            result.status = ParseResult::ParserStatus_ParseError;
            result.error_message = "unknown option in combined flags: -";
            result.error_message += single_flag_char;
            std::cerr << "Error: " << result.error_message << "\n\n";
            generate_help(std::cerr, command_path_prefix);
            result.exit_code = 1;
            return result;
          }

          if (matched_arg->is_help_flag)
          {
            generate_help(std::cout, command_path_prefix);
            result.status = ParseResult::ParserStatus_HelpRequested;
            result.exit_code = 0; // help request is a successful exit
            return result;
          }

          // ensure combined flags are actually boolean flags
          if (matched_arg->type != ArgType_Flag)
          {
            result.status = ParseResult::ParserStatus_ParseError;
            result.error_message = "option -" + single_flag_char +
                                   " requires a value and cannot be combined.";
            std::cerr << "Error: " << result.error_message << "\n\n";
            generate_help(std::cerr, command_path_prefix);
            result.exit_code = 1;
            return result;
          }

          // mark as seen and set value to true
          args_seen[matched_arg->name] = true;
          result.m_values[matched_arg->name] = ArgValue(true);
        }
        continue;
      }

      const ArgumentDef *matched_arg =
          find_keyword_arg(flag_name_str, is_short_flag_kind);

      ZLOG_TRACE("Flag lookup result: '%s' -> %s",
                 flag_name_str.c_str(), matched_arg ? "found" : "not found");

      if (!matched_arg)
      {
        if (!is_short_flag_kind && m_allow_dynamic_keywords)
        {
          current_token_index++; // consume flag token

          ArgType dynamic_type = m_dynamic.keyword_type;

          if (dynamic_type == ArgType_Single)
          {
            if (current_token_index >= tokens.size() ||
                is_flag_token(tokens, current_token_index))
            {
              result.status = ParseResult::ParserStatus_ParseError;
              result.error_message = "option --" + flag_name_str +
                                     " requires a value.";
              std::cerr << "Error: " << result.error_message << "\n\n";
              generate_help(std::cerr, command_path_prefix);
              result.exit_code = 1;
              return result;
            }

            const lexer::Token &value_token = tokens[current_token_index];
            ArgValue parsed_value =
                parse_token_value(value_token, ArgType_Single);
            if (parsed_value.is_none())
            {
              result.status = ParseResult::ParserStatus_ParseError;
              result.error_message =
                  "invalid value for option --" + flag_name_str;
              std::cerr << "Error: " << result.error_message << "\n\n";
              generate_help(std::cerr, command_path_prefix);
              result.exit_code = 1;
              return result;
            }

            parsed_value.set_dynamic(true);
            result.m_dynamic_values[flag_name_str] = parsed_value;
            current_token_index++;
            continue;
          }

          if (dynamic_type == ArgType_Multiple)
          {
            if (current_token_index >= tokens.size() ||
                is_flag_token(tokens, current_token_index))
            {
              result.status = ParseResult::ParserStatus_ParseError;
              result.error_message = "option --" + flag_name_str +
                                     " requires at least one value.";
              std::cerr << "Error: " << result.error_message << "\n\n";
              generate_help(std::cerr, command_path_prefix);
              result.exit_code = 1;
              return result;
            }

            std::vector<std::string> values;
            while (current_token_index < tokens.size() &&
                   !is_flag_token(tokens, current_token_index))
            {
              const lexer::Token &value_token = tokens[current_token_index];
              ArgValue parsed_value =
                  parse_token_value(value_token, ArgType_Multiple);
              const std::string *value_str = parsed_value.get_string();
              if (!value_str)
                break;
              values.push_back(*value_str);
              current_token_index++;
            }

            if (values.empty())
            {
              result.status = ParseResult::ParserStatus_ParseError;
              result.error_message =
                  "invalid value for option --" + flag_name_str;
              std::cerr << "Error: " << result.error_message << "\n\n";
              generate_help(std::cerr, command_path_prefix);
              result.exit_code = 1;
              return result;
            }

            ArgValue dynamic_val(values);
            dynamic_val.set_dynamic(true);
            result.m_dynamic_values[flag_name_str] = dynamic_val;
            continue;
          }

          // fallback in case of unexpected configuration
          result.status = ParseResult::ParserStatus_ParseError;
          result.error_message =
              "internal error: unsupported dynamic keyword configuration";
          std::cerr << "Error: " << result.error_message << "\n\n";
          generate_help(std::cerr, command_path_prefix);
          result.exit_code = 1;
          return result;
        }

        result.status = ParseResult::ParserStatus_ParseError;
        result.error_message = "unknown option: ";
        result.error_message += (is_short_flag_kind ? "-" : "--");
        result.error_message += flag_name_str;

        // Suggest similar option
        size_t best_dist = (size_t)-1;
        std::string best_match;
        for (size_t i = 0; i < m_args.size(); ++i)
        {
          const ArgumentDef &arg = m_args[i];
          if (arg.positional)
            continue;
          // Check canonical name
          size_t dist = parser::levenshtein_distance(flag_name_str, arg.name);
          if (dist < best_dist)
          {
            best_dist = dist;
            best_match = "--" + arg.name;
          }
          // Check aliases
          for (size_t j = 0; j < arg.aliases.size(); ++j)
          {
            std::string alias = arg.aliases[j];
            // Remove leading dashes for comparison
            std::string alias_cmp = alias;
            while (!alias_cmp.empty() && alias_cmp[0] == '-')
              alias_cmp = alias_cmp.substr(1);
            dist = parser::levenshtein_distance(flag_name_str, alias_cmp);
            if (dist < best_dist)
            {
              best_dist = dist;
              best_match = alias;
            }
          }
        }
        std::cerr << "Error: " << result.error_message << "\n";

        if (best_dist != (size_t)-1 && best_dist <= 2)
        {
          std::cerr << "Did you mean '" << best_match << "'?\n\n";
        }
        else
        {
          std::cerr << "\n";
        }

        generate_help(std::cerr, command_path_prefix);
        result.exit_code = 1;
        return result;
      }

      // if this is the specifically marked help flag, handle it immediately.
      if (matched_arg->is_help_flag)
      {
        ZLOG_TRACE("Help flag detected, showing help and exiting");

        // Execute pre-command hooks before the help runs
        if (!execute_pre_hooks(true, "help command"))
        {
          result.exit_code = 1;
          return result;
        }

        generate_help(std::cout, command_path_prefix);
        result.status = ParseResult::ParserStatus_HelpRequested;
        result.exit_code = 0; // help request is a successful exit
        return result;
      }

      if (matched_arg->is_json_flag)
      {
        current_token_index++; // consume only the flag token, never a value
        args_seen[matched_arg->name] = true;
        result.m_values[matched_arg->name] = ArgValue(true);
        continue;
      }

      current_token_index++; // consume the flag token itself
      args_seen[matched_arg->name] = true;

      ZLOG_TRACE("Processing argument value for '%s', type=%d",
                 matched_arg->name.c_str(), (int)matched_arg->type);

      // handle argument value based on type
      if (matched_arg->type == ArgType_Flag)
      {
        // default to true
        ArgValue flag_value(true);

        // check for an optional value (e.g., --flag false)
        if (current_token_index < tokens.size() &&
            !is_flag_token(tokens, current_token_index))
        {
          const lexer::Token &value_token = tokens[current_token_index];
          ArgValue parsed_value =
              parse_token_value(value_token, ArgType_Flag);

          if (!parsed_value.is_none())
          {
            flag_value = parsed_value;
            current_token_index++; // consume the value token if one exists
          }
        }
        result.m_values[matched_arg->name] = flag_value;
      }
      else
      {
        // check if next token exists and is not another flag
        if (current_token_index >= tokens.size() ||
            tokens[current_token_index].get_kind() == lexer::TokFlagShort ||
            tokens[current_token_index].get_kind() == lexer::TokFlagLong)
        {
          result.status = ParseResult::ParserStatus_ParseError;
          result.error_message = "option " + matched_arg->get_display_name() +
                                 " requires a value.";
          std::cerr << "Error: " << result.error_message << "\n\n";
          generate_help(std::cerr, command_path_prefix);
          result.exit_code = 1;
          return result;
        }

        const lexer::Token &value_token = tokens[current_token_index];
        ArgValue parsed_value =
            parse_token_value(value_token, matched_arg->type,
                              matched_arg->coerce_as_string);
        if (parsed_value.is_none())
        {
          result.status = ParseResult::ParserStatus_ParseError;
          result.error_message =
              "invalid value for option " + matched_arg->get_display_name();
          std::cerr << "Error: " << result.error_message << "\n\n";
          generate_help(std::cerr, command_path_prefix);
          result.exit_code = 1;
          return result;
        }

        if (matched_arg->type == ArgType_Single)
        {
          result.m_values[matched_arg->name] = parsed_value;
          current_token_index++;
        }
        else if (matched_arg->type == ArgType_Multiple)
        {
          // ensure value type is string for multiple
          if (!parsed_value.is_string())
          {
            result.status = ParseResult::ParserStatus_ParseError;
            result.error_message =
                "internal error: expected string value for multiple option " +
                matched_arg->name;
            std::cerr << "Error: " << result.error_message << "\n\n";
            generate_help(std::cerr, command_path_prefix);
            result.exit_code = 1;
            return result;
          }

          auto map_it = result.m_values.find(matched_arg->name);
          if (map_it == result.m_values.end() ||
              !map_it->second.is_string_vector() || map_it->second.is_none())
          {
            result.m_values[matched_arg->name] =
                ArgValue(std::vector<std::string>());
            map_it = result.m_values.find(matched_arg->name);
          }

          std::vector<std::string> *vec =
              map_it->second.get_string_vector_mutable();
          if (vec)
          {
            const std::string *first_val_str_ptr = parsed_value.get_string();
            if (first_val_str_ptr)
            {
              vec->push_back(*first_val_str_ptr);
            }

            current_token_index++;

            // keep consuming values until next flag or end
            while (
                current_token_index < tokens.size() &&
                tokens[current_token_index].get_kind() != lexer::TokFlagShort &&
                tokens[current_token_index].get_kind() != lexer::TokFlagLong)
            {
              const lexer::Token &next_value_token =
                  tokens[current_token_index];
              ArgValue next_parsed_value =
                  parse_token_value(next_value_token, ArgType_Multiple,
                                    matched_arg->coerce_as_string);
              const std::string *next_val_str_ptr =
                  next_parsed_value.get_string();
              if (next_val_str_ptr)
              {
                vec->push_back(*next_val_str_ptr);
                current_token_index++;
              }
              else
              {
                break;
              }
            }
          }
          else
          {
            result.status = ParseResult::ParserStatus_ParseError;
            result.error_message =
                "internal error accessing vector for multiple values for " +
                matched_arg->name;
            std::cerr << "Error: " << result.error_message << "\n\n";
            generate_help(std::cerr, command_path_prefix);
            result.exit_code = 1;
            return result;
          }
        }
      }
      continue;
    }

    // check for subcommand
    if (kind == lexer::TokId)
    {
      std::string potential_subcommand_or_alias = token.get_id_value();
      ZLOG_TRACE("Checking for subcommand: '%s'", potential_subcommand_or_alias.c_str());
      command_ptr matched_subcommand;

      // first, check if it's a direct name match
      auto sub_it = m_commands.find(potential_subcommand_or_alias);
      if (sub_it != m_commands.end())
      {
        matched_subcommand = sub_it->second;
      }
      else
      {
        // if not a direct name match, check aliases
        for (const auto &pair : m_commands)
        {
          if (pair.second->has_alias(potential_subcommand_or_alias))
          {
            // prevent aliasing to multiple commands
            if (matched_subcommand)
            {
              result.status = ParseResult::ParserStatus_ParseError;
              result.error_message = "ambiguous alias '" +
                                     potential_subcommand_or_alias +
                                     "' matches multiple subcommands.";
              std::cerr << "Error: " << result.error_message << "\n\n";
              generate_help(std::cerr, command_path_prefix);
              result.exit_code = 1;
              return result;
            }
            matched_subcommand = pair.second;
          }
        }
      }

      // if a subcommand (by name or alias) was found
      if (matched_subcommand)
      {
        ZLOG_TRACE("Subcommand '%s' matched, delegating parse", potential_subcommand_or_alias.c_str());
        current_token_index++; // consume the subcommand/alias token
        ParseResult sub_result = matched_subcommand->parse(
            tokens, current_token_index, result.command_path + " ",
            json_inherited || result.get_value<bool>("json", false));

        // propagate the result (success, error, or help request) from the
        // subcommand.
        return sub_result;
      }
      else if (!m_commands.empty() && current_positional_arg_index == 0 && !m_handler)
      {
        // Only treat as unknown subcommand if we haven't started processing positional args
        // AND this command has no handler (is purely a command group)
        // Suggest similar subcommand/group
        size_t best_dist = (size_t)-1;
        std::string best_match;
        for (const auto &cmd_pair : m_commands)
        {
          // Check subcommand name
          size_t dist = parser::levenshtein_distance(potential_subcommand_or_alias, cmd_pair.first);
          if (dist < best_dist)
          {
            best_dist = dist;
            best_match = cmd_pair.first;
          }
          // Check aliases
          const std::vector<std::string> &aliases = cmd_pair.second->get_aliases();
          for (size_t j = 0; j < aliases.size(); ++j)
          {
            dist = parser::levenshtein_distance(potential_subcommand_or_alias, aliases[j]);
            if (dist < best_dist)
            {
              best_dist = dist;
              best_match = aliases[j];
            }
          }
        }
        result.status = ParseResult::ParserStatus_ParseError;
        result.error_message = "unknown command or group: " + potential_subcommand_or_alias;
        std::cerr << "Error: " << result.error_message << "\n";

        if (best_dist != (size_t)-1 && best_dist <= 2)
        {
          std::cerr << "Did you mean '" << best_match << "'?\n\n";
        }
        else
        {
          std::cerr << "\n";
        }
        generate_help(std::cerr, command_path_prefix);
        result.exit_code = 1;
        return result;
      }
      // If we reach here and have subcommands but current_positional_arg_index > 0,
      // fall through to positional argument processing
    }

    // if not a flag/option or subcommand, treat as positional argument
    if (current_positional_arg_index < pos_args.size())
    {
      const ArgumentDef &pos_arg_def = pos_args[current_positional_arg_index];
      ZLOG_TRACE("Processing positional argument[%zu]: '%s'",
                 current_positional_arg_index, pos_arg_def.name.c_str());
      ArgValue parsed_value = parse_token_value(token, pos_arg_def.type,
                                                pos_arg_def.coerce_as_string);

      if (parsed_value.is_none())
      {
        result.status = ParseResult::ParserStatus_ParseError;
        result.error_message =
            "invalid value for positional argument '" + pos_arg_def.name + "'";
        std::cerr << "Error: " << result.error_message << "\n\n";
        generate_help(std::cerr, command_path_prefix);
        result.exit_code = 1;
        return result;
      }

      if (pos_arg_def.type == ArgType_Single)
      {
        result.m_values[pos_arg_def.name] = parsed_value;
        current_token_index++;
        current_positional_arg_index++;
      }
      else if (pos_arg_def.type == ArgType_Multiple)
      {
        // ensure value type is string for multiple positional
        if (!parsed_value.is_string())
        {
          result.status = ParseResult::ParserStatus_ParseError;
          result.error_message = "internal error: expected string value for "
                                 "multiple positional argument " +
                                 pos_arg_def.name;
          std::cerr << "Error: " << result.error_message << "\n\n";
          generate_help(std::cerr, command_path_prefix);
          result.exit_code = 1;
          return result;
        }

        std::vector<std::string> values;
        // add the first value (must be string)
        const std::string *first_val_str_ptr = parsed_value.get_string();
        if (first_val_str_ptr)
        { // should always be true
          values.push_back(*first_val_str_ptr);
        }

        current_token_index++;
        while (current_token_index < tokens.size() &&
               !is_flag_token(
                   tokens, current_token_index) /* && !is_subcommand(...) */)
        {
          // todo: add is_subcommand check more robustly?
          // currently relies on subcommand check earlier in loop
          const lexer::Token &next_value_token = tokens[current_token_index];
          // parse subsequent as strings
          ArgValue next_parsed_value =
              parse_token_value(next_value_token, ArgType_Multiple,
                                pos_arg_def.coerce_as_string);
          const std::string *next_val_str_ptr = next_parsed_value.get_string();
          if (next_val_str_ptr)
          {
            values.push_back(*next_val_str_ptr);
            current_token_index++;
          }
          else
          {
            break;
          }
        }
        // add the vector to positional args map using ArgValue constructor
        result.m_values[pos_arg_def.name] = ArgValue(values);
        current_positional_arg_index++;
      }
    }
    else
    {
      result.status = ParseResult::ParserStatus_ParseError;
      std::stringstream ss;
      ss << "unexpected argument: ";
      token.print(ss);
      result.error_message = ss.str();
      // print error and help for this command to stderr
      std::cerr << "Error: " << result.error_message << "\n\n";
      generate_help(std::cerr, command_path_prefix);
      result.exit_code = 1;
      return result;
    }
  }

  // check for required arguments
  for (const auto &arg : m_args)
  {
    if (arg.positional || arg.is_help_flag)
      continue;

    if (arg.required && args_seen.find(arg.name) == args_seen.end())
    {
      result.status = ParseResult::ParserStatus_ParseError;
      result.error_message =
          "missing required option: " + arg.get_display_name();
      std::cerr << "Error: " << result.error_message << "\n\n";
      generate_help(std::cerr, command_path_prefix);
      result.exit_code = 1;
      return result;
    }
  }

  // check for required positional arguments that might not have been "seen"
  // but are still missing
  for (size_t i = current_positional_arg_index; i < pos_args.size(); ++i)
  {
    const ArgumentDef &pos_arg_def = pos_args[i];
    if (pos_arg_def.required)
    {
      result.status = ParseResult::ParserStatus_ParseError;
      result.error_message =
          "missing required positional argument: " + pos_arg_def.name;
      std::cerr << "Error: " << result.error_message << "\n\n";
      generate_help(std::cerr, command_path_prefix);
      result.exit_code = 1;
      return result;
    }
    else
    {
      // add default value for optional missing positional args
      result.m_values[pos_arg_def.name] = pos_arg_def.default_value;
    }
  }

  // check for conflicting arguments - aggregate conflicts for reporting
  std::set<std::pair<std::string, std::string>> conflict_pairs;
  for (const auto &arg : m_args)
  {
    if (!args_seen.count(arg.name) || arg.conflicts_with.empty())
      continue;

    for (size_t i = 0; i < arg.conflicts_with.size(); ++i)
    {
      const std::string &conflict = arg.conflicts_with[i];
      if (!args_seen.count(conflict))
        continue;

      std::string first = arg.name;
      std::string second = conflict;
      if (first == second)
        continue;
      if (second < first)
        std::swap(first, second);
      conflict_pairs.insert(std::make_pair(first, second));
    }
  }

  // if conflict pairs present, print them to the error stream and return w/ help text
  if (!conflict_pairs.empty())
  {
    result.status = ParseResult::ParserStatus_ParseError;
    std::stringstream ss;
    ss << "conflicting options provided: ";
    bool first_pair = true;
    for (const auto &conflict_pair : conflict_pairs)
    {
      if (!first_pair)
        ss << "; ";
      ss << "--" << conflict_pair.first << " conflicts with --" << conflict_pair.second;
      first_pair = false;
    }
    result.error_message = ss.str();
    std::cerr << "Error: " << result.error_message << "\n\n";
    generate_help(std::cerr, command_path_prefix);
    result.exit_code = 1;
    return result;
  }

  // only execute the handler if parsing was successful and no subcommand took
  // over.
  if (m_handler && result.status == ParseResult::ParserStatus_Success)
  {
    ZLOG_TRACE("Executing handler for command '%s'", m_name.c_str());

    // Execute pre-command hooks before the handler
    if (!execute_pre_hooks(false, "command"))
    {
      result.exit_code = 1;
      return result;
    }

    plugin::ArgumentMap invocation_args;
    for (const auto &kv : result.m_values)
    {
      invocation_args[kv.first] = kv.second;
    }
    for (const auto &kv : result.m_dynamic_values)
    {
      invocation_args[kv.first] = kv.second;
    }

    const bool json_wanted =
        json_inherited || result.get_value<bool>("json", false);
    const bool json_active = json_wanted && m_json_capture;
    if (json_wanted && !m_json_capture)
    {
      result.json_suppressed = true;
    }

    // Redirecting also flips Io::is_redirecting_output(), which handlers
    // already consult to suppress human-only decoration such as the
    // "etag: " and "size: " lines -- exactly what should stay out of JSON.
    std::stringstream json_stdout_buffer;
    std::stringstream json_stderr_buffer;
    plugin::ContextArgs context_args(result.command_path, invocation_args, result.m_passthrough_args,
                                     nullptr,
                                     json_active ? &json_stdout_buffer : nullptr,
                                     json_active ? &json_stderr_buffer : nullptr);
    plugin::InvocationContext context(context_args);
    result.exit_code = m_handler(context);
    ZLOG_TRACE("Handler returned exit code: %d", result.exit_code);

    if (json_active)
    {
      result.json_requested = true;
      result.json_data = context.get_object();
      result.json_stdout = json_stdout_buffer.str();
      result.json_stderr = json_stderr_buffer.str();
      result.json_stdout_is_payload = m_stdout_is_payload;
    }
  }

  // If this command is a group (has subcommands), has no handler, and parsing
  // succeeded, print help and set status to HelpRequested.
  if (!m_handler && !m_commands.empty() &&
      result.status == ParseResult::ParserStatus_Success)
  {
    ZLOG_TRACE("Command group with no handler, showing help");

    // Execute pre-command hooks before the help runs
    if (!execute_pre_hooks(true, "help command group"))
    {
      result.exit_code = 1;
      return result;
    }

    generate_help(std::cout, command_path_prefix);
    result.status = ParseResult::ParserStatus_HelpRequested;
    result.exit_code = 0;
  }

  ZLOG_TRACE("Command::parse exit: command='%s', status=%d, exit_code=%d",
             m_name.c_str(), (int)result.status, result.exit_code);
  return result;
}

class ArgumentParser
{
public:
  ArgumentParser() = default;
  explicit ArgumentParser(std::string prog_name, std::string description = "")
      : m_program_name(prog_name), m_program_desc(description),
        m_root_cmd(command_ptr(new Command(prog_name, description))),
        m_pre_hooks(std::make_shared<std::vector<PreCommandHook>>())
  {
    m_root_cmd->set_pre_hooks(m_pre_hooks);
  }

  ~ArgumentParser() = default;

  /**
   * @brief Changes the program name to the given name.
   *
   * @param new_program_name The new program name
   */
  void update_program_name(const std::string &new_program_name)
  {
    m_program_name = new_program_name;
    if (m_root_cmd)
    {
      m_root_cmd->set_name(new_program_name);
    }
  }

  /**
   * @return Reference to the root command
   */
  Command &get_root_command()
  {
    return *m_root_cmd;
  }

  /**
   * @return Returns the current program name registered with the parser
   */
  const std::string &get_program_name() const
  {
    return m_program_name;
  }

  /**
   * @brief Register a pre-command hook to be called before any command handler executes.
   *
   * Hooks receive the full command path (e.g. "zo console issue") and run
   * in registration order. They cannot prevent handler execution.
   *
   * @param hook Callback invoked with the command path before the handler runs
   */
  void add_pre_command_hook(PreCommandHook hook)
  {
    if (m_pre_hooks == nullptr)
    {
      m_pre_hooks = std::make_shared<std::vector<PreCommandHook>>();
    }
    m_pre_hooks->push_back(std::move(hook));
    if (m_root_cmd)
    {
      m_root_cmd->set_pre_hooks(m_pre_hooks);
    }
  }

  /**
   * @brief Parse keyword and positional arguments from an entrypoint function
   *
   * @param argc Total number of arguments
   * @param argv User-provided argument list (raw text)
   * @return Result of parse operation
   */
  /**
   * @brief Turn off --json for this parser entirely
   *
   * Used by zoweax, which runs APF-authorized: serializing a result would
   * call the z/OS HWTJ services from an authorized job step.
   */
  void disable_json_output()
  {
    m_json_enabled = false;
    m_root_cmd->disable_json_output();
  }

  ParseResult parse(int argc, char *argv[])
  {
    ParseResult result = parse_impl(argc, argv);
    emit_json_envelope(result, argv_requests_json(argc, argv));
    return result;
  }

  /**
   * @brief Parse and execute command based on the given string
   *
   * @param command_line The user-provided command, including keyword and positional arguments
   * @return Result of parse operation
   */
  ParseResult parse(const std::string &command_line)
  {
    ParseResult result = parse_impl(command_line);
    emit_json_envelope(result, line_requests_json(command_line));
    return result;
  }

private:
  static bool argv_requests_json(int argc, char *argv[])
  {
    for (int i = 1; i < argc; ++i)
    {
      if (argv[i] == nullptr)
        continue;
      const std::string arg(argv[i]);
      if (arg == "--")
        break;
      if (arg == "--json" || arg == "--rfj")
        return true;
    }
    return false;
  }

  static bool line_requests_json(const std::string &command_line)
  {
    std::istringstream words(command_line);
    std::string word;
    while (words >> word)
    {
      if (word == "--")
        break;
      if (word == "--json" || word == "--rfj")
        return true;
    }
    return false;
  }

  /**
   * @brief Print the --json envelope, at most once per invocation
   *
   * @param result Outcome of the parse, carrying the captured handler output
   * @param json_requested Whether --json appeared in the raw input
   */
  void emit_json_envelope(ParseResult &result, bool json_requested)
  {
    // Help goes to stdout as human-readable text; an envelope on top of it
    // would leave stdout holding neither one thing nor the other.
    if (result.status == ParseResult::ParserStatus_HelpRequested)
      return;
    if (!m_json_enabled || result.json_suppressed)
      return;
    if (!result.json_requested && !json_requested)
      return;

#ifdef __MVS__
    // zjson serializes through the z/OS HWTJ services, so there is nothing
    // to emit off-platform. Guarded here rather than in json_output.hpp so
    // that examples/native-cli keeps building parser.hpp with g++.
    json_output::Envelope envelope;
    envelope.exit_code = result.exit_code;
    envelope.data = result.json_data;
    envelope.captured_out = result.json_stdout;
    envelope.captured_err = result.json_stderr;
    envelope.error_message = result.error_message;
    envelope.stdout_is_payload = result.json_stdout_is_payload;

    result.json_output = json_output::serialize(envelope);
    std::cout << result.json_output << std::endl;
#endif
  }

  // Try to parse `arg` as a numeric or boolean literal that spans the whole
  // argument; anything else (including a lex failure) is an identifier.
  static lexer::Token classify_literal_or_identifier(const std::string &arg, const lexer::Span &span)
  {
    try
    {
      lexer::Src arg_source = lexer::Src::from_string(arg, "<arg>");
      std::vector<lexer::Token> arg_tokens = lexer::Lexer::tokenize(arg_source);

      if (arg_tokens.size() != 2 || arg_tokens[1].get_kind() != lexer::TokEof)
      {
        return lexer::Token::make_id(arg.c_str(), arg.size(), span);
      }

      const lexer::Token &parsed_token = arg_tokens[0];
      switch (parsed_token.get_kind())
      {
      case lexer::TokIntLit:
        return lexer::Token::make_int_lit(parsed_token.get_int_value(), parsed_token.get_int_base(), span);
      case lexer::TokFloatLit:
        return lexer::Token::make_float_lit(parsed_token.get_float_value(), parsed_token.has_float_exponent(), span);
      case lexer::TokTrue:
        return lexer::Token(lexer::TokTrue, span);
      case lexer::TokFalse:
        return lexer::Token(lexer::TokFalse, span);
      default:
        return lexer::Token::make_id(arg.c_str(), arg.size(), span);
      }
    }
    catch (const lexer::LexError &)
    {
      return lexer::Token::make_id(arg.c_str(), arg.size(), span);
    }
  }

  // Classify a single non-passthrough CLI argument as a long flag, short
  // flag, quoted string literal, numeric/boolean literal, or identifier.
  static lexer::Token classify_argument(const std::string &arg, const lexer::Span &span)
  {
    if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-')
    {
      // Long flag: --flag
      return lexer::Token::make_long_flag(arg.c_str() + 2, arg.size() - 2, span);
    }
    if (arg.size() > 1 && arg[0] == '-' && arg != "-")
    {
      // Short flag: -f or -abc
      return lexer::Token::make_short_flag(arg.c_str() + 1, arg.size() - 1, span);
    }
    if (arg.size() >= 2 &&
        ((arg[0] == '"' && arg[arg.size() - 1] == '"') ||
         (arg[0] == '\'' && arg[arg.size() - 1] == '\'')))
    {
      // Quoted string literal (remove quotes)
      return lexer::Token::make_str_lit(arg.c_str() + 1, arg.size() - 2, span);
    }
    // Use lexer to determine if this is numeric or identifier
    return classify_literal_or_identifier(arg, span);
  }

  ParseResult parse_impl(int argc, char *argv[])
  {
    ZLOG_TRACE("ArgumentParser::parse(argc=%d) entry", argc);

    if (argc < 1)
    {
      ZLOG_TRACE("ArgumentParser::parse: invalid argc < 1");
      ParseResult error_result;
      error_result.status = ParseResult::ParserStatus_ParseError;
      error_result.error_message = "invalid arguments provided (argc < 1)";
      error_result.exit_code = 1;
      return error_result;
    }

    // Convert argv[1..argc-1] to vector<string>
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
      args.push_back(std::string(argv[i]));
    }

    // Use lexer to parse each argument appropriately
    std::vector<lexer::Token> tokens;
    ZLOG_TRACE("Converting %zu arguments to tokens", args.size());
    size_t pos = 0;
    bool passthrough_mode = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];
      lexer::Span span(pos, pos + arg.size());

      // After --, treat all arguments as raw strings (passthrough mode)
      if (passthrough_mode)
      {
        tokens.push_back(lexer::Token::make_id(arg.c_str(), arg.size(), span));
        pos += arg.size() + 1;
        continue;
      }

      // Check for passthrough delimiter --
      if (arg == "--")
      {
        tokens.push_back(lexer::Token(lexer::TokDoubleMinus, span));
        passthrough_mode = true;
        pos += arg.size() + 1;
        continue;
      }

      tokens.push_back(classify_argument(arg, span));
      pos += arg.size() + 1;
    }

    size_t token_index = 0;
    if (!m_root_cmd)
    {
      ParseResult error_result;
      error_result.status = ParseResult::ParserStatus_ParseError;
      error_result.error_message =
          "ArgumentParser is not initialized correctly.";
      error_result.exit_code = 1;
      return error_result;
    }

    ZLOG_TRACE("Starting parse with %zu tokens", tokens.size());
    ParseResult result = m_root_cmd->parse(tokens, token_index, "");
    if (result.status == ParseResult::ParserStatus_Success &&
        token_index < tokens.size())
    {
      result.status = ParseResult::ParserStatus_ParseError;
      std::stringstream ss;
      ss << "unexpected arguments starting from: ";
      tokens[token_index].print(ss);
      result.error_message = ss.str();
      std::cerr << "Error: " << result.error_message << "\n\n";
      m_root_cmd->generate_help(std::cerr, "");
      result.exit_code = 1;
      return result;
    }
    return result;
  }

  ParseResult parse_impl(const std::string &command_line)
  {
    ZLOG_TRACE("ArgumentParser::parse(string='%s') entry", command_line.c_str());

    if (!m_root_cmd)
    {
      ParseResult error_result;
      error_result.status = ParseResult::ParserStatus_ParseError;
      error_result.error_message =
          "ArgumentParser is not initialized correctly.";
      error_result.exit_code = 1;
      return error_result;
    }

    try
    {
      lexer::Src source = lexer::Src::from_string(command_line, "<cli>");
      std::vector<lexer::Token> tokens = lexer::Lexer::tokenize(source);

      // filter out TokEof at the end
      if (!tokens.empty() && tokens.back().get_kind() == lexer::TokEof)
      {
        tokens.pop_back();
      }

      ZLOG_TRACE("Lexer produced %zu tokens for string parse", tokens.size());
      size_t token_index = 0;
      ParseResult result = m_root_cmd->parse(tokens, token_index, "");

      // return early in the event of an error or help request from the parse
      // call
      if (result.status == ParseResult::ParserStatus_ParseError)
      {
        return result;
      }
      if (result.status == ParseResult::ParserStatus_HelpRequested)
      {
        return result;
      }

      // parsing succeeded, but not all tokens were consumed (and no subcommand
      // handled them)
      if (token_index < tokens.size())
      {
        result.status = ParseResult::ParserStatus_ParseError;
        std::stringstream ss;
        ss << "unexpected arguments starting from: ";
        tokens[token_index].print(ss);
        result.error_message = ss.str();
        std::cerr << "Error: " << result.error_message << "\n\n";
        // show root help on unexpected trailing arguments error (to stderr)
        m_root_cmd->generate_help(std::cerr, "");
        result.exit_code = 1;
        return result;
      }

      return result;
    }
    catch (const lexer::LexError &e)
    {
      std::cerr << "Lexer error: " << e.what() << std::endl;
      ParseResult error_result;
      error_result.status = ParseResult::ParserStatus_ParseError;
      error_result.error_message = e.what();
      error_result.exit_code = 1;
      // show root help on lexer error (to stderr)
      if (m_root_cmd)
        m_root_cmd->generate_help(std::cerr, "");
      return error_result;
    }
    catch (const std::exception &e)
    {
      // catch standard exceptions that might arise
      std::cerr << "Parser error: " << e.what() << std::endl;
      ParseResult error_result;
      error_result.status = ParseResult::ParserStatus_ParseError;
      error_result.error_message = e.what();
      error_result.exit_code = 1;
      // show root help on general parser error (to stderr)
      if (m_root_cmd)
        m_root_cmd->generate_help(std::cerr, "");
      return error_result;
    }
  }

private:
  bool m_json_enabled = true;
  std::string m_program_name;
  std::string m_program_desc;
  command_ptr m_root_cmd;
  std::shared_ptr<std::vector<PreCommandHook>> m_pre_hooks;
}; // class ArgumentParser

/**
 * @brief Generate a bash completion script for the given CLI parser.
 *
 * @param os Output stream to write the script to.
 * @param prog_name The name of the executable (as invoked).
 * @param root_cmd The root Command object.
 */
inline void generate_bash_completion(std::ostream &os, const std::string &prog_name, const Command &root_cmd)
{
  // Helper to recursively emit command tree as bash arrays
  struct BashEmit
  {
    static void emit_command_tree(const Command &cmd, const std::string &prefix, std::ostream &os)
    {
      std::string arr_name = "_parser_cmds";
      if (!prefix.empty())
        arr_name += "_" + prefix;
      os << arr_name << "=( ";
      // Subcommands
      const std::map<std::string, command_ptr> &subs = cmd.get_commands();
      for (const auto &sub_pair : subs)
      {
        os << "'" << sub_pair.first << "' ";
        // Aliases
        const std::vector<std::string> &aliases = sub_pair.second->get_aliases();
        for (size_t j = 0; j < aliases.size(); ++j)
        {
          os << "'" << aliases[j] << "' ";
        }
      }
      os << ")\n";
      // Options
      std::string opt_arr = "_parser_opts";
      if (!prefix.empty())
        opt_arr += "_" + prefix;
      os << opt_arr << "=( ";
      const std::vector<ArgumentDef> &args = cmd.get_args();
      for (size_t i = 0; i < args.size(); ++i)
      {
        if (args[i].positional)
          continue;
        for (size_t j = 0; j < args[i].aliases.size(); ++j)
        {
          os << "'" << args[i].aliases[j] << "' ";
        }
        // Also add --<name> if not already present
        if (!args[i].name.empty() && !args[i].positional)
        {
          std::string long_flag = "--" + args[i].name;
          bool found = false;
          for (size_t j = 0; j < args[i].aliases.size(); ++j)
          {
            if (args[i].aliases[j] == long_flag)
            {
              found = true;
              break;
            }
          }
          if (!found)
            os << "'" << long_flag << "' ";
        }
      }
      os << ")\n";
      // Recurse for subcommands
      for (const auto &sub_pair : subs)
      {
        std::string sub_prefix = prefix.empty() ? sub_pair.first : (prefix + "_" + sub_pair.first);
        emit_command_tree(*sub_pair.second, sub_prefix, os);
      }
    }
  };

  // Emit arrays for all commands/subcommands
  BashEmit::emit_command_tree(root_cmd, "", os);

  // Emit the completion function
  os << "_parser_complete_" << prog_name << "() {\n";
  os << "  local cur prev words cword\n";
  os << "  _get_comp_words_by_ref -n : cur prev words cword\n";
  os << "  local i cmd_path=\"\" arr_name=\"_parser_cmds\" opt_arr=\"_parser_opts\"\n";
  os << "  local level=0\n";
  os << "  for ((i=1; i < ${#words[@]}; ++i)); do\n";
  os << "    w=${words[i]}\n";
  os << "    eval arr=\"\\${!arr_name}\"\n";
  os << "    found=0\n";
  os << "    for entry in \"${arr[@]}\"; do\n";
  os << "      if [[ \"$w\" == \"$entry\" ]]; then\n";
  os << "        cmd_path+=\"_\"$w\n";
  os << "        arr_name=\"_parser_cmds$cmd_path\"\n";
  os << "        opt_arr=\"_parser_opts$cmd_path\"\n";
  os << "        found=1\n";
  os << "        break\n";
  os << "      fi\n";
  os << "    done\n";
  os << "    if [[ $found -eq 0 ]]; then break; fi\n";
  os << "  done\n";
  os << "  eval opts=\"\\${!opt_arr}\"\n";
  os << "  eval cmds=\"\\${!arr_name}\"\n";
  os << "  COMPREPLY=( $(compgen -W \"${opts[*]} ${cmds[*]}\" -- \"$cur\") )\n";
  os << "  return 0\n";
  os << "}\n";
  os << "complete -F _parser_complete_" << prog_name << " " << prog_name << "\n";
}

/**
 * @brief Add an argument with the given name to the ArgumentBuilder
 *
 * @param name The name of the new argument
 * @return ArgumentBuilder
 */
inline ArgumentBuilder Command::add_argument(const std::string &name)
{
  return ArgumentBuilder(this, name);
}

inline ArgumentBuilder::ArgumentBuilder(ArgumentBuilder &&other)
    : m_command(other.m_command), m_arg_def(other.m_arg_def),
      m_is_registered(other.m_is_registered)
{
  other.m_command = nullptr;
  other.m_is_registered = true;
}

inline ArgumentBuilder &ArgumentBuilder::operator=(ArgumentBuilder &&other)
{
  if (this != &other)
  {
    if (!m_is_registered && m_command)
    {
      m_command->register_argument(m_arg_def);
    }
    m_command = other.m_command;
    m_arg_def = other.m_arg_def;
    m_is_registered = other.m_is_registered;
    other.m_command = nullptr;
    other.m_is_registered = true;
  }
  return *this;
}

inline ArgumentBuilder::~ArgumentBuilder()
{
  if (!m_is_registered)
  {
    m_command->register_argument(m_arg_def);
    m_is_registered = true;
  }
}
} // namespace parser

#endif // PARSER_HPP
