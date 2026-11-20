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
#include "zo.job.list.test.hpp"
#include "zo.job.submit.test.hpp"
#include "zo.job.manage.test.hpp"

using namespace ztst;

void zo_job_tests()
{
  static std::vector<std::string> _jobs;
  static std::vector<std::string> _ds;
  static std::vector<std::string> _files;

  // Helper to delete tracked jobs
  auto _cleanup_jobs = [&]() -> void
  {
    TestLog("Cleaning up " + std::to_string(_jobs.size()) + " jobs...");
    for (const auto &jobid : _jobs)
    {
      std::string response;
      execute_command_with_output(zo_command + " job delete \"" + jobid + "\"", response);
    }
    _jobs.clear();
  };

  // Helper to delete tracked datasets
  auto _cleanup_ds = [&]() -> void
  {
    TestLog("Cleaning up " + std::to_string(_ds.size()) + " data sets...");
    for (const auto &ds : _ds)
    {
      std::string response;
      execute_command_with_output(zo_command + " data-set delete " + ds, response);
    }
    _ds.clear();
  };

  // Helper to delete tracked files
  auto _cleanup_files = [&]() -> void
  {
    for (const auto &file : _files)
    {
      remove(file.c_str());
    }
    _files.clear();
  };

  describe("job", [&]() -> void
  {
    TEST_OPTIONS long_test_opts = {false, 60};

    afterAll([&]() -> void
    {
      _cleanup_jobs();
      _cleanup_ds();
      _cleanup_files();
    }, long_test_opts);

    zo_job_list_tests(_jobs, _ds, _files);
    zo_job_submit_tests(_jobs, _ds, _files);
    zo_job_manage_tests(_jobs, _ds, _files);
  });
}
