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

#include "zstorage.test.hpp"
#include "zut.test.hpp"
#include "zjb.test.hpp"
#include "zds.test.hpp"
#include "zkr.test.hpp"
#include "zcn.test.hpp"
#include "zrecovery.test.hpp"
#include "zmetal.test.hpp"
#include "zusf.test.hpp"
#include "zbase64.test.hpp"
#include "zo.test.hpp"
#include "zo.uss.test.hpp"
#include "zlogger.test.hpp"
#include "zwto.test.hpp"
#include "parser.test.hpp"
#include "result_table.test.hpp"
#include "zstd.test.hpp"
#include "zjson.test.hpp"
#include "zo.server.test.hpp"
#include "zo.uss.server.test.hpp"
#include "zo.ds.server.test.hpp"
#include "zo.cert.server.test.hpp"
#include "zo.job.server.test.hpp"
#include "server/worker.test.hpp"
#include "server/validator.test.hpp"
#include "server_logger.test.hpp"
#include "plugin.test.hpp"
#include "ztest.hpp"
#include "../zlogger.hpp"
#include <cstdlib>
#include <cstring>

using namespace ztst;

#pragma runopts("TRAP(ON,NOSPIE)")
int main(int argc, char *argv[])
{
  if (argc == 2 && std::strcmp(argv[1], "--zlogger-fatal") == 0)
  {
    ZLogger::get_instance().fatal("literal %s %n 100%", 7);
    return 1;
  }

  // Keep test log output confined to the test working directory rather than
  // the real user's home directory (the default when ZO_LOGS_DIR is unset)
  setenv("ZO_LOGS_DIR", "logs", 1);

  int rc = tests(
      argc, argv,
      []() -> void
      {
        zo_uss_tests();
        zut_tests();
        zjb_tests();
        zds_tests();
        zkr_tests();
        zcn_tests();
        zstorage_tests();
        zrecovery_tests();
        zmetal_tests();
        zusf_tests();
        zbase64_tests();
        zo_tests();
        zlogger_tests();
        zwto_tests();
        parser_tests();
        result_table_tests();
        zstd_tests();
        zjson_tests();
        zo_server_tests();
        zo_uss_server_tests();
        zo_ds_server_tests();
        zo_cert_server_tests();
        zo_job_server_tests();
        server_worker_tests();
        server_validator_tests();
        server_logger_tests();
        plugin_tests();
      });

  return rc;
}
