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

void zo_job_submit_tests(std::vector<std::string> &_jobs, std::vector<std::string> &_ds,
                         std::vector<std::string> &_files)
{
  describe("submit", [&]() -> void
  {
    const std::string jcl_base = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";

    it("should submit JCL from stdin", [&]()
    {
      std::string jobid;
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --only-jobid";

      int rc = execute_command(command, stdout_output, stderr_output);
      jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);

      execute_command_with_output(zo_command + " job list --owner *", stdout_output);
    });

    it("should submit JCL with --oj short form for --only-jobid", [&]()
    {
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --oj";

      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);
    });

    it("should submit JCL with --oc short form for --only-correlator", [&]()
    {
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --oc";

      int rc = execute_command(command, stdout_output, stderr_output);
      std::string correlator = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(correlator).Not().ToBe("");

      // Get jobid for cleanup using correlator (RFC format: jobid,retcode,jobname,status,correlator)
      std::string response;
      execute_command_with_output(zo_command + " job view-status \"" + correlator + "\" --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 1)
        {
          _jobs.push_back(parts[0]);
        }
      }
    });

    it("should submit JCL and wait for output", [&]()
    {
      std::string jobid;
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --wait output";

      int rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted JCL");

      // Extract jobid for cleanup if possible
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit JCL and wait for ACTIVE status", [&]()
    {
      std::string jobid;
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --wait ACTIVE";

      int rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted JCL");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit JCL and wait for pattern in spool files", [&]()
    {
      std::string jobid;
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --only-jobid";
      int rc = execute_command(command, stdout_output, stderr_output);
      jobid = TrimChars(stdout_output);
      _jobs.push_back(jobid);
      ExpectWithContext(rc, stderr_output).ToBe(0);

      std::string response;
      execute_command(zo_command + " job list-files " + jobid + " --response-format-csv", stdout_output, stderr_output);
      std::vector<std::string> lines = parse_rfc_response(stdout_output, "\n");
      std::string job_dsn = "";
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 1)
        {
          job_dsn = parts[1];
        }
      }

      command = zo_command + " job watch " + job_dsn + " --pattern \"IEFBR14  ENDED\"";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("'string' pattern");
      Expect(stdout_output).ToContain("job spool files matched");
      Expect(stdout_output).ToContain("IEFBR14  ENDED");

      command = zo_command + " job watch " + job_dsn + " --pattern \"iefbr14  ended\" --ignore-case";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("job spool files matched");
      Expect(stdout_output).ToContain("IEFBR14  ENDED");

      // Test alias 'wch' for 'watch', um, and regex
      command = zo_command + " job wch " + job_dsn + " -p \"/^.*IEFBR14.*ENDED.*$/\" --mws 1";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("'Regex' pattern");
      Expect(stdout_output).ToContain("job spool files matched");

      // Test that this fails waiting beyond what is permitted by the --mws option
      command = zo_command + " job watch " + job_dsn + " --pattern \"IEFBR14 ENDED\" --mws 301";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).Not().ToBe(0);

      // Test that this fails when waiting for 0 seconds
      command = zo_command + " job wch " + job_dsn + " -p \"/^.*IEFBR14.*ENDED.*$/\" --mws 0";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).Not().ToBe(0);
      Expect(stderr_output).ToContain("Error: max-wait-seconds must be greater than 0 seconds");

      // Test that this fails when waiting for a regex pattern with --ignore-case
      command = zo_command + " job wch " + job_dsn + " -p \"/^.*IEFBR14.*ENDED.*$/\" --mws 1 --ignore-case";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).Not().ToBe(0);
      Expect(stderr_output).ToContain("Error: ignore-case is not supported for regex patterns");
    });

    it("should submit from a data set", [&]()
    {
      // Create DS (PS)
      std::string ds = get_random_ds();
      _ds.push_back(ds);
      std::string response;
      int rc = execute_command_with_output(zo_command + " data-set create " + ds + " --dsorg PS --recfm FB --lrecl 80",
                                           response);
      ExpectWithContext(rc, response).ToBe(0);

      // Write JCL
      std::string command = "echo \"" + jcl_base + "\" | " + zo_command + " data-set write " + ds;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);

      // Submit
      std::string stdout_output, stderr_output;
      command = zo_command + " job submit " + ds + " --only-jobid";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);
    });

    it("should submit from a USS file", [&]()
    {
      // Create local file
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      // Use printf to write the file to ensure correct newline handling and encoding
      std::string response;
      std::string cmd = "printf \"" + jcl_base + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);

      // Ensure file is untagged so zo reads raw EBCDIC without conversion
      execute_command_with_output("chtag -r " + filename, response);

      // Submit
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --only-jobid";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);
    });

    it("should submit JCL with --only-correlator option", [&]()
    {
      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --only-correlator";

      int rc = execute_command(command, stdout_output, stderr_output);
      std::string correlator = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(correlator).Not().ToBe("");

      // Get jobid for cleanup using correlator (RFC format: jobid,retcode,jobname,status,correlator)
      std::string response;
      execute_command_with_output(zo_command + " job view-status " + correlator + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 1)
        {
          _jobs.push_back(parts[0]);
        }
      }
    });

    it("should submit from data set with --only-correlator", [&]()
    {
      // Create DS
      std::string ds = get_random_ds();
      _ds.push_back(ds);
      std::string response;
      int rc = execute_command_with_output(zo_command + " data-set create " + ds + " --dsorg PS --recfm FB --lrecl 80",
                                           response);
      ExpectWithContext(rc, response).ToBe(0);

      // Write JCL
      std::string command = "echo \"" + jcl_base + "\" | " + zo_command + " data-set write " + ds;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);

      // Submit with --only-correlator
      std::string stdout_output, stderr_output;
      command = zo_command + " job submit " + ds + " --only-correlator";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string correlator = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(correlator).Not().ToBe("");

      // Get jobid for cleanup (RFC format: jobid,retcode,jobname,status,correlator)
      execute_command_with_output(zo_command + " job view-status " + correlator + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 1)
        {
          _jobs.push_back(parts[0]);
        }
      }
    });

    it("should submit from USS file with --only-correlator", [&]()
    {
      // Create local file
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      std::string response;
      std::string cmd = "printf \"" + jcl_base + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);
      execute_command_with_output("chtag -r " + filename, response);

      // Submit with --only-correlator
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --only-correlator";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string correlator = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(correlator).Not().ToBe("");

      // Get jobid for cleanup (RFC format: jobid,retcode,jobname,status,correlator)
      execute_command_with_output(zo_command + " job view-status " + correlator + " --rfc", response);
      std::vector<std::string> lines = parse_rfc_response(response, "\n");
      if (lines.size() > 0)
      {
        std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
        if (parts.size() >= 1)
        {
          _jobs.push_back(parts[0]);
        }
      }
    });

    it("should submit from data set with --wait option", [&]()
    {
      // Create DS
      std::string ds = get_random_ds();
      _ds.push_back(ds);
      std::string response;
      int rc = execute_command_with_output(zo_command + " data-set create " + ds + " --dsorg PS --recfm FB --lrecl 80",
                                           response);
      ExpectWithContext(rc, response).ToBe(0);

      // Write JCL
      std::string command = "echo \"" + jcl_base + "\" | " + zo_command + " data-set write " + ds;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);

      // Submit with --wait output
      std::string stdout_output, stderr_output;
      command = zo_command + " job submit " + ds + " --wait output";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        std::string jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit from data set with --wait ACTIVE option", [&]()
    {
      std::string ds = get_random_ds();
      _ds.push_back(ds);
      std::string response;
      int rc = execute_command_with_output(zo_command + " data-set create " + ds + " --dsorg PS --recfm FB --lrecl 80",
                                           response);
      ExpectWithContext(rc, response).ToBe(0);

      std::string command = "echo \"" + jcl_base + "\" | " + zo_command + " data-set write " + ds;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);

      // Submit with --wait ACTIVE
      std::string stdout_output, stderr_output;
      command = zo_command + " job submit " + ds + " --wait ACTIVE";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        std::string jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit from USS file with --wait option", [&]()
    {
      // Create local file
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      std::string response;
      std::string cmd = "printf \"" + jcl_base + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);
      execute_command_with_output("chtag -r " + filename, response);

      // Submit with --wait output
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --wait output";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        std::string jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit from USS file with --only-jobid option", [&]()
    {
      // Create local file
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      std::string response;
      std::string cmd = "printf \"" + jcl_base + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);
      execute_command_with_output("chtag -r " + filename, response);

      // Submit with --only-jobid
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --only-jobid";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      // Verify it's just the jobid
      Expect(jobid.find("Submitted")).ToBe(std::string::npos);
      _jobs.push_back(jobid);
    });

    it("should submit from USS file with --wait ACTIVE option", [&]()
    {
      std::string jcl_long = "//LONGJOB JOB (IZUACCT),TEST,REGION=0M\n//STEP1 EXEC PGM=IEFBR14";
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      std::string response;
      std::string cmd = "printf \"" + jcl_long + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);
      execute_command_with_output("chtag -r " + filename, response);

      // Submit with --wait ACTIVE
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --wait ACTIVE";
      rc = execute_command(command, stdout_output, stderr_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(stdout_output).ToContain("Submitted");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        std::string jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should submit from USS file with --wait and --only-jobid combined", [&]()
    {
      std::string filename = "test_job_" + get_random_string(5) + ".jcl";
      _files.push_back(filename);

      std::string response;
      std::string cmd = "printf \"" + jcl_base + "\" > " + filename;
      int rc = execute_command_with_output(cmd, response);
      ExpectWithContext(rc, response).ToBe(0);
      execute_command_with_output("chtag -r " + filename, response);

      // Submit with --wait output and --only-jobid
      std::string stdout_output, stderr_output;
      std::string command = zo_command + " job submit-uss " + filename + " --wait output --only-jobid";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      // Verify it's just the jobid (no "Submitted" text)
      Expect(jobid.find("Submitted")).ToBe(std::string::npos);
      // Verify jobid format
      Expect(jobid.length()).ToBe(8);
      _jobs.push_back(jobid);
    });
  });

  describe("submit-edge-cases", [&]() -> void
  {
    const std::string jcl_base = "//IEFBR14 JOB (IZUACCT),TEST,REGION=0M\n//RUN EXEC PGM=IEFBR14";

    it("should handle submit from non-existent dataset", [&]()
    {
      std::string ds = "NONEXIST.DS.NAME";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job submit " + ds, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle submit from non-existent USS file", [&]()
    {
      std::string filename = "nonexistent_file_12345.jcl";
      std::string response;
      int rc = execute_command_with_output(zo_command + " job submit-uss " + filename, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle empty JCL submission", [&]()
    {
      std::string command = "echo \"\" | " + zo_command + " job submit-jcl";
      std::string response;
      int rc = execute_command_with_output(command, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle malformed JCL submission", [&]()
    {
      std::string bad_jcl = "THIS IS NOT VALID JCL";
      std::string command = "printf \"" + bad_jcl + "\" | " + zo_command + " job submit-jcl";
      std::string response;
      int rc = execute_command_with_output(command, response);
      Expect(rc).Not().ToBe(0);
    });

    it("should handle submit with encoding option", [&]()
    {
      std::string command =
          "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --encoding IBM-1047 --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      if (rc == 0)
      {
        Expect(jobid).Not().ToBe("");
        _jobs.push_back(jobid);
      }
    });

    it("should handle submit with local-encoding option", [&]()
    {
      std::string command =
          "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --local-encoding ISO8859-1 --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      if (rc == 0)
      {
        Expect(jobid).Not().ToBe("");
        _jobs.push_back(jobid);
      }
    });

    it("should handle conflicting --only-jobid and --only-correlator options", [&]()
    {
      std::string command =
          "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --only-jobid --only-correlator";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);

      // The command should either:
      // 1. Return an error (rc != 0) for conflicting options
      // 2. Use last option specified (implementation-dependent)
      // For now, we just verify it doesn't crash and produces output
      if (rc == 0)
      {
        std::string output = TrimChars(stdout_output);
        Expect(output).Not().ToBe("");

        // Try to determine if it's a jobid or correlator and clean up
        if (output.find("JOB") == 0 && output.length() == 8)
        {
          // Looks like a jobid
          _jobs.push_back(output);
        }
        else if (!output.empty())
        {
          // Might be a correlator, try to get jobid (RFC format: jobid,retcode,jobname,status,correlator)
          std::string response;
          int status_rc =
              execute_command_with_output(zo_command + " job view-status \"" + output + "\" --rfc", response);
          if (status_rc == 0)
          {
            std::vector<std::string> lines = parse_rfc_response(response, "\n");
            if (lines.size() > 0)
            {
              std::vector<std::string> parts = parse_rfc_response(lines[0], ",");
              if (parts.size() >= 1)
              {
                _jobs.push_back(parts[0]);
              }
            }
          }
        }
      }
    });

    it("should handle submit with --ec short form for --encoding", [&]()
    {
      std::string command =
          "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --ec IBM-1047 --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      if (rc == 0)
      {
        Expect(jobid).Not().ToBe("");
        _jobs.push_back(jobid);
      }
    });

    it("should handle submit with --lec short form for --local-encoding", [&]()
    {
      std::string command =
          "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --lec ISO8859-1 --only-jobid";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      if (rc == 0)
      {
        Expect(jobid).Not().ToBe("");
        _jobs.push_back(jobid);
      }
    });

    it("should handle submit from dataset with special characters in name", [&]()
    {
      std::string ds = get_user() + ".T" + get_random_string(3) + "#@.JCL";
      std::string response;
      int rc = execute_command_with_output(zo_command + " data-set create " + ds + " --dsorg PS --recfm FB --lrecl 80",
                                           response);
      ExpectWithContext(rc, response).ToBe(0);

      std::string command = "echo \"" + jcl_base + "\" | " + zo_command + " data-set write " + ds;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);

      std::string stdout_output, stderr_output;
      command = zo_command + " job submit " + ds + " --only-jobid";
      rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);
      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);

      // Clean up dataset
      execute_command_with_output(zo_command + " data-set delete " + ds, response);
    });

    it("should handle submit with invalid wait status value", [&]()
    {
      std::string command = "printf \"" + jcl_base + "\" | " + zo_command + " job submit-jcl --wait INVALID_STATUS";
      std::string stdout_output, stderr_output;
      int rc = execute_command(command, stdout_output, stderr_output);
      Expect(rc).Not().ToBe(0);
      Expect(stderr_output).ToContain("cannot wait for unknown status");

      // Extract jobid for cleanup
      size_t pos = stdout_output.find("(JOB");
      if (pos != std::string::npos)
      {
        std::string jobid = stdout_output.substr(pos + 1, 8);
        _jobs.push_back(jobid);
      }
    });

    it("should successfully submit JCL with CRLF line endings", [&]()
    {
      std::string jcl_crlf = "//CRLFJOB JOB (IZUACCT),TEST,CLASS=A\\r\\n//STEP1 EXEC PGM=IEFBR14\\r\\n";

      std::string stdout_output, stderr_output;
      std::string command = "printf \"" + jcl_crlf + "\" | " + zo_command + " job submit-jcl --only-jobid";

      int rc = execute_command(command, stdout_output, stderr_output);
      std::string jobid = TrimChars(stdout_output);

      ExpectWithContext(rc, stderr_output).ToBe(0);
      Expect(jobid).Not().ToBe("");
      _jobs.push_back(jobid);
    });
  });
}
