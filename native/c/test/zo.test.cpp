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

#include <cstdlib>
#include <string>
#include "ztest.hpp"
#include "ztype.h"
#include "zutils.hpp"
#include "zo.test.hpp"
#include "zo.ds.test.hpp"
#include "zo.uss.test.hpp"
#include "zo.job.test.hpp"
#include "zo.system.test.hpp"
#include "zoa.console.test.hpp"
#include "zo.tso.test.hpp"
#include "zo.plugin.test.hpp"

using namespace ztst;

void zo_tests()
{

  describe("zo", []() -> void
  {
    it("should list a version of the tool", []() -> void
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output(zo_command + " version", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("zo");
      Expect(response).ToContain("Version:");

      // Version information
      static const std::regex rev(R"(Version:\s*(\S+))");
      std::smatch mv;

      Expect(std::regex_search(response, mv, rev)).ToBe(true);
      const std::string version = mv[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum

      Expect(response).ToContain("Build Date:");

      // Build date information
      static const std::regex red(R"(Build Date:\s*([^\r\n]+))");
      std::smatch md;

      Expect(std::regex_search(response, md, red)).ToBe(true);
      const std::string buildDate = md[1].str();

      Expect(buildDate.length()).ToBeGreaterThanOrEqualTo(11); // MMM DD YYYY at minimum

      Expect(response).Not().ToContain("unknown");
    });
    it("should list a short version of the tool", []() -> void
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output(zo_command + " --version", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).Not().ToContain("zo");
      Expect(response).Not().ToContain("Version");
      Expect(response).Not().ToContain("Build Date");
      Expect(response).Not().ToContain("unknown");

      static const std::regex rev(R"(^\s*(\d+\.\d+\.\d+)(?:[-+][^\r\n]*)?\s*$)");
      std::smatch mv;

      Expect(std::regex_search(response, mv, rev)).ToBe(true);
      const std::string version = mv[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum
    });
#ifdef RELEASE_BUILD
    it("should remain less than 10mb in size",
#else
    xit("should remain less than 10mb in size",
#endif
       []() -> void
    {
      std::string response;
      execute_command_with_output("cat ../build-out/zo | wc -c", response);
      int file_size = std::stoi(response);
      ExpectWithContext(file_size, response).ToBeLessThan(10 * 1024 * 1024);
    });
    describe("--json output", []() -> void
    {
      it("should emit a structured envelope for the version command", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " version --json", response);
        ExpectWithContext(rc, response).ToBe(0);

        // One line, one JSON object.
        Expect(response.find('\n')).ToBe(response.length() - 1);
        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"exitCode\":0");
        Expect(response).ToContain("\"version\"");
        Expect(response).ToContain("\"buildDate\"");

        // The human-readable banner must stay out of the payload.
        Expect(response).Not().ToContain("Copyright Contributors");
      });

      it("should accept --json before the subcommand", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " --json uss list /tmp", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"items\"");
      });

      it("should list USS files as structured items without --rfc", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " uss list /tmp --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("\"items\"");
        Expect(response).ToContain("\"returnedRows\"");
        Expect(response).ToContain("\"name\"");
      });

      it("should report a failing command in the envelope", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set list NO.SUCH.HLQ.AT.ALL --json", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("\"success\":false");
        // Diagnostics land in the envelope rather than bare stderr.
        Expect(response).ToContain("\"stderr\"");
      });

      it("should still emit an envelope when the options are invalid", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set list SOME.HLQ --no-such-option --json", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        // The pre-scan covers failures that never reach a handler.
        Expect(response).ToContain("\"success\":false");
      });

      it("should print help rather than JSON for a command group", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).ToContain("view");
      });

      it("should print help rather than JSON for the root command", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        // The root handler prints help; wrapping that serves nobody.
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).ToContain("data-set");
      });
      it("should leave output unchanged when --json is absent", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " uss list /tmp", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).Not().ToContain("\"items\"");
      });
    });
    zo_ds_tests();
    zo_uss_tests();
    zo_job_tests();
    zo_system_tests();
    zo_tso_tests();
    zo_plugin_tests();
  });

  describe("zoa", []() -> void
  {
    zoa_console_tests();
  });
}
