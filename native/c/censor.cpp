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

std::string censor_raw_data(const std::string &data)
{
  std::string result(data);
  int censor_start = -1;
  char censor_start_char = '\0';
  const std::vector<std::string> suffixes = {" ", "=", "\t", "\n", "\r"};
  const std::vector<char> whitespace_chars = {' ', '\t', '\n', '\r'};

  for (auto &search_arg : MAIN_CENSORED_OPTIONS)
  {
    for (auto &suffix : suffixes)
    {
      std::string dash_form = search_arg.length() == 1 ? "-" + search_arg + suffix : "--" + search_arg + suffix;
      size_t arg_index = result.find(dash_form, 0);
      while (arg_index != std::string::npos)
      {
        censor_start = arg_index + dash_form.length();

        lexer::Src arg_source = lexer::Src::from_string(
            result.substr(censor_start, result.length() - censor_start), "<args>");
        std::vector<lexer::Token> arg_tokens = lexer::Lexer::tokenize(arg_source);

        result = result.replace(censor_start + arg_tokens[0].get_span().start, arg_tokens[0].get_span().end - arg_tokens[0].get_span().start, CENSOR_RESPONSE);
        // search for repeated instances of this arg
        arg_index = result.find(dash_form, censor_start + arg_tokens[0].get_span().start + CENSOR_RESPONSE.length());
      }
    }
  }
  return result;
}
} // namespace censor