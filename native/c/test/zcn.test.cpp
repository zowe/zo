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

#include <iostream>
#include <stdexcept>
#include <unistd.h>

#include "ztest.hpp"
#include "../zcn.hpp"

using namespace ztst;

void zcn_tests()
{
  describe("zcn tests", []() -> void
  {
    it("should fail for unauthorized callers", []() -> void
    {
      int rc = 0;
      ZCN zcn = {0};
      std::string console_name = "ZOWETST";

      rc = zcn_activate(&zcn, console_name);
      ExpectWithContext(rc, zcn.diag.e_msg).ToBe(RTNCD_FAILURE);

      // Verify console name was properly set
      Expect(std::string(zcn.console_name, 8)).ToBe("ZOWETST ");

      rc = zcn_deactivate(&zcn);
      ExpectWithContext(rc, zcn.diag.e_msg).ToBe(RTNCD_FAILURE);
    });

    //  it("should be able to activate and deactivate a console",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Verify console name was properly set
    //       Expect(std::string(zcn.console_name, 8)).ToBe("ZOWETST ");

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should handle console name truncation and padding",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "verylongconsolename"; // exceeds 8 chars

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Verify console name was truncated to 8 characters
    //       Expect(std::string(zcn.console_name, 8)).ToBe("VERYLONG");

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should handle short console names with padding",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "TEST";

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Verify console name was padded with spaces
    //       Expect(std::string(zcn.console_name, 8)).ToBe("TEST    ");

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should be able to issue a console command and get response",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";
    //       std::string command = "D T";
    //       std::string response;

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_put(&zcn, command);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_get(&zcn, response);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Verify we got some response
    //       Expect(response).Not().ToBe("");

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should handle D IPLINFO command",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";
    //       std::string command = "D IPLINFO";
    //       std::string response;

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_put(&zcn, command);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_get(&zcn, response);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Verify we got some response containing IPL info
    //       Expect(response).Not().ToBe("");

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should handle empty console name",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "";

    //       rc = zcn_activate(&zcn, console_name);
    //       // Should handle empty name gracefully, possibly using default
    //       // or returning an error - either is acceptable
    //       if (0 != rc)
    //       {
    //         rc = zcn_deactivate(&zcn);
    //       }
    //     });

    //  it("should handle null ZCN pointer",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       std::string console_name = "ZOWETST";

    //       rc = zcn_activate(nullptr, console_name);
    //       Expect(rc).Not().ToBe(0);
    //     });

    //  it("should initialize ZCN structure properly",
    //     []() -> void
    //     {
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";

    //       int rc = zcn_activate(&zcn, console_name);
    //       if (0 != rc)
    //       {
    //         // Verify eye catcher
    //         Expect(std::string(zcn.eye, 3)).ToBe("ZCN");

    //         // Verify buffer size defaults
    //         if (zcn.buffer_size == 0)
    //         {
    //           Expect(zcn.buffer_size).ToBe(0); // May be set to default internally
    //         }
    //         else
    //         {
    //           Expect(zcn.buffer_size).ToBeGreaterThan(0);
    //         }

    //         rc = zcn_deactivate(&zcn);
    //       }
    //     });

    //  it("should handle multiple console operations",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";
    //       std::string response;

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Issue multiple commands
    //       rc = zcn_put(&zcn, "D T");
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_get(&zcn, response);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Clear response for next command
    //       response = "";

    //       rc = zcn_put(&zcn, "D A");
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_get(&zcn, response);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });

    //  it("should handle console command without activation",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string command = "D T";

    //       // Try to put command without activating console first
    //       rc = zcn_put(&zcn, command);
    //       Expect(rc).Not().ToBe(0); // Should fail
    //     });

    //  it("should handle get without put",
    //     []() -> void
    //     {
    //       int rc = 0;
    //       ZCN zcn = {0};
    //       std::string console_name = "ZOWETST";
    //       std::string response;

    //       rc = zcn_activate(&zcn, console_name);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);

    //       // Try to get response without issuing a command
    //       rc = zcn_get(&zcn, response);
    //       // This may succeed with empty response or fail - both are valid

    //       rc = zcn_deactivate(&zcn);
    //       ExpectWithContext(rc, zcn.diag.e_msg).ToBe(0);
    //     });
  });
}