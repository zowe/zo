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

#ifndef ZJB_HPP
#define ZJB_HPP

#include <iostream>
#include <vector>
#include <string>
#include "zjbtype.h"

struct ZJob
{
  std::string jobname;
  std::string jobid;
  std::string owner;
  std::string status;
  std::string full_status;
  std::string retcode;
  std::string correlator;
  std::string subsystem;
  std::string type;
  std::string jobclass;
  int phase;
  std::string exec_system;
  std::string exec_member;
  std::string exec_submitted;
  std::string exec_started;
  std::string exec_ended;
};

struct ZJobDD
{
  std::string jobid;
  std::string ddn;
  std::string dsn;
  std::string stepname;
  std::string procstep;
  unsigned char token[80];
  int key;
  bool is_asa = false;
};

// The convenience overloads are hidden from SWIG: C linkage cannot express an overload set, and
// every symbol crossing the bindings boundary needs it. See the extern "C" block below.
#ifndef SWIG
/**
 * @brief Return a list of jobs from an input or default owner
 *
 * @param zjb job returned attributes and error information
 * @param owner_name owner name of the job to query, defaults to current user if == "", may use wild cards, i.e.
 * "IBMUS*"
 * @param jobs populated list returned containing job information array
 * @return int 0 for success; non zero otherwise
 */
int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, std::vector<ZJob> &jobs);

/**
 * @brief Return a list of jobs from an input or default owner
 *
 * @param zjb job returned attributes and error information
 * @param owner_name owner name of the job to query, defaults to current user if == "", may use wild cards, i.e.
 * "IBMUS*"
 * @param prefix_name job prefix, defaults to "*" if == "", may use wild cards, i.e. "IBMUS*"
 * @param jobs populated list returned containing job information array
 * @return int 0 for success; non zero otherwise
 */
int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, const std::string &prefix_name, std::vector<ZJob> &jobs);
#endif

// The bindings compile this header EBCDIC and their SWIG wrappers ASCII. libc++ uses a distinct
// inline namespace per char mode (std::__1 vs std::__1_a), so a mangled name is unresolvable
// across that boundary -- everything the bindings call needs C linkage.
#ifdef SWIG
extern "C"
{
#endif
/**
 * @brief Return a list of jobs from an input or default owner
 *
 * @param zjb job returned attributes and error information
 * @param owner_name owner name of the job to query, defaults to current user if == "", may use wild cards, i.e.
 * "IBMUS*"
 * @param prefix_name job prefix, defaults to "*" if == "", may use wild cards, i.e. "IBMUS*"
 * @param status_name job status, defaults to "*" if == "", supports "ACTIVE" only
 * @param jobs populated list returned containing job information array
 * @return int 0 for success; non zero otherwise
 */
int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, const std::string &prefix_name, const std::string &status_name, std::vector<ZJob> &jobs);

/**
 * @brief Return a list of proclib for a job
 *
 * @param zjb job returned attributes and error information
 * @param proclib populated list returned containing proclib names
 * @return int 0 for success; non zero otherwise
 */
int zjb_list_proclib(ZJB *zjb, std::vector<std::string> &proclib);

/**
 * @brief Return a job status struct from input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator used to search
 * @param job populated struct returned for found job
 * @return int 0 for success; non zero otherwise
 */
int zjb_view(ZJB *zjb, const std::string &jobid, ZJob &job);

/**
 * @brief Return a list of job file information from an input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator used to search
 * @param job_dds populated list returned containing job file information array
 * @return int 0 for success; non zero otherwise
 */
int zjb_list_dds(ZJB *zjb, const std::string &jobid, std::vector<ZJobDD> &job_dds);

/**
 * @brief Return output from a specific job file
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid coresponding to the key to locate
 * @param key data set key returned from zjb_list_dds, e.g. JESMSGLG is usually 2
 * @param response return job file output
 * @return int 0 for success; non zero otherwise
 */
int zjb_read_job_content_by_key(ZJB *zjb, const std::string &jobid, int key, std::string &response);

int zjb_get_job_dsn_by_key(ZJB *zjb, const std::string &, int, std::string &);

/**
 * @brief Read the content of a job by input job data set name and timestamp or date
 *
 * @param zjb job returned attributes and error information
 * @param job_dsn job data set name to read
 * @param timestamp timestamp to read, e.g. 10:41:00
 * @param date date to read, e.g. 2026-03-13
 * @param response return job content
 * @return int 0 for success; non zero otherwise
 */
int zjb_read_job_content_by_dsn(ZJB *zjb, const std::string &job_dsn, std::string &response);

struct ZJBSyslogOptions
{
  // Inputs
  std::string date;
  std::string time;
  int max_lines = 0;
};

struct ZJBSyslogResponse
{
  std::string data;
  bool has_more;
  std::string end_date;
  std::string end_time;
  int returned_lines;
};

int zjb_read_syslog(ZJB *zjb, ZJBSyslogOptions &opts, ZJBSyslogResponse &response);

/**
 * @brief Wait for a job to reach a specific status
 *
 * @param zjb job returned attributes and error information
 * @param status job status to wait for
 * @return int 0 for success; non zero otherwise
 */
int zjb_wait(ZJB *zjb, const std::string &status);

/**
 * @brief Return JCL for a job by input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator coresponding to the key to locate
 * @param response return JCL
 * @return int 0 for success; non zero otherwise
 */
int zjb_read_job_jcl(ZJB *zjb, const std::string &jobid, std::string &response);

/**
 * @brief Submit a job with the given JCL
 *
 * @param zjb job returned attributes and error information
 * @param contents The JCL contents to submit
 * @param jobid jobid returned after successfully submitting JCL
 * @return int 0 for success; non zero otherwise
 */
int zjb_submit(ZJB *zjb, const std::string &contents, std::string &jobId);

/**
 * @brief Submit a job from a given input data set
 *
 * @param zjb job returned attributes and error information
 * @param dsn The DSN containing JCL contents to submit
 * @param jobid jobid returned after successfully submitting JCL
 * @return int 0 for success; non zero otherwise
 */
int zjb_submit_dsn(ZJB *zjb, const std::string &dsn, std::string &jobId);

/**
 * @brief Delete a job using input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid to delete, i.e. JOB00123 or J123
 * @return int 0 for success; non zero otherwise
 */
int zjb_delete(ZJB *zjb, const std::string &jobid);
#ifdef SWIG
}
#endif

/**
 * @brief Cancel a job using input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator used to cancel, i.e. JOB00123 or J123
 * @return int 0 for success; non zero otherwise
 */
int zjb_cancel(ZJB *zjb, const std::string &jobid);

/**
 * @brief Hold a job using input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator used to hold, i.e. JOB00123 or J123
 * @return int 0 for success; non zero otherwise
 */
int zjb_hold(ZJB *zjb, const std::string &jobid);

/**
 * @brief Release a job using input jobid
 *
 * @param zjb job returned attributes and error information
 * @param jobid jobid or job correlator used to release, i.e. JOB00123 or J123
 * @return int 0 for success; non zero otherwise
 */
int zjb_release(ZJB *zjb, const std::string &jobid);

#endif
