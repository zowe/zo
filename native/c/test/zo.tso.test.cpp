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

#include "ztest.hpp"
#include "zutils.hpp"

using namespace ztst;

void zo_tso_tests()
{
  describe("tso issue command tests", [&]() -> void
  {
    it("should display help", []() -> void
    {
      std::string response;
      std::string command = zo_command + " tso";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("issue");
    });

    it("should successfully issue a simple TSO command", []() -> void
    {
      std::string response;
      std::string command = zo_command + " tso issue time";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("TIME");
    });

    it("should successfully issue a TSO command with special characters in the command", []() -> void
    {
      std::string response;
      std::string command = zo_command + " tso issue \"time (&$\"";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("TIME");
    });

    it("should successfully issue a TSO command with special characters in the command", []() -> void
    {
      std::string response;
      std::string command = zo_command + " tso issue \"time (&$\"";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("TIME");
    });

    it("should error when the TSO command itself is invalid", []() -> void
    {
      std::string response;
      std::string command = zo_command + " tso issue \"invalidCommand\"";

      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("Error running command");
    });
  });
}