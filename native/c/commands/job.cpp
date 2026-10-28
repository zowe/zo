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

#include "job.hpp"
#include "common_args.hpp"
#include "result_table.hpp"
#include "../zds.hpp"
#include "../zjb.hpp"
#include "../zusf.hpp"
#include "../zut.hpp"
#include <regex.h>

using namespace ast;
using namespace parser;
using namespace commands::common;
using namespace commands::format;

namespace job
{

static void add_job_execution_data(const ZJob &job, const ast::Node &result)
{
  if (!job.exec_system.empty())
    result->set("execSystem", str(job.exec_system));
  if (!job.exec_member.empty())
    result->set("execMember", str(job.exec_member));
  if (!job.exec_submitted.empty())
    result->set("execSubmitted", str(job.exec_submitted));
  if (!job.exec_started.empty())
    result->set("execStarted", str(job.exec_started));
  if (!job.exec_ended.empty())
    result->set("execEnded", str(job.exec_ended));
}

/**
 * @brief Parse a string to determine if it's a regex pattern
 *
 * Detects regex patterns in the format: /pattern/
 * Note: POSIX regex doesn't support flags like 'g' or 'i' in the pattern itself
 *
 * @param input The input string to check
 * @param pattern Output parameter for the extracted pattern
 * @return true if input is a regex pattern, false otherwise
 */
bool parse_regex_pattern(const std::string &input, std::string &pattern)
{
  pattern = input;

  // Check for /pattern/ format
  if (input.size() >= 2 && input[0] == '/' && input[input.size() - 1] == '/')
  {
    pattern = input.substr(1, input.size() - 2);
    return true;
  }

  return false;
}

int handle_job_list(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string owner_name = context.get<std::string>("owner", "");
  std::string prefix_name = context.get<std::string>("prefix", "*");
  std::string status_name = context.get<std::string>("status", "*");
  long long max_entries = context.get<long long>("max-entries", 0);
  bool warn = context.get<bool>("warn", true);
  bool exec_data = context.get<bool>("exec-data", false);

  if (max_entries > 0)
  {
    zjb.jobs_max = max_entries;
  }

  std::vector<ZJob> jobs;
  rc = zjb_list_by_owner(&zjb, owner_name, prefix_name, status_name, jobs);

  if (RTNCD_SUCCESS == rc || RTNCD_WARNING == rc)
  {
    // Pad the leading columns only when exec data follows, so the wider rows stay aligned
    const int id_width = exec_data ? 8 : 0;
    ResultTable table(context);
    table.add_column("jobid", id_width)
        .add_column("jobname", id_width)
        .add_column("owner", id_width)
        .add_column("status", 7)
        .add_column("retcode", exec_data ? 12 : 0);
    if (exec_data)
    {
      table.add_column("execsystem", id_width)
          .add_column("execmember", id_width)
          .add_column("execsubmitted", 24)
          .add_column("execstarted", 24)
          .add_column("execended");
    }

    for (const auto &job : jobs)
    {
      const auto entry = obj();
      entry->set("id", str(job.jobid));
      std::string trimmed_name = job.jobname;
      entry->set("name", str(zut_rtrim(trimmed_name)));
      trimmed_name = job.subsystem;
      if (!zut_rtrim(trimmed_name).empty())
        entry->set("subsystem", str(trimmed_name));
      trimmed_name = job.owner;
      entry->set("owner", str(zut_rtrim(trimmed_name)));
      entry->set("status", str(job.status));
      entry->set("type", str(job.type));
      trimmed_name = job.jobclass;
      entry->set("class", str(zut_rtrim(trimmed_name)));
      if (!job.retcode.empty())
        entry->set("retcode", str(job.retcode));
      trimmed_name = job.correlator;
      if (!zut_rtrim(trimmed_name).empty())
        entry->set("correlator", str(trimmed_name));
      entry->set("phase", i64(job.phase));
      entry->set("phaseName", str(job.full_status));
      add_job_execution_data(job, entry);

      auto &row = table.row();
      row.add(job.jobid)
          .add(job.jobname)
          .add(job.owner)
          .add(job.status)
          .add(job.retcode);
      if (exec_data)
      {
        row.add(job.exec_system)
            .add(job.exec_member)
            .add(job.exec_submitted)
            .add(job.exec_started)
            .add(job.exec_ended);
      }
      row.emit(entry);
    }

    table.finish();
  }
  if (RTNCD_WARNING == rc)
  {
    if (warn)
    {
      context.error_stream() << "Warning: results truncated" << std::endl;
    }
  }
  if (RTNCD_SUCCESS != rc && RTNCD_WARNING != rc)
  {
    context.error_stream() << "Error: could not list jobs for: '" << owner_name << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  return (!warn && rc == RTNCD_WARNING) ? RTNCD_SUCCESS : rc;
}

int handle_job_list_files(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");
  long long max_entries = context.get<long long>("max-entries", 0);
  bool warn = context.get<bool>("warn", true);

  if (max_entries > 0)
  {
    zjb.dds_max = max_entries;
  }

  std::vector<ZJobDD> job_dds;
  rc = zjb_list_dds(&zjb, jobid, job_dds);
  if (RTNCD_SUCCESS == rc || RTNCD_WARNING == rc)
  {
    ResultTable table(context);
    table.add_column("ddname", 9)
        .add_column("dsname")
        .add_column("id", 4)
        .add_column("stepname")
        .add_column("procstep");

    for (const auto &dd : job_dds)
    {
      const auto entry = obj();
      std::string trimmed_name = dd.ddn;
      entry->set("ddname", str(zut_rtrim(trimmed_name)));
      trimmed_name = dd.dsn;
      entry->set("dsname", str(zut_rtrim(trimmed_name)));
      entry->set("id", i64(dd.key));
      trimmed_name = dd.stepname;
      entry->set("stepname", str(zut_rtrim(trimmed_name)));
      trimmed_name = dd.procstep;
      entry->set("procstep", str(zut_rtrim(trimmed_name)));

      table.row()
          .add(dd.ddn)
          .add(dd.dsn)
          .add(dd.key)
          .add(dd.stepname)
          .add(dd.procstep)
          .emit(entry);
    }

    table.finish();
  }

  if (RTNCD_WARNING == rc)
  {
    if (warn)
    {
      context.error_stream() << "Warning: " << zjb.diag.e_msg << std::endl;
    }
  }

  if (RTNCD_SUCCESS != rc && RTNCD_WARNING != rc)
  {
    context.error_stream() << "Error: could not list files for: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  return (!warn && rc == RTNCD_WARNING) ? RTNCD_SUCCESS : rc;
}

int handle_job_view_status(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  ZJob job{};
  std::string jobid = context.get<std::string>("jobid", "");

  rc = zjb_view(&zjb, jobid, job);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not view job status for: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  // A single row rather than a list: the structured result is the job object
  // built below, so no ast::Node is handed to emit() here.
  std::string trimmed_correlator = job.correlator;
  zut_rtrim(trimmed_correlator);

  ResultTable table(context);
  table.add_column("jobid")
      .add_column("jobname")
      .add_column("owner")
      .add_column("status", 7)
      .add_column("retcode", 10)
      .add_column("correlator", 33)
      .add_column("phaseName");
  table.row()
      .add(job.jobid)
      .add(job.jobname)
      .add(job.owner)
      .add(job.status)
      .add(job.retcode)
      .add(trimmed_correlator)
      .add(job.full_status)
      .emit();

  const auto result = obj();
  result->set("id", str(jobid));
  std::string trimmed_name = job.jobname;
  result->set("name", str(zut_rtrim(trimmed_name)));
  trimmed_name = job.subsystem;
  if (!zut_rtrim(trimmed_name).empty())
    result->set("subsystem", str(trimmed_name));
  trimmed_name = job.owner;
  result->set("owner", str(zut_rtrim(trimmed_name)));
  result->set("status", str(job.status));
  result->set("type", str(job.type));
  trimmed_name = job.jobclass;
  result->set("class", str(zut_rtrim(trimmed_name)));
  if (!job.retcode.empty())
    result->set("retcode", str(job.retcode));
  trimmed_name = job.correlator;
  if (!zut_rtrim(trimmed_name).empty())
    result->set("correlator", str(trimmed_name));
  result->set("phase", i64(job.phase));
  result->set("phaseName", str(job.full_status));
  add_job_execution_data(job, result);
  context.set_object(result);

  return 0;
}

int handle_job_view_file(InvocationContext &context)
{
  // Note: Middleware doesn't use this command - it lists jobs by ID instead of DSN
  int rc = 0;
  ZJB zjb{};
  std::string dsn = context.get<std::string>("dsn", "");
  transform(dsn.begin(), dsn.end(), dsn.begin(), ::toupper);

  if (context.has("encoding"))
  {
    zut_prepare_encoding(context.get<std::string>("encoding", ""), &zjb.encoding_opts);
  }
  if (context.has("local-encoding"))
  {
    const auto source_encoding = context.get<std::string>("local-encoding", "");
    if (!source_encoding.empty() && source_encoding.size() < sizeof(zjb.encoding_opts.source_codepage))
    {
      memcpy(zjb.encoding_opts.source_codepage, source_encoding.data(), source_encoding.length() + 1);
    }
  }

  std::string resp;
  rc = zjb_read_job_content_by_dsn(&zjb, dsn, resp);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not view job file for: '" << dsn << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  bool has_encoding = context.has("encoding");
  bool response_format_bytes = context.get<bool>("response-format-bytes", false);

  if (has_encoding && response_format_bytes)
  {
    zut_print_string_as_bytes(resp, &context.output_stream());
  }
  else
  {
    context.output_stream() << resp;
  }

  return RTNCD_SUCCESS;
}

int handle_job_view_file_by_id(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");
  long long key = context.get<long long>("key", 0);

  if (context.has("encoding"))
  {
    zut_prepare_encoding(context.get<std::string>("encoding", ""), &zjb.encoding_opts);
  }
  if (context.has("local-encoding"))
  {
    const auto source_encoding = context.get<std::string>("local-encoding", "");
    if (!source_encoding.empty() && source_encoding.size() < sizeof(zjb.encoding_opts.source_codepage))
    {
      memcpy(zjb.encoding_opts.source_codepage, source_encoding.data(), source_encoding.length() + 1);
    }
  }

  std::string resp;
  rc = zjb_read_job_content_by_key(&zjb, jobid, key, resp);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not view job file for: '" << jobid << "' with key '" << key << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  bool has_encoding = context.has("encoding");
  bool response_format_bytes = context.get<bool>("response-format-bytes", false);

  if (has_encoding && response_format_bytes)
  {
    zut_print_string_as_bytes(resp, &context.output_stream());
  }
  else
  {
    context.output_stream() << resp;
  }

  return RTNCD_SUCCESS;
}

int handle_job_view_jcl(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");

  std::string resp;
  rc = zjb_read_job_jcl(&zjb, jobid, resp);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not view job file for: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << resp;

  return 0;
}

int handle_job_submit(InvocationContext &context)
{
  int rc = 0;
  std::string dsn = context.get<std::string>("dsn", "");
  std::string jobid;

  ZDS zds{};
  ZDSReadOpts read_opts{.zds = &zds, .dsname = dsn};
  std::string contents;
  rc = zds_read(read_opts, contents);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not read data set: '" << dsn << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zds.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  return job_submit_common(context, contents, jobid, dsn);
}

int handle_job_submit_uss(InvocationContext &context)
{
  int rc = 0;
  std::string file = context.get<std::string>("file-path", "");

  ZUSF zusf{};
  std::string response;
  rc = zusf_read_from_uss_file(&zusf, file, response);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not view USS file: '" << file << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details:\n"
                           << zusf.diag.e_msg << std::endl
                           << response << std::endl;
    return RTNCD_FAILURE;
  }

  std::string jobid;

  return job_submit_common(context, response, jobid, file);
}

int handle_job_submit_jcl(InvocationContext &context)
{
  ZJB zjb{};
  std::string jobid;
  std::string data = zut_read_input(context.input_stream());

  ZEncode encoding_opts{};
  bool encoding_prepared = context.has("encoding") && zut_prepare_encoding(context.get<std::string>("encoding", ""), &encoding_opts);

  if (context.has("local-encoding"))
  {
    const auto source_encoding = context.get<std::string>("local-encoding", "");
    if (!source_encoding.empty() && source_encoding.size() < sizeof(encoding_opts.source_codepage))
    {
      memcpy(encoding_opts.source_codepage, source_encoding.data(), source_encoding.length() + 1);
    }
  }

  if (encoding_prepared && encoding_opts.data_type != eDataTypeBinary)
  {
    const auto source_encoding = std::strlen(encoding_opts.source_codepage) > 0 ? std::string(encoding_opts.source_codepage) : "UTF-8";
    data = zut_encode(data, source_encoding, std::string(encoding_opts.codepage), zjb.diag);
  }

  return job_submit_common(context, data, jobid, "JCL", true);
}

int handle_job_delete(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");

  rc = zjb_delete(&zjb, jobid);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not delete job: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "Job " << jobid << " deleted " << std::endl;

  return RTNCD_SUCCESS;
}

bool case_insensitive_match(const char a, const char b)
{
  return tolower(a) == tolower(b);
}

int handle_job_watch(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string job_dsn = context.get<std::string>("job-dsn", "");
  std::string until_match = context.get<std::string>("pattern", "");
  long long max_sleep_seconds = context.get<long long>("max-wait-seconds");
  bool any_case = context.get<bool>("ignore-case", false);

#define MAX_WAIT_SECONDS 60ll * 5ll

  if (max_sleep_seconds > MAX_WAIT_SECONDS)
  {
    context.error_stream() << "Error: max-wait-seconds must be less than " << MAX_WAIT_SECONDS << " seconds" << std::endl;
    return RTNCD_FAILURE;
  }

  if (max_sleep_seconds <= 0)
  {
    context.error_stream() << "Error: max-wait-seconds must be greater than 0 seconds" << std::endl;
    return RTNCD_FAILURE;
  }

  std::string pattern;
  bool is_regex = parse_regex_pattern(until_match, pattern);

  if (any_case && is_regex)
  {
    context.error_stream() << "Error: ignore-case is not supported for regex patterns" << std::endl;
    return RTNCD_FAILURE;
  }

  bool found_match = false;
  std::string matched;
  long long total_sleep_seconds = 0;

  do
  {
    std::string response;
    rc = zjb_read_job_content_by_dsn(&zjb, job_dsn, response);
    if (0 != rc)
    {
      context.error_stream() << "Error: could not read job content: '" << job_dsn << "' rc: '" << rc << "'" << std::endl;
      context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
      return RTNCD_FAILURE;
    }

    if (is_regex)
    {
      regex_t re;
      // Use REG_EXTENDED for extended regex syntax
      // Use REG_NEWLINE so ^ and $ match line boundaries (not just start/end of string)
      int ret = regcomp(&re, pattern.c_str(), REG_EXTENDED | REG_NEWLINE);
      if (ret == 0)
      {
        regmatch_t match[1];
        memset(match, 0, sizeof(match));

        int exec_ret = regexec(&re, response.c_str(), 1, match, 0);

        if (exec_ret == 0)
        {
          // On z/OS, rm_eo may not be set correctly by regexec
          // Use rm_so as the start position - it appears to be reliable
          if (match[0].rm_so >= 0)
          {
            size_t start = static_cast<size_t>(match[0].rm_so);

            // Find the line containing the match
            // Search backwards for start of line
            size_t line_start = start;
            while (line_start > 0 && response[line_start - 1] != '\n')
            {
              line_start--;
            }

            // Search forwards for end of line
            size_t line_end = start;
            while (line_end < response.length() && response[line_end] != '\n')
            {
              line_end++;
            }

            if (line_start <= response.length() && line_end <= response.length())
            {
              matched = response.substr(line_start, line_end - line_start);
              found_match = true;
            }
          }
        }
        else if (exec_ret == REG_NOMATCH)
        {
          // No match found, continue loop
        }
        else
        {
          char errbuf[256];
          regerror(exec_ret, &re, errbuf, sizeof(errbuf));
          context.error_stream() << "Debug: regexec error: " << errbuf << std::endl;
        }
        regfree(&re);
      }
      else
      {
        char errbuf[256];
        regerror(ret, &re, errbuf, sizeof(errbuf));
        context.error_stream() << "Error: invalid regex pattern: '" << pattern << "' - " << errbuf << std::endl;
      }
    }
    else
    {
      if (any_case)
      {
        auto it = search(response.begin(), response.end(), pattern.begin(), pattern.end(), case_insensitive_match);
        if (it != response.end())
        {
          matched = response.substr(it - response.begin(), pattern.length());
          found_match = true;
        }
      }
      else
      {
        if (response.find(pattern) != std::string::npos)
        {
          matched = pattern;
          found_match = true;
        }
      }
    }

    if (found_match)
    {
      break;
    }
    total_sleep_seconds++;
    sleep(1);
  } while (total_sleep_seconds < max_sleep_seconds);

  if (found_match)
  {
    context.output_stream() << (is_regex ? "'Regex'" : "'string'") << " pattern '" << pattern << "' in job spool files matched in " << total_sleep_seconds << "s on:"
                            << std::endl;
    context.output_stream() << matched << std::endl;
    return RTNCD_SUCCESS;
  }
  else
  {
    context.error_stream() << "Error: " << (is_regex ? "'Regex'" : "'string'") << " pattern '" << pattern << "' in job spool files was not found in " << total_sleep_seconds << "s" << std::endl;
    return RTNCD_FAILURE;
  }

  return found_match ? RTNCD_SUCCESS : RTNCD_FAILURE;
}

int handle_job_cancel(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");

  // Note: Cancel options (dump, force, purge, restart) are currently not used by the backend
  // but are defined for future compatibility
  // bool option_dump = context.get<bool>("dump", false);
  // bool option_force = context.get<bool>("force", false);
  // bool option_purge = context.get<bool>("purge", false);
  // bool option_restart = context.get<bool>("restart", false);

  rc = zjb_cancel(&zjb, jobid);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not cancel job: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "Job " << jobid << " cancelled " << std::endl;

  return RTNCD_SUCCESS;
}

int handle_job_hold(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");

  rc = zjb_hold(&zjb, jobid);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not hold job: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "Job " << jobid << " held " << std::endl;

  return RTNCD_SUCCESS;
}

int handle_job_release(InvocationContext &context)
{
  int rc = 0;
  ZJB zjb{};
  std::string jobid = context.get<std::string>("jobid", "");

  rc = zjb_release(&zjb, jobid);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not release job: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  context.output_stream() << "Job " << jobid << " released " << std::endl;

  return RTNCD_SUCCESS;
}

int job_submit_common(InvocationContext &context, const std::string &jcl, std::string &jobid, const std::string &identifier, bool strip_crlf)
{
  int rc = 0;
  ZJB zjb{};
  const char CR_CHAR = '\x0D';
  std::string new_contents;

  if (strip_crlf)
  {
    std::stringstream ss(jcl);
    std::string line;
    while (std::getline(ss, line))
    {
      if (!line.empty() && line.back() == CR_CHAR)
      {
        line.pop_back();
      }
      line.resize(80, ' ');
      new_contents += line;
    }
  }
  else
  {
    new_contents = jcl;
  }

  rc = zjb_submit(&zjb, new_contents, jobid);

  if (0 != rc)
  {
    context.error_stream() << "Error: could not submit JCL: '" << identifier << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  ZJob job{};
  rc = zjb_view(&zjb, std::string(zjb.correlator, sizeof(zjb.correlator)), job);
  if (0 != rc)
  {
    context.error_stream() << "Error: could not get job status for: '" << jobid << "' rc: '" << rc << "'" << std::endl;
    context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
    return RTNCD_FAILURE;
  }

  bool only_jobid = context.get<bool>("only-jobid", false);
  bool only_correlator = context.get<bool>("only-correlator", false);
  std::string wait = context.get<std::string>("wait", "");
  std::transform(wait.begin(), wait.end(), wait.begin(), ::toupper);

  if (only_jobid)
    context.output_stream() << jobid << std::endl;
  else if (only_correlator)
    context.output_stream() << std::string(zjb.correlator, sizeof(zjb.correlator)) << std::endl;
  else
    context.output_stream() << "Submitted " << identifier << ", " << job.jobname << "(" << jobid << ")" << std::endl;

#define JOB_STATUS_OUTPUT "OUTPUT"
#define JOB_STATUS_INPUT "ACTIVE"

  if (JOB_STATUS_OUTPUT == wait || JOB_STATUS_INPUT == wait)
  {
    rc = zjb_wait(&zjb, wait);
    if (0 != rc)
    {
      context.error_stream() << "Error: could not wait for job status: '" << wait << "' rc: '" << rc << "'" << std::endl;
      context.error_stream() << "  Details: " << zjb.diag.e_msg << std::endl;
      return RTNCD_FAILURE;
    }
  }
  else if ("" != wait)
  {
    context.error_stream() << "Error: cannot wait for unknown status '" << wait << "'" << std::endl;
    return RTNCD_FAILURE;
  }

  const auto result = obj();
  result->set("jobId", str(jobid));
  result->set("jobName", str(job.jobname));
  context.set_object(result);

  return rc;
}

void register_commands(parser::Command &root_command)
{
  auto encoding_option = make_aliases("--encoding", "--ec");
  auto source_encoding_option = make_aliases("--local-encoding", "--lec");
  auto response_format_csv_option = make_aliases("--response-format-csv", "--rfc");
  auto response_format_bytes_option = make_aliases("--response-format-bytes", "--rfb");

  // Job command group
  auto job_group = command_ptr(new Command("job", "z/OS job operations"));

  // List subcommand
  auto job_list_cmd = command_ptr(new Command("list", "list jobs"));
  job_list_cmd->add_alias("ls");
  job_list_cmd->add_keyword_arg("owner", make_aliases("--owner", "-o"), "filter by owner", ArgType_Single, false);
  job_list_cmd->add_keyword_arg("prefix", make_aliases("--prefix", "-p"), "filter by prefix", ArgType_Single, false);
  job_list_cmd->add_keyword_arg("status", make_aliases("--status", "-s"), "filter by status", ArgType_Single, false);
  job_list_cmd->add_keyword_arg("exec-data", make_aliases("--exec-data", "--ed"), "include job execution data", ArgType_Flag, false, ArgValue(false));
  job_list_cmd->add_keyword_arg(MAX_ENTRIES);
  job_list_cmd->add_keyword_arg(WARN);
  job_list_cmd->add_keyword_arg(RESPONSE_FORMAT_CSV);
  job_list_cmd->add_keyword_arg(RESPONSE_FORMAT_HEADER);
  job_list_cmd->set_handler(handle_job_list);
  job_group->add_command(job_list_cmd);

  // List-files subcommand
  auto job_list_files_cmd = command_ptr(new Command("list-files", "list spool files for jobid"));
  job_list_files_cmd->add_alias("lf");
  job_list_files_cmd->add_positional_arg(JOB_ID);
  job_list_files_cmd->add_keyword_arg(MAX_ENTRIES);
  job_list_files_cmd->add_keyword_arg(WARN);
  job_list_files_cmd->add_keyword_arg(RESPONSE_FORMAT_CSV);
  job_list_files_cmd->add_keyword_arg(RESPONSE_FORMAT_HEADER);
  job_list_files_cmd->set_handler(handle_job_list_files);
  job_group->add_command(job_list_files_cmd);

  // View-status subcommand
  auto job_view_status_cmd = command_ptr(new Command("view-status", "view job status"));
  job_view_status_cmd->add_alias("vs");
  job_view_status_cmd->add_positional_arg(JOB_ID);
  job_view_status_cmd->add_keyword_arg(RESPONSE_FORMAT_CSV);
  job_view_status_cmd->add_keyword_arg(RESPONSE_FORMAT_HEADER);
  job_view_status_cmd->set_handler(handle_job_view_status);
  job_group->add_command(job_view_status_cmd);

  // View-file subcommand
  auto job_view_file_cmd = command_ptr(new Command("view-file", "view job file output"));
  job_view_file_cmd->mark_stdout_as_payload();
  job_view_file_cmd->add_alias("vf");
  job_view_file_cmd->add_positional_arg("dsn", "job dsn via 'job list-files'", ArgType_Single, true);
  job_view_file_cmd->add_keyword_arg(ENCODING);
  job_view_file_cmd->add_keyword_arg(LOCAL_ENCODING);
  job_view_file_cmd->add_keyword_arg(RESPONSE_FORMAT_BYTES);
  job_view_file_cmd->set_handler(handle_job_view_file);
  job_group->add_command(job_view_file_cmd);

  // View-file-by-id subcommand
  auto job_view_file_by_id_cmd = command_ptr(new Command("view-file-by-id", "view job file output by id"));
  job_view_file_by_id_cmd->mark_stdout_as_payload();
  job_view_file_by_id_cmd->add_alias("vfbi");
  job_view_file_by_id_cmd->add_positional_arg(JOB_ID);
  job_view_file_by_id_cmd->add_positional_arg("key", "valid job dsn key via 'job list-files'", ArgType_Single, true);
  job_view_file_by_id_cmd->add_keyword_arg(ENCODING);
  job_view_file_by_id_cmd->add_keyword_arg(LOCAL_ENCODING);
  job_view_file_by_id_cmd->add_keyword_arg(RESPONSE_FORMAT_BYTES);
  job_view_file_by_id_cmd->set_handler(handle_job_view_file_by_id);
  job_group->add_command(job_view_file_by_id_cmd);

  // View-jcl subcommand
  auto job_view_jcl_cmd = command_ptr(new Command("view-jcl", "view job jcl from input jobid"));
  job_view_jcl_cmd->mark_stdout_as_payload();
  job_view_jcl_cmd->add_alias("vj");
  job_view_jcl_cmd->add_positional_arg(JOB_ID);
  job_view_jcl_cmd->set_handler(handle_job_view_jcl);
  job_group->add_command(job_view_jcl_cmd);

  // Submit subcommand
  auto job_submit_cmd = command_ptr(new Command("submit", "submit a job"));
  job_submit_cmd->add_alias("sub");
  job_submit_cmd->add_positional_arg(DSN);
  job_submit_cmd->add_keyword_arg("wait", make_aliases("--wait"), "wait for job status", ArgType_Single, false);
  job_submit_cmd->add_keyword_arg("only-jobid", make_aliases("--only-jobid", "--oj"), "show only job id on success", ArgType_Flag, false, ArgValue(false));
  job_submit_cmd->add_keyword_arg("only-correlator", make_aliases("--only-correlator", "--oc"), "show only job correlator on success", ArgType_Flag, false, ArgValue(false));
  job_submit_cmd->set_handler(handle_job_submit);
  job_group->add_command(job_submit_cmd);

  // Submit-jcl subcommand
  auto job_submit_jcl_cmd = command_ptr(new Command("submit-jcl", "submit JCL contents directly"));
  job_submit_jcl_cmd->add_alias("subj");
  job_submit_jcl_cmd->add_keyword_arg("wait", make_aliases("--wait"), "wait for job status", ArgType_Single, false);
  job_submit_jcl_cmd->add_keyword_arg("only-jobid", make_aliases("--only-jobid", "--oj"), "show only job id on success", ArgType_Flag, false, ArgValue(false));
  job_submit_jcl_cmd->add_keyword_arg("only-correlator", make_aliases("--only-correlator", "--oc"), "show only job correlator on success", ArgType_Flag, false, ArgValue(false));
  job_submit_jcl_cmd->add_keyword_arg(ENCODING_INPUT);
  job_submit_jcl_cmd->add_keyword_arg(LOCAL_ENCODING);
  job_submit_jcl_cmd->set_handler(handle_job_submit_jcl);
  job_group->add_command(job_submit_jcl_cmd);

  // Submit-uss subcommand
  auto job_submit_uss_cmd = command_ptr(new Command("submit-uss", "submit a job from USS files"));
  job_submit_uss_cmd->add_alias("sub-u");
  job_submit_uss_cmd->add_positional_arg(FILE_PATH);
  job_submit_uss_cmd->add_keyword_arg("wait", make_aliases("--wait"), "wait for job status", ArgType_Single, false);
  job_submit_uss_cmd->add_keyword_arg("only-jobid", make_aliases("--only-jobid", "--oj"), "show only job id on success", ArgType_Flag, false, ArgValue(false));
  job_submit_uss_cmd->add_keyword_arg("only-correlator", make_aliases("--only-correlator", "--oc"), "show only job correlator on success", ArgType_Flag, false, ArgValue(false));
  job_submit_uss_cmd->set_handler(handle_job_submit_uss);
  job_group->add_command(job_submit_uss_cmd);

  // Delete subcommand
  auto job_delete_cmd = command_ptr(new Command("delete", "delete a job"));
  job_delete_cmd->add_alias("del");
  job_delete_cmd->add_positional_arg(JOB_ID);
  job_delete_cmd->set_handler(handle_job_delete);
  job_group->add_command(job_delete_cmd);

  // Watch subcommand
  auto job_watch_cmd = command_ptr(new Command("watch", "watch job spool files for a given string pattern"));
  job_watch_cmd->add_alias("wch");
  job_watch_cmd->add_positional_arg("job-dsn", "job dsn to watch (from 'job list-files')", ArgType_Single, true);
  job_watch_cmd->add_keyword_arg("pattern", make_aliases("--pattern", "-p"), "string pattern to watch for in spool files", ArgType_Single, true);
  job_watch_cmd->add_keyword_arg("max-wait-seconds", make_aliases("--max-wait-seconds", "--mws"), "maximum number of seconds to wait for the pattern to match (max 300 seconds)", ArgType_Single, false, ArgValue(15ll));
  job_watch_cmd->add_keyword_arg("ignore-case", make_aliases("--ignore-case", "--ic"), "match string in any case", ArgType_Flag, false, ArgValue(false));
  job_watch_cmd->set_handler(handle_job_watch);
  job_watch_cmd->add_example("Watch job spool files for a given string pattern", "zo job watch IBMUSER.IEFBR14@.JOB01684.D0000002.JESMSGLG --pattern \"$HASP395 IEFBR14@ ENDED\"");
  job_watch_cmd->add_example("Watch job spool files for a given regex pattern", "zo job watch IBMUSER.IEFBR14@.JOB01684D0000002.JESMSGLG --pattern \"/^.*ENDED.*$/g\"");
  job_group->add_command(job_watch_cmd);

  // Cancel subcommand
  auto job_cancel_cmd = command_ptr(new Command("cancel", "cancel a job"));
  job_cancel_cmd->add_alias("cnl");
  job_cancel_cmd->add_positional_arg(JOB_ID);
  job_cancel_cmd->add_keyword_arg("dump", make_aliases("--dump", "-d"), "Dump the cancelled jobs if waiting for conversion, in conversion, or in execution", ArgType_Flag, false, ArgValue(false));
  job_cancel_cmd->add_keyword_arg("force", make_aliases("--force", "-f"), "Force cancel the jobs, even if marked", ArgType_Flag, false, ArgValue(false));
  job_cancel_cmd->add_keyword_arg("purge", make_aliases("--purge", "-p"), "Purge output of the cancelled jobs", ArgType_Flag, false, ArgValue(false));
  job_cancel_cmd->add_keyword_arg("restart", make_aliases("--restart", "-r"), "Request that automatic restart management automatically restart the selected jobs after they are cancelled", ArgType_Flag, false, ArgValue(false));
  job_cancel_cmd->set_handler(handle_job_cancel);
  job_group->add_command(job_cancel_cmd);

  // Hold subcommand
  auto job_hold_cmd = command_ptr(new Command("hold", "hold a job"));
  job_hold_cmd->add_alias("hld");
  job_hold_cmd->add_positional_arg(JOB_ID);
  job_hold_cmd->set_handler(handle_job_hold);
  job_group->add_command(job_hold_cmd);

  // Release subcommand
  auto job_release_cmd = command_ptr(new Command("release", "release a job"));
  job_release_cmd->add_alias("rel");
  job_release_cmd->add_positional_arg(JOB_ID);
  job_release_cmd->set_handler(handle_job_release);
  job_group->add_command(job_release_cmd);

  root_command.add_command(job_group);
}
} // namespace job
