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

#include "zjb_py.hpp"
#include <iostream>

using namespace std;

static const char *ZJB_ANY_PREFIX = "*";

void convert_job_to_ascii(ZJob &job)
{
  e2a_inplace(job.jobname);
  e2a_inplace(job.jobid);
  e2a_inplace(job.owner);
  e2a_inplace(job.status);
  e2a_inplace(job.full_status);
  e2a_inplace(job.retcode);
  e2a_inplace(job.correlator);
}

void convert_jobs_to_ascii(vector<ZJob> &jobs)
{
  for (auto &job : jobs)
  {
    convert_job_to_ascii(job);
  }
}

void handle_zjb_error(const ZJB &zjb, int rc)
{
  if (rc != 0)
  {
    std::string diag(zjb.diag.e_msg, zjb.diag.e_msg_len);
    diag.push_back('\0');
    e2a_inplace(diag);
    diag.pop_back();
    throw std::runtime_error(diag);
  }
}

vector<ZJob> list_jobs_by_owner(string owner_name)
{
  return list_jobs_by_owner(owner_name, ZJB_ANY_PREFIX, "");
}

vector<ZJob> list_jobs_by_owner(string owner_name, string prefix, string status)
{
  vector<ZJob> jobs;
  ZJB zjb = {0};

  // A blank prefix reaches ZJBMLIST as a job name filter of blanks rather than "no filter",
  // which the extended status service rejects, so send the wildcard zo defaults to.
  if (prefix.empty())
  {
    prefix = ZJB_ANY_PREFIX;
  }

  a2e_inplace(owner_name);
  a2e_inplace(prefix);
  a2e_inplace(status);
  int rc = zjb_list_by_owner(&zjb, owner_name, prefix, status, jobs);

  handle_zjb_error(zjb, rc);
  convert_jobs_to_ascii(jobs);

  return jobs;
}

ZJob get_job_status(string jobid)
{
  ZJob job = {}; // ZJob holds std::string members; `= {0}` would build one from a null pointer
  ZJB zjb = {0};

  a2e_inplace(jobid);
  int rc = zjb_view(&zjb, jobid, job);

  handle_zjb_error(zjb, rc);
  convert_job_to_ascii(job);

  return job;
}

vector<ZJobDD> list_spool_files(string jobid)
{
  vector<ZJobDD> jobDDs;
  ZJB zjb = {0};

  a2e_inplace(jobid);
  int rc = zjb_list_dds(&zjb, jobid, jobDDs);

  handle_zjb_error(zjb, rc);

  for (auto &dd : jobDDs)
  {
    e2a_inplace(dd.jobid);
    e2a_inplace(dd.ddn);
    e2a_inplace(dd.dsn);
    e2a_inplace(dd.stepname);
    e2a_inplace(dd.procstep);
  }

  return jobDDs;
}

string read_spool_file(string jobid, int key)
{
  string response;
  ZJB zjb = {0};

  a2e_inplace(jobid);
  int rc = zjb_read_job_content_by_key(&zjb, jobid, key, response);

  handle_zjb_error(zjb, rc);
  e2a_inplace(response);

  return response;
}

string get_job_jcl(string jobid)
{
  string response;
  ZJB zjb = {0};

  a2e_inplace(jobid);
  int rc = zjb_read_job_jcl(&zjb, jobid, response);

  handle_zjb_error(zjb, rc);
  e2a_inplace(response);

  return response;
}

string submit_job(string jcl_content)
{
  string jobid;
  ZJB zjb = {0};

  a2e_inplace(jcl_content);
  int rc = zjb_submit(&zjb, jcl_content, jobid);

  handle_zjb_error(zjb, rc);
  e2a_inplace(jobid);
  return jobid;
}

bool delete_job(string jobid)
{
  ZJB zjb = {0};

  a2e_inplace(jobid);
  int rc = zjb_delete(&zjb, jobid);

  handle_zjb_error(zjb, rc);

  return true;
}