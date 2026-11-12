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

void zoa_console_tests()
{
  describe("console issue command tests", [&]() -> void
  {
    it("should display help", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " console";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("issue");
    });

    it("should issue console command successfully", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " console issue \"D T\"";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response.find("IEE136I LOCAL: ")).Not().ToBe(std::string::npos);
    });

    it("should error when the console name is invalid", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " console issue \"D IPLINFO\" --console-name 1Invalid";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("Error: could not activate console:");
    });

    it("should successfully issue a command when the console does not already exist", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " console issue \"D IPLINFO\" --console-name newConsoleName";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response.find("IEE254I")).Not().ToBe(std::string::npos);
    });

    it("should issue without waiting when boolean is set to false", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " console issue \"D IPLINFO\" --wait false";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should not exist in the unauthorized zo binary", []() -> void
    {
      std::string response;
      std::string command = zo_command + " console issue \"D T\"";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("unexpected argument: console");
    });
  });

  describe("minimal authorized binary tests", [&]() -> void
  {
    it("should not include data set commands", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " data-set list";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("unexpected argument: data-set");
    });

    it("should not include the RPC server", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " server";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("unexpected argument: server");
    });

    it("should not include plug-in commands", []() -> void
    {
      std::string response;
      std::string command = zoa_command + " plugins list";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("unexpected argument: plugins");
    });

    it("should list only console and version in root help", []() -> void
    {
      std::string response;
      std::string command = zoa_command;
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("console");
      Expect(response).ToContain("version");
      Expect(response).Not().ToContain("data-set");
      Expect(response).Not().ToContain("plugins");
    });
  });

  describe("APF authorization drop (ZUTNOAUT) tests", [&]() -> void
  {
    it("should run a non-privileged command from the APF-authorized binary", []() -> void
    {
      // exercises the live authorization drop (IEAVJAOF under IEAARR recovery);
      // the pre-command hook fails closed, so a broken drop would exit non-zero
      std::string response;
      std::string command = zoa_command + " version";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("Version:");
    });

    it("should run a non-privileged command from the unauthorized binary", []() -> void
    {
      std::string response;
      std::string command = zo_command + " version";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("Version:");
    });

    it("should drop authorization for the whole job step in an interactive session", []() -> void
    {
      // entering interactive mode runs the non-privileged root command, which
      // drops JSCBAUTH for the job step; a console command in the same process
      // must then fail with a TESTAUTH error even though the binary is
      // APF-authorized, while non-privileged commands keep working
      std::string response;
      std::string command = "printf 'console issue DTIME\\nversion\\nquit\\n' | " + zoa_command + " --it";
      int rc = execute_command_with_output(command, response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("Error: could not activate console:");
      Expect(response).ToContain("Not authorized");
      Expect(response).ToContain("Version:");
    });
  });
}
