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
#include "censor.hpp"

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
  int censor_end = -1;
  char censor_start_char = '\0';
  const std::vector<std::string> suffixes = {" ", "=", "\t", "\n"};
  const std::vector<char> whitespace_chars = {' ', '\t', '\n'};
  for (auto &search_arg : MAIN_CENSORED_OPTIONS)
  {
    for (auto &suffix : suffixes)
    {
      std::string dash_form = search_arg.length() == 1 ? "-" + search_arg + suffix : "--" + search_arg + suffix;
      size_t arg_index = result.find(dash_form, 0);
      while (arg_index != std::string::npos)
      {
        censor_start = arg_index + dash_form.length();
        censor_start_char = '\0';
        // skip leading whitespace
        for (; censor_start < result.length(); censor_start++)
        {
          if (std::find(whitespace_chars.begin(), whitespace_chars.end(), result[censor_start]) == whitespace_chars.end())
          { // if it is NOT a whitespace character
            break;
          }
        }
        censor_end = censor_start;
        while (censor_end < result.length())
        {
          const char &current_char = result[censor_end];
          bool current_char_whitespace = std::find(whitespace_chars.begin(), whitespace_chars.end(), current_char) != whitespace_chars.end();
          if (censor_start_char == '\0' && !current_char_whitespace)
          {
            censor_start_char = current_char;
          }
          else if (censor_start_char == '"' || censor_start_char == '\'')
          {
            if (result[censor_end - 1] != '\\' && censor_start_char == current_char)
            {
              censor_end++; // censor the end quote
              break;
            }
          }
          else if (current_char_whitespace)
          {
            // the start of the censor string isn't a quote character, so whitespace should end the censoring
            break;
          }
          censor_end++;
        }
        censor_end = censor_end > result.length() ? result.length() : censor_end;
        if (censor_end - censor_start > 0)
        {
          result = result.replace(censor_start, censor_end - censor_start, CENSOR_RESPONSE);
          // search for repeated instances of this arg
          arg_index = result.find(dash_form, censor_start + CENSOR_RESPONSE.length());
        }
        else
        {
          // in case we didn't censor anything, search the rest of the string for the same form of argument
          arg_index = result.find(dash_form, censor_end);
        }
      }
    }
  }
  return result;
}
} // namespace censor