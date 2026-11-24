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

#include <cstddef>
#include <ctime>
#include <stdlib.h>
#include <string>
#include <vector>
#include <fstream>
#include "ztest.hpp"
#include "zutils.hpp"
#include "zo.test.hpp"
#include "zo.system.test.hpp"

using namespace std;
using namespace ztst;

void zo_system_tests()
{
  describe("list-proclib tests", [&]() -> void
  {
    it("should list proclib", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-proclib", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("list-parmlib tests", [&]() -> void
  {
    it("should list parmlib", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-parmlib", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("list-apf tests", [&]() -> void
  {
    it("should list apf", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-apf", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("list-linklist tests", [&]() -> void
  {
    it("should list linklist", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-linklist", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("list-subsystems tests", [&]() -> void
  {
    it("should list subsystems", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-subsystems", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("list-proclib and validate content tests", [&]() -> void
  {
    it("should list proclib and validate content", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system list-proclib", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
      Expect(response.length()).ToBeGreaterThan(0);
      // Basic validation that we got some output
      vector<string> lines = parse_rfc_response(response, "\n");
      Expect(lines.size()).ToBeGreaterThan(0);
    });
  });
  describe("display-symbol tests", [&]() -> void
  {
    it("should display a system symbol", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system display-symbol YYMMDD", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
      Expect(response).Not().ToContain("&");
      Expect(response.length()).ToBeGreaterThan(0);
    });
  });
  describe("use 'lp' alias for list-proclib command tests", [&]() -> void
  {
    it("should use 'lp' alias for list-proclib command", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system lp", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
    });
  });
  describe("view syslog tests", [&]() -> void
  {
    it("should view syslog", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog", response);
      ExpectWithContext(rc, response).ToBe(0);
      // split response into lines in an array
      vector<string> lines = parse_rfc_response(response, "\n");
      Expect(lines.size()).ToBeGreaterThanOrEqualTo(1);
    });

    it("should view syslog with max-lines", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      int max_lines = 3;

      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines " +
                               to_string(max_lines),
                           stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      vector<string> lines = parse_rfc_response(stdout_output, "\n");
      Expect(lines.size() - 1).ToBe(max_lines);
    });

    it("should fail with invalid max-lines", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --max-lines 0", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      rc = execute_command_with_output(zo_command + " system view-syslog --max-lines 10001", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
    });
    it("should allow time of just hh:mm:ss", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --time 00:00:00", response);
      ExpectWithContext(rc, response).ToBe(0);
    });
    it("should fail with invalid time", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --time 25:00:00", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("Error: invalid time");
    });
    it("should fail with invalid date", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --date 2026-13-01", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("Error: invalid date");
    });

    it("should view syslog with --seconds-ago", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --seconds-ago 300", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response.length()).ToBeGreaterThan(0);
    });

    it("should fail when --seconds-ago used with --date", []()
    {
      int rc = 0;
      string response;
      rc =
          execute_command_with_output(zo_command + " system view-syslog --seconds-ago 300 --date 2026-03-23", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("mutually exclusive");
    });

    it("should fail when --seconds-ago used with --time", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --seconds-ago 300 --time 10:00:00", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      Expect(response).ToContain("mutually exclusive");
    });

    it("should fail with invalid --seconds-ago value", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog --seconds-ago 0", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
      rc = execute_command_with_output(zo_command + " system view-syslog --seconds-ago 86401", response);
      ExpectWithContext(rc, response).Not().ToBe(0);
    });

    it("should use default behavior (last 30 seconds) with no arguments", []()
    {
      int rc = 0;
      string response;
      rc = execute_command_with_output(zo_command + " system view-syslog", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should include endDate and endTime in metadata", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --seconds-ago 300", stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stderr_output).ToContain("End:");
      Expect(stderr_output).ToContain("Start:");
      Expect(stderr_output).ToContain("Lines:");
      Expect(stderr_output).ToContain("Has more:");
    });

    it("should have endDate in yyyy-mm-dd format", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --seconds-ago 300", stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);

      auto pos = stderr_output.find("End: ");
      Expect(pos).Not().ToBe(string::npos);
      string after_end = stderr_output.substr(pos + 5);
      // yyyy-mm-dd = 10 chars
      string end_date = after_end.substr(0, 10);
      Expect(end_date.length()).ToBe(10);
      Expect(end_date[4]).ToBe('-');
      Expect(end_date[7]).ToBe('-');
    });

    it("should have endTime in hh:mm:ss format", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --seconds-ago 300", stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);

      auto pos = stderr_output.find("End: ");
      Expect(pos).Not().ToBe(string::npos);
      string after_end = stderr_output.substr(pos + 5);
      // skip date (10 chars) + space (1 char) = 11
      string end_time = after_end.substr(11, 8);
      Expect(end_time[2]).ToBe(':');
      Expect(end_time[5]).ToBe(':');
    });

    it("should have end timestamp >= start timestamp", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines 5",
                           stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);

      auto start_pos = stderr_output.find("Start: ");
      auto end_pos = stderr_output.find("End: ");
      Expect(start_pos).Not().ToBe(string::npos);
      Expect(end_pos).Not().ToBe(string::npos);

      string start_str = stderr_output.substr(start_pos + 7, 21);
      string end_str = stderr_output.substr(end_pos + 5, 21);
      Expect(end_str >= start_str).ToBe(true);
    });

    it("should have different end timestamps for different ranges", []()
    {
      int rc = 0;
      string stdout1, stderr1, stdout2, stderr2;

      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines 3", stdout1,
                           stderr1);
      ExpectWithContext(rc, stderr1).ToBe(0);

      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines 10", stdout2,
                           stderr2);
      ExpectWithContext(rc, stderr2).ToBe(0);

      auto end_pos1 = stderr1.find("End: ");
      auto end_pos2 = stderr2.find("End: ");
      Expect(end_pos1).Not().ToBe(string::npos);
      Expect(end_pos2).Not().ToBe(string::npos);

      string end1 = stderr1.substr(end_pos1 + 5, 21);
      string end2 = stderr2.substr(end_pos2 + 5, 21);
      Expect(end2 >= end1).ToBe(true);
    });

    it("should include endDate/endTime with --seconds-ago", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --seconds-ago 60", stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stderr_output).ToContain("End:");
    });

    it("should include endDate/endTime with explicit --date/--time", []()
    {
      int rc = 0;
      string stdout_output, stderr_output;
      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines 3",
                           stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stderr_output).ToContain("End:");
    });

    it("should allow pagination using endDate/endTime from previous call", []()
    {
      int rc = 0;
      string stdout1, stderr1;

      rc = execute_command(zo_command + " system view-syslog --date 1990-01-01 --time 00:00:00 --max-lines 3", stdout1,
                           stderr1);
      ExpectWithContext(rc, stderr1).ToBe(0);

      auto end_pos = stderr1.find("End: ");
      Expect(end_pos).Not().ToBe(string::npos);
      string end_date = stderr1.substr(end_pos + 5, 10);
      string end_time = stderr1.substr(end_pos + 16, 8);

      string stdout2, stderr2;
      rc = execute_command(zo_command + " system view-syslog --date " + end_date + " --time " + end_time +
                               " --max-lines 3",
                           stdout2, stderr2);
      ExpectWithContext(rc, stderr2).ToBe(0);
      Expect(stdout2.length()).ToBeGreaterThan(0);

      auto end_pos2 = stderr2.find("End: ");
      Expect(end_pos2).Not().ToBe(string::npos);
      string end2 = stderr2.substr(end_pos2 + 5, 21);
      string end1 = stderr1.substr(end_pos + 5, 21);
      Expect(end2 >= end1).ToBe(true);
    });
  });
}
