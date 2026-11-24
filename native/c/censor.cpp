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

#include <algorithm>
#include <string>
#include <vector>
#include "censor.hpp"
#include "lexer.hpp"
#include "zlogger.hpp"

namespace censor
{

std::vector<std::string> build_censored_options()
{
  return std::vector<std::string>({
      "--auth",
      "--authentication",
      "--basicAuth",
      "--base64EncodedAuth",
      "--certFilePassphrase",
      "--credentials",
      "--pw",
      "--pass",
      "--password",
      "--passphrase",
      "--tv",
      "--tokenValue",
      "--oldPwd",
      "--newPwd",
      "-p",
  });
}

static const std::vector<std::string> MAIN_CENSORED_OPTIONS = build_censored_options();
static const std::string CENSOR_RESPONSE = "****";

static size_t sensitive_value_end(const std::string &value)
{
  const std::string_view whitespace = " \t\r\n";

  if (!value.empty() && value.front() == '"')
  {
    try
    {
      const size_t end = lexer::Lexer::quoted_value_end(value);
      if (end == value.size() || whitespace.find(value[end]) != std::string_view::npos)
        return end;
    }
    catch (const lexer::LexError &)
    {
      // Malformed quote, don't risk logging rest of the value
    }
    return value.size();
  }

  const size_t end = value.find_first_of(" \t\r\n\"");
  return end == std::string_view::npos || value[end] == '"' ? value.size() : end;
}

std::string censor_raw_data(const std::string &data)
{
  std::string result(data);
  std::vector<lexer::Token> command_tokens;
  std::vector<std::string> command_token_strs;
  std::vector<lexer::Span> censored_spans;
  try
  {
    lexer::Src full_str_source = lexer::Src::from_string(
        data, "<censor_raw_data>");
    command_tokens = lexer::Lexer::tokenize(full_str_source);
    for (auto &token : command_tokens)
    {
      std::string arg_text = data.substr(token.get_span().start, token.get_span().end - token.get_span().start);
      size_t equals_index = arg_text.find_first_of('=');
      if (equals_index != std::string::npos && equals_index != 0)
      {
        arg_text = arg_text.substr(0, equals_index);
      }
      command_token_strs.emplace_back(arg_text);
    }
  }
  catch (const lexer::LexError &le)
  {
    ZLOG_ERROR("Lexer error while trying to censor command line: %s", le.what());
    return CENSOR_RESPONSE;
  }

  for (const std::string &search_arg : MAIN_CENSORED_OPTIONS)
  {
    auto search_start = command_token_strs.begin();
    auto match = std::find(search_start, command_token_strs.end(), search_arg);
    while (match != command_token_strs.end())
    {
      long arg_index = std::distance(command_token_strs.begin(), match);
      bool is_equals_form = result[command_tokens[arg_index].get_span().start + search_arg.length()] == '=';
      search_start = std::next(match);
      if (search_start == command_token_strs.end())
      {
        break;
      }

      long censor_start_token_index = std::distance(command_token_strs.begin(), search_start);
      long censor_start = is_equals_form ? command_tokens[arg_index].get_span().start + command_token_strs[arg_index].length() + 1
                                         : command_tokens[censor_start_token_index].get_span().start;
      censored_spans.emplace_back(lexer::Span(censor_start,
                                              censor_start + sensitive_value_end(result.substr(censor_start))));
      match = std::find(search_start, command_token_strs.end(), search_arg);
    }
  }
  std::sort(censored_spans.begin(), censored_spans.end(), [](const lexer::Span &a, const lexer::Span &b)
  {
    return a.start > b.start; // sort the spans  with rightmost first so that we can replace right to left
  });
  for (auto &span : censored_spans)
  {
    result = result.replace(span.start, span.end - span.start, CENSOR_RESPONSE);
  }
  return result;
}
} // namespace censor