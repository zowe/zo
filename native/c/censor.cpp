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
      "auth",
      "authentication",
      "basicAuth",
      "base64EncodedAuth",
      "certFilePassphrase",
      "credentials",
      "pw",
      "pass",
      "password",
      "passphrase",
      "tv",
      "tokenValue",
      "oldPwd",
      "newPwd",
      "p",
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
  int censor_start = -1;
  const std::vector<std::string> suffixes = {" ", "=", "\t", "\n", "\r"};

  for (auto &search_arg : MAIN_CENSORED_OPTIONS)
  {
    for (auto &suffix : suffixes)
    {
      std::string dash_form = search_arg.length() == 1 ? "-" + search_arg + suffix : "--" + search_arg + suffix;
      size_t arg_index = result.find(dash_form, 0);
      while (arg_index != std::string::npos)
      {
        censor_start = arg_index + dash_form.length();

        try
        {
          const std::string censor_search_str = result.substr(censor_start, result.length() - censor_start);
          lexer::Src arg_source = lexer::Src::from_string(
              censor_search_str, "<censor_raw_data>");
          std::vector<lexer::Token> arg_tokens = lexer::Lexer::tokenize(arg_source);

          result = result.replace(censor_start + arg_tokens[0].get_span().start,
                                  sensitive_value_end(censor_search_str.substr(arg_tokens[0].get_span().start)), CENSOR_RESPONSE);
          // search for repeated instances of this arg
          arg_index = result.find(dash_form, censor_start + arg_tokens[0].get_span().start + CENSOR_RESPONSE.length());
        }
        catch (const lexer::LexError &le)
        {
          ZLOG_ERROR("Lexer error while trying to censor command line: %s", le.what());
          return result.substr(0, censor_start) + CENSOR_RESPONSE;
        }
      }
    }
  }
  return result;
}
} // namespace censor