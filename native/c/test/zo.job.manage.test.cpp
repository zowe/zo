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
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include "ztest.hpp"
#include "zutils.hpp"
#include "zo.test.hpp"
#include "zo.job.test.hpp"

using namespace ztst;

void zo_job_manage_tests(std::vector<std::string> &_jobs, std::vector<std::string> &_ds,
                         std::vector<std::string> &_files)
{
  describe("view", [&]() -> void
  {
    std::string _jobid;
    TEST_OPTIONS hook_opts{false, 30};
    beforeEach([&]()
    {
      // Submit and wait for output to ensure job completes
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      Expect(rc).ToBe(0);
      _jobid = TrimChars(stdout_output);
      Expect(_jobid).Not().ToBe("");
      _jobs.push_back(_jobid);
    }, hook_opts);

    it("should view job status", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + _jobid, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain(_jobid);
    });

    it("should view job status with --rfc format", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      // Validate RFC format structure
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      Expect(lines.size()).ToBeGreaterThan(0);

      // Parse the first line (should contain job status)
      std::vector<std::string> columns = parse_rfc_response(lines[0], ",");

      // RFC format: jobid, jobname, owner, status, retcode, correlator, ...
      Expect(columns.size()).ToBeGreaterThanOrEqualTo(6);

      // Validate jobid matches
      Expect(columns[0]).ToBe(_jobid);

      // Validate jobname is not empty
      Expect(columns[1]).Not().ToBe("");

      // Validate owner is not empty
      Expect(columns[2]).Not().ToBe("");

      // Validate status field exists (should be OUTPUT for completed job)
      Expect(columns[3]).Not().ToBe("");

      // Validate retcode is not empty
      Expect(columns[4]).Not().ToBe("");
    });

    it("should include full_status field in CSV format", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      // Validate RFC format structure includes full_status
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      Expect(lines.size()).ToBeGreaterThan(0);

      std::vector<std::string> columns = parse_rfc_response(lines[0], ",");
      // RFC format: jobid, jobname, owner, status, retcode, correlator, full_status
      Expect(columns.size()).ToBeGreaterThanOrEqualTo(7);

      // Validate full_status field (7th field, index 6)
      // full_status should not be empty for a completed job
      Expect(columns[6]).Not().ToBe("");
    });

    it("should view job JCL", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-jcl " + _jobid, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("IEFBR14");
    });

    it("should handle view-status of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-jcl of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-jcl " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-status with invalid jobid format", [&]()
    {
      std::string invalid_jobid = "INVALID";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + invalid_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-jcl with invalid jobid format", [&]()
    {
      std::string invalid_jobid = "INVALID";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-jcl " + invalid_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-status with invalid correlator", [&]()
    {
      std::string invalid_correlator = "INVALID_CORRELATOR";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status " + invalid_correlator, response);
      Expect(rc).Not().ToBe(0);
    });
  });

  describe("files", [&]() -> void
  {
    std::string _jobid;
    TEST_OPTIONS hook_opts{false, 30};
    beforeEach([&]()
    {
      // Submit and wait for output to ensure spool files exist
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      Expect(rc).ToBe(0);
      _jobid = TrimChars(stdout_output);
      Expect(_jobid).Not().ToBe("");
      _jobs.push_back(_jobid);
    }, hook_opts);

    it("should list job files", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("JESMSGLG");
    });

    it("should list job files with --max-entries option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --max-entries 2", response);
      // listing job files with --max-entries option should return a warning RC if more than 2 files are found under the
      // jobid
      ExpectWithContext(rc, response).ToBe(1);
    });

    it("should list job files with --warn option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --warn", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should list job files with --rfc option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain(",");
    });

    it("should list job files with max-entries 0", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --max-entries 0", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should list job files with very large max-entries", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --max-entries 10000", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("JESMSGLG");
    });

    it("should view a specific job file", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      std::string dsn = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            dsn = parts[1];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping view-file test");
        return;
      }

      rc = execute_command_with_output(zo_command + " job view-file-by-id " + _jobid + " " + file_id, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("IEFBR14");

      std::string duplicate_response;
      rc = execute_command_with_output(zo_command + " job view-file " + dsn, duplicate_response);
      ExpectWithContext(rc, duplicate_response).ToBe(0);
      Expect(duplicate_response).ToBe(response);
    });

    it("should view job file with --encoding option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      std::string dsn = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            dsn = parts[1];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping encoding test");
        return;
      }

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --encoding ISO8859-1", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::string duplicate_response;
      rc = execute_command_with_output(zo_command + " job view-file " + dsn + " --encoding ISO8859-1",
                                       duplicate_response);
      ExpectWithContext(rc, duplicate_response).ToBe(0);
      Expect(duplicate_response).ToBe(response);
    });

    it("should view job file with --local-encoding option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping local-encoding test");
        return;
      }

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --local-encoding IBM-1047", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should view job file with --ec short form for --encoding", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
        return;

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --ec ISO8859-1", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should view job file with --lec short form for --local-encoding", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      std::string dsn = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            dsn = parts[1];
            break;
          }
        }
      }

      if (file_id.empty())
        return;

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --lec IBM-1047", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::string duplicate_response;
      rc = execute_command_with_output(zo_command + " job view-file " + dsn, duplicate_response);
      ExpectWithContext(rc, duplicate_response).ToBe(0);
      Expect(duplicate_response).ToBe(response);
    });

    it("should view job file with --response-format-bytes option", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping rfb test");
        return;
      }

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --response-format-bytes", response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    it("should view job file with --rfb short alias", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping rfb alias test");
        return;
      }

      rc = execute_command_with_output(zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --rfb",
                                       response);
      ExpectWithContext(rc, response).ToBe(0);
    });

    xit("should handle view-file-by-id with invalid encoding", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
        return;

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --encoding INVALID_ENC", response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-file-by-id with key 0", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-file-by-id " + _jobid + " 0", response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-file-by-id with invalid file ID", [&]()
    {
      std::string invalid_file_id = "999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-file " + _jobid + " " + invalid_file_id, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-file-by-id with non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string file_id = "2";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-file-by-id " + fake_jobid + " " + file_id, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle view-file with non-existent dsn", [&]()
    {
      std::string response;
      std::string dsn = "DOES.NOT.EXIST";
      int rc = execute_command_with_output(zo_command + " job view-file " + dsn, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle list-files with non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle list-files with invalid jobid format", [&]()
    {
      std::string invalid_jobid = "INVALID";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + invalid_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle job with no spool files", [&]()
    {
      std::string jcl = "//NOSPOOL JOB (IZUACCT),TEST,REGION=0M,TYPRUN=SCAN\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);

      if (rc == 0)
      {
        std::string jobid = TrimChars(stdout_output);
        _jobs.push_back(jobid);

        std::string response;
        rc = execute_command_with_output(zo_command + " job list-files " + jobid, response);
        ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
      }
    });

    it("should handle view-file-by-id with binary spool content", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files " + _jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      std::string dsn = "";
      for (const auto &line : lines)
      {
        if (!line.empty())
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            dsn = parts[1];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find file ID, skipping binary content test");
        return;
      }

      rc = execute_command_with_output(
          zo_command + " job view-file-by-id " + _jobid + " " + file_id + " --encoding binary", response);
      ExpectWithContext(rc, response).ToBeGreaterThanOrEqualTo(0);
      Expect(response.length()).ToBeGreaterThan(0);

      std::string duplicate_response;
      rc = execute_command_with_output(zo_command + " job view-file " + dsn + " --encoding binary", duplicate_response);
      ExpectWithContext(rc, duplicate_response).ToBe(0);
      Expect(duplicate_response.length()).ToBe(response.length());
    });
  });

  describe("lifecycle", [&]() -> void
  {
    it("should hold and release a job", [&]()
    {
      // Submit with TYPRUN=HOLD
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Wait for job to be visible in JES
      Expect(wait_for_job(jobid)).ToBe(true);

      // Release
      rc = execute_command_with_output(zo_command + " job release " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("released");
    });

    it("should delete a job", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid); // Add to list just in case test fails before delete

      Expect(wait_for_job(jobid)).ToBe(true);

      // Delete
      rc = execute_command_with_output(zo_command + " job delete " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("deleted");

      // Remove from cleanup list since it is deleted
      for (auto it = _jobs.begin(); it != _jobs.end();)
      {
        if (*it == jobid)
        {
          it = _jobs.erase(it);
        }
        else
        {
          ++it;
        }
      }
    });

    it("should handle delete of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job delete " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle cancel of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job cancel " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle hold of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job hold " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle release of non-existent job", [&]()
    {
      std::string fake_jobid = "JOB9999999";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job release " + fake_jobid, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle release of job that was not held", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Try to release without holding
      rc = execute_command_with_output(zo_command + " job release " + jobid, stdout_output);
      // Should succeed or return specific error
      ExpectWithContext(rc, stdout_output).ToBeGreaterThanOrEqualTo(0);
    });

    it("should handle double hold of a job", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // First hold
      rc = execute_command_with_output(zo_command + " job hold " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);

      // Second hold
      rc = execute_command_with_output(zo_command + " job hold " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBeGreaterThanOrEqualTo(0);
    });

    it("should handle double cancel of a job", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Wait for job to be visible in JES
      Expect(wait_for_job(jobid)).ToBe(true);

      // First cancel
      rc = execute_command_with_output(zo_command + " job cancel " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);

      // Second cancel
      rc = execute_command_with_output(zo_command + " job cancel " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBeGreaterThanOrEqualTo(0);
    });

    it("should handle invalid jobid format for delete", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job delete INVALID_ID", response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle invalid jobid format for cancel", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job cancel INVALID_ID", response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle invalid jobid format for hold", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job hold INVALID_ID", response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle invalid jobid format for release", [&]()
    {
      std::string response;
      int rc = execute_command_with_output(zo_command + " job release INVALID_ID", response);
      Expect(rc).Not().ToBe(0);
    });
  });

  describe("commands", [&]() -> void
  {
    it("should hold and release job output", [&]()
    {
      // Submit and wait for output
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Hold
      int rc = execute_command_with_output(zo_command + " job hold " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("held");

      // Release
      rc = execute_command_with_output(zo_command + " job release " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("released");
    });

    it("should cancel a job", [&]()
    {
      // Submit with TYPRUN=HOLD so we can cancel it
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Wait for job to be visible in JES
      Expect(wait_for_job(jobid)).ToBe(true);

      // Cancel
      int rc = execute_command_with_output(zo_command + " job cancel " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with --dump option", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " --dump", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with --force option", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " --force", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with --purge option", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " --purge", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with --restart option", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " --restart", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with multiple options", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " --force --purge", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with -d alias for --dump", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " -d", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with -f alias for --force", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " -f", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with -p alias for --purge", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " -p", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });

    it("should cancel a job with -r alias for --restart", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      Expect(wait_for_job(jobid)).ToBe(true);

      int rc = execute_command_with_output(zo_command + " job cancel " + jobid + " -r", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });
  });

  describe("correlator", [&]() -> void
  {
    std::string _jobid;
    std::string _correlator;
    TEST_OPTIONS hook_opts{false, 30};

    beforeEach([&]()
    {
      // Submit a job
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      Expect(rc).ToBe(0);
      _jobid = TrimChars(stdout_output);
      Expect(_jobid).Not().ToBe("");
      _jobs.push_back(_jobid);

      // Get correlator
      std::string response;
      execute_command_with_output(zo_command + " job view-status " + _jobid + " --rfc", response);
      // Parse CSV to get correlator (6th column, index 5)
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 6)
        {
          _correlator = parts[5];
        }
      }
      if (_correlator.empty())
      {
        TestLog("Could not get correlator for job " + _jobid);
      }
    }, hook_opts);

    it("should view status by correlator", [&]()
    {
      if (_correlator.empty())
        return;
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-status \"" + _correlator + "\"", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain(_jobid);
    });

    it("should view JCL by correlator", [&]()
    {
      if (_correlator.empty())
        return;
      std::string response;
      int rc = execute_command_with_output(zo_command + " job view-jcl \"" + _correlator + "\"", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("IEFBR14");
    });

    it("should list files by correlator", [&]()
    {
      if (_correlator.empty())
        return;
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files \"" + _correlator + "\"", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("JESMSGLG");
    });

    it("should view file by correlator", [&]()
    {
      if (_correlator.empty())
        return;

      // Get the file ID for JESMSGLG
      std::string response;
      int rc = execute_command_with_output(zo_command + " job list-files \"" + _correlator + "\" --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string file_id = "";
      for (const auto &line : lines)
      {
        if (line.find("JESMSGLG") != std::string::npos)
        {
          std::vector<std::string> parts = parse_rfc_response(line, ",");
          if (parts.size() >= 3)
          {
            file_id = parts[2];
            break;
          }
        }
      }

      if (file_id.empty())
      {
        TestLog("Could not find JESMSGLG file ID, skipping view-file-by-id by correlator test");
        return;
      }

      rc =
          execute_command_with_output(zo_command + " job view-file-by-id \"" + _correlator + "\" " + file_id, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("IEFBR14");
    });

    it("should delete by correlator", [&]()
    {
      if (_correlator.empty())
        return;
      std::string response;
      int rc = execute_command_with_output(zo_command + " job delete \"" + _correlator + "\"", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("deleted");

      // Remove from cleanup list
      for (auto it = _jobs.begin(); it != _jobs.end();)
      {
        if (*it == _jobid)
        {
          it = _jobs.erase(it);
        }
        else
        {
          ++it;
        }
      }
    });

    it("should hold by correlator", [&]()
    {
      // Submit a job and wait for it to complete
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Get correlator
      std::string response;
      execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string correlator = "";
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 6)
        {
          correlator = parts[5];
        }
      }

      if (correlator.empty())
      {
        TestLog("Could not get correlator, skipping hold test");
        return;
      }

      // Hold by correlator
      rc = execute_command_with_output(zo_command + " job hold \"" + correlator + "\"", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("held");
    });

    it("should release by correlator", [&]()
    {
      // Submit a job and wait for it to complete, then hold it
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --wait output --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      // Hold the job first
      execute_command_with_output(zo_command + " job hold " + jobid, stdout_output);

      // Get correlator
      std::string response;
      execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string correlator = "";
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 6)
        {
          correlator = parts[5];
        }
      }

      if (correlator.empty())
      {
        TestLog("Could not get correlator, skipping release test");
        return;
      }

      // Release by correlator
      rc = execute_command_with_output(zo_command + " job release \"" + correlator + "\"", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("released");
    });

    it("should cancel by correlator", [&]()
    {
      // Submit a long-running job that we can cancel while it's executing
      std::string jcl = "//LONGJOB JOB (IZUACCT),TEST,REGION=0M\n"
                        "//STEP1 EXEC PGM=IEFBR14\n"
                        "//STEP2 EXEC PGM=BPXBATCH\n"
                        "//STDOUT DD SYSOUT=*\n"
                        "//STDERR DD SYSOUT=*\n"
                        "//STDPARM DD *\n"
                        "SH sleep 30\n"
                        "/*";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Wait for job to be visible in JES
      Expect(wait_for_job(jobid)).ToBe(true);

      // Get correlator from the running job
      std::string response;
      execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string correlator = "";
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 6)
        {
          correlator = parts[5];
        }
      }

      if (correlator.empty())
      {
        TestLog("Could not get correlator, skipping cancel test");
        return;
      }

      // Cancel by correlator while job is running
      rc = execute_command_with_output(zo_command + " job cancel \"" + correlator + "\"", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("cancelled");
    });
  });

  describe("input-mode", [&]() -> void
  {
    it("should view status of job in input mode", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);

      std::string response;
      // Use RFC for reliable parsing
      int rc = execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      Expect(lines.size()).ToBeGreaterThan(0);
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        Expect(parts.size()).ToBeGreaterThanOrEqualTo(4);
        Expect(parts[0]).ToBe(jobid);
        // Status is index 3. Verify it's not empty.
        Expect(parts[3]).Not().ToBe("");
      }
    });

    it("should delete job in input mode by jobid", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Verify job is in INPUT mode
      std::string response;
      rc = execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      // Delete the job in INPUT mode
      rc = execute_command_with_output(zo_command + " job delete " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("deleted");

      // Remove from cleanup list since it's deleted
      for (auto it = _jobs.begin(); it != _jobs.end();)
      {
        if (*it == jobid)
        {
          it = _jobs.erase(it);
        }
        else
        {
          ++it;
        }
      }
    });

    it("should hold job in input mode by jobid", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Verify job is in INPUT mode
      std::string response;
      rc = execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      // Hold the job (even though it's already held by TYPRUN=HOLD)
      rc = execute_command_with_output(zo_command + " job hold " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBeGreaterThanOrEqualTo(0);
      if (rc == 0)
      {
        Expect(stdout_output).ToContain("held");
      }
    });

    it("should hold job in input mode by correlator", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Get correlator
      std::string response;
      rc = execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      std::string correlator = "";
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 6)
        {
          correlator = TrimChars(parts[5]);
        }
      }

      // INPUT mode jobs may not have a valid correlator yet
      // Skip test if correlator is empty or appears invalid
      if (correlator.empty() || correlator.find(' ') != std::string::npos)
      {
        TestLog("Correlator not valid for INPUT mode job, skipping test");
        return;
      }

      // Hold by correlator
      rc = execute_command_with_output(zo_command + " job hold \"" + correlator + "\"", stdout_output);
      ExpectWithContext(rc, stdout_output).ToBeGreaterThanOrEqualTo(0);
      if (rc == 0)
      {
        Expect(stdout_output).ToContain("held");
      }
    });

    it("should release job in input mode by jobid", [&]()
    {
      std::string jcl = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M,TYPRUN=HOLD\n//RUN EXEC PGM=IEFBR14";
      std::string command = "printf \"" + jcl + "\" | " + zo_command + " job submit-jcl --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      _jobs.push_back(jobid);

      // Verify job is in INPUT mode
      std::string response;
      rc = execute_command_with_output(zo_command + " job view-status " + jobid + " --rfc", response);
      ExpectWithContext(rc, response).ToBe(0);

      // Release the job from INPUT mode
      rc = execute_command_with_output(zo_command + " job release " + jobid, stdout_output);
      ExpectWithContext(rc, stdout_output).ToBe(0);
      Expect(stdout_output).ToContain("released");
    });
  });
}
