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

#ifndef ZJB_PY_HPP
#define ZJB_PY_HPP

#include "../../c/zjb.hpp"
#include "../../c/ztype.h"
#include <string>
#include <vector>
#include "conversion.hpp"

// Both forms funnel into zjb_list_by_owner's owner/prefix/status form, the only one zjb.hpp
// declares under -DSWIG -- C linkage cannot express an overload set.
std::vector<ZJob> list_jobs_by_owner(std::string owner_name);
std::vector<ZJob> list_jobs_by_owner(std::string owner_name, std::string prefix, std::string status);

ZJob get_job_status(std::string jobid);

std::vector<ZJobDD> list_spool_files(std::string jobid);

std::string read_spool_file(std::string jobid, int key);

std::string get_job_jcl(std::string jobid);

std::string submit_job(std::string jcl_content);

bool delete_job(std::string jobid);

#endif