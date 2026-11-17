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
  return std::vector<std::string>({"auth", "authentication", "basicAuth", "base64EncodedAuth",
                                   "certFilePassphrase", "credentials", "p",
                                   "pw", "pass", "password", "passphrase",
                                   "tv", "tokenValue", "oldPwd", "newPwd"});
}

static const std::vector<std::string> MAIN_CENSORED_OPTIONS = build_censored_options();
static const std::string CENSOR_RESPONSE = "****";

std::string censor_raw_data(const std::string &data)
{
  std::string result(data);
  int censor_start = -1;
  int censor_end = -1;
  char censor_start_char = '\0';
  for (auto &search_arg : MAIN_CENSORED_OPTIONS)
  {
    censor_start = -1;
    std::string dash_form = search_arg.length() == 1 ? "-" + search_arg + " " : "--" + search_arg + " ";
    size_t arg_index = result.find(dash_form);
    censor_start_char = '\0';
    if (arg_index == std::string::npos)
    {
      // try again with arg=value format
      dash_form = search_arg.length() == 1 ? "-" + search_arg + "=" : "--" + search_arg + "=";
      arg_index = result.find(dash_form);
    }
    if (arg_index != std::string::npos)
    {
      censor_start = arg_index + dash_form.length();
    }

    if (censor_start != -1)
    {
      for (; censor_start < result.length(); censor_start++)
      {
        // skip leading spaces
        if (result[censor_start] != ' ')
        {
          break;
        }
      }
      censor_end = censor_start;
      while (censor_end < result.length())
      {
        char &current_char = result[censor_end];
        if (censor_start_char == '\0' && current_char != ' ')
        {
          censor_start_char = current_char;
        }
        else if ((censor_start_char == '"' || censor_start_char == '\''))
        {
          if (result[censor_end - 1] != '\\' && censor_start_char == current_char)
          {
            censor_end++; // censor the end quote
            break;
          }
        }
        else if (current_char == ' ')
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
      }
    }
  }
  return result;
}
} // namespace censor