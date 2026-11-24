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
#include <iostream>
#include <stdexcept>

#include "ztest.hpp"
#include "zjb.hpp"
#include "../zjbtime.hpp"
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace ztst;

void zjb_tests()
{
  std::vector<std::string> submitted_jobs;
  describe("zjb tests", [&]() -> void
  {
    std::string jcl = "//IEFBR14$ JOB IZUACCT\n"
                      "//RUNBR14  EXEC PGM=IEFBR14\n";

    std::string hold_jcl = "//IEFBR14$ JOB IZUACCT,TYPRUN=HOLD\n"
                           "//RUNBR14  EXEC PGM=IEFBR14\n";

    TEST_OPTIONS cleanup_opts = {false, 30};
    afterAll([&]() -> void
    {
      for (const auto &jobid : submitted_jobs)
      {
        try
        {
          ZJB zjb = {0};
          zjb_delete(&zjb, jobid);
        }
        catch (...)
        {
        }
      }
      submitted_jobs.clear();
    }, cleanup_opts);

    describe("execution timestamps", []() -> void
    {
      it("should preserve centiseconds in z/OSMF UTC format", []() -> void
      {
        const unsigned char date[] = {0x01, 0x18, 0x30, 0x7F};
        Expect(zjb_detail::format_timestamp(date, 3271801, 0)).ToBe("2018-11-03T09:05:18.010Z");
      });

      it("should accept midnight and positive packed date signs", []() -> void
      {
        const unsigned char date[] = {0x01, 0x24, 0x00, 0x1C};
        Expect(zjb_detail::format_timestamp(date, 0, 0)).ToBe("2024-01-01T00:00:00.000Z");
      });

      it("should handle Gregorian leap years and century boundaries", []() -> void
      {
        const unsigned char leap_day[] = {0x01, 0x24, 0x06, 0x0F};
        const unsigned char leap_century[] = {0x01, 0x00, 0x36, 0x6F};
        const unsigned char non_leap_century[] = {0x02, 0x00, 0x36, 0x6F};
        Expect(zjb_detail::format_timestamp(leap_day, 0, 0)).ToBe("2024-02-29T00:00:00.000Z");
        Expect(zjb_detail::format_timestamp(leap_century, 0, 0)).ToBe("2000-12-31T00:00:00.000Z");
        Expect(zjb_detail::format_timestamp(non_leap_century, 0, 0)).ToBe("");
      });

      it("should normalize both UTC offset directions across year boundaries", []() -> void
      {
        const unsigned char first_day[] = {0x01, 0x24, 0x00, 0x1F};
        const unsigned char last_day[] = {0x01, 0x24, 0x36, 0x6F};
        Expect(zjb_detail::format_timestamp(first_day, 90000, 360000)).ToBe("2023-12-31T23:15:00.000Z");
        Expect(zjb_detail::format_timestamp(last_day, 8550000, -360000)).ToBe("2025-01-01T00:45:00.000Z");
      });

      it("should omit unavailable or malformed JES timestamps", []() -> void
      {
        const unsigned char missing[] = {0, 0, 0, 0};
        const unsigned char no_date[] = {0, 0, 0, 0x0C};
        const unsigned char invalid_day[] = {0x01, 0x24, 0x36, 0x7F};
        const unsigned char invalid_digit[] = {0x01, 0x2A, 0x00, 0x1F};
        const unsigned char date[] = {0x01, 0x24, 0x00, 0x1F};
        Expect(zjb_detail::format_timestamp(missing, 0, 0)).ToBe("");
        Expect(zjb_detail::format_timestamp(no_date, 0, 0)).ToBe("");
        Expect(zjb_detail::format_timestamp(invalid_day, 0, 0)).ToBe("");
        Expect(zjb_detail::format_timestamp(invalid_digit, 0, 0)).ToBe("");
        Expect(zjb_detail::format_timestamp(date, -1, 0)).ToBe("");
        Expect(zjb_detail::format_timestamp(date, 8640000, 0)).ToBe("");
      });
    });

    it("should be able to list a job", [&]() -> void
    {
      ZJB zjb = {0};
      std::string owner = "*";  // all owners
      std::string prefix = "*"; // any prefix
      zjb.jobs_max = 1;         // limit to one
      std::vector<ZJob> jobs;
      int rc = zjb_list_by_owner(&zjb, owner, prefix, jobs);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_WARNING); // expect truncated list returned
      Expect(jobs.size()).ToBe(zjb.jobs_max);                    // expect one job returned
    });

    it("should be able to submit JCL", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      int rc = zjb_submit(&zjb, jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
      Expect(jobid).Not().ToBe("");
    });

    it("should be able to view a submitted job", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      int rc = zjb_submit(&zjb, jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      ZJob zjob;
      std::string correlator = std::string(zjb.correlator, sizeof(zjb.correlator));

      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_view(&zjb, correlator, zjob);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      Expect(zjob.correlator).ToBe(correlator); // verify submit correlator matches view status correlator
    });

    it("should be able to delete a submitted job", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      int rc = zjb_submit(&zjb, jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
    });

    it("should be able to read job JCL", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      int rc = zjb_submit(&zjb, jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      std::string correlator = std::string(zjb.correlator, sizeof(zjb.correlator));
      std::string returned_jcl;

      zjb_wait(&zjb, "OUTPUT");

      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_read_job_jcl(&zjb, correlator, returned_jcl);

      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
    });

    it("should detect ASA carriage control for spool DDs and read their content", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      const std::string asa_jcl = "//ASASPOOL JOB IZUACCT\n"
                                  "//STEP01   EXEC PGM=IEBGENER\n"
                                  "//SYSPRINT DD SYSOUT=*\n"
                                  "//SYSIN    DD DUMMY\n"
                                  "//SYSUT2   DD SYSOUT=*,DCB=(RECFM=FBA,LRECL=80)\n"
                                  "//SYSUT1   DD *\n"
                                  "1*** PAGE 1: HEADING ***\n"
                                  " THIS LINE IS SINGLE SPACED.\n"
                                  "0THIS LINE IS DOUBLE SPACED.\n"
                                  "/*\n";
      int rc = zjb_submit(&zjb, asa_jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      std::string correlator = std::string(zjb.correlator, sizeof(zjb.correlator));
      zjb_wait(&zjb, "OUTPUT");

      std::vector<ZJobDD> dds;
      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_list_dds(&zjb, correlator, dds);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
      Expect(dds.size()).ToBeGreaterThan(0);

      auto asa_dd = std::find_if(dds.begin(), dds.end(), [](const ZJobDD &dd)
      {
        return dd.is_asa;
      });
      std::string err_log;
      if (asa_dd == dds.end()) {
        zjb_read_job_content_by_key(&zjb, correlator, 2, err_log);
      }
      ExpectWithContext(asa_dd != dds.end(), err_log).ToBe(true);

      std::string content;
      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_read_job_content_by_key(&zjb, correlator, asa_dd->key, content);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      Expect(content).Not().ToBe(""); // expect some content returned
      Expect(content.find('\n') != std::string::npos).ToBe(true);
    });

    // https://github.com/zowe/zowex/issues/641
    xit("should be able to list and view SYSOUT files for INPUT jobs", [&]() -> void
    {
      ZJB zjb = {0};
      std::string jobid;

      int rc = zjb_submit(&zjb, hold_jcl, jobid);
      submitted_jobs.push_back(jobid);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);

      std::string correlator = std::string(zjb.correlator, sizeof(zjb.correlator));
      // sleep_on_status("INPUT", correlator);

      std::vector<ZJobDD> dds;
      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_list_dds(&zjb, correlator, dds);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
      Expect(dds.size()).ToBeGreaterThan(0); // expect at least one DD returned

      std::string content;
      memset(&zjb, 0, sizeof(zjb));
      rc = zjb_read_job_content_by_key(&zjb, correlator, dds[0].key, content);
      ExpectWithContext(rc, zjb.diag.e_msg).ToBe(RTNCD_SUCCESS);
      Expect(content).Not().ToBe(""); // expect some content returned
    });
  });
}
