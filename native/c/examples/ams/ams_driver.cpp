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

#include <iostream>
#include <string>
#include <vector>
#include "ztype.h"
#include <unistd.h>
#include "zds.hpp"

// TODO(Kelosky): test on archived data set
int main()
{
  int rc = 0;
  unsigned int code = 0;

  std::vector<std::string> data;
  data.push_back("hello world                                                                     ");
  // data.push_back("012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789abcdefghijkl");
  data.push_back("          0123456789012345678901234567890123456789012345678901234567890123456789");
  // data.push_back("0123456789          012345678901234567890123456789012345678901234567890123456789");
  // data.push_back(" hey workld");
  // data.push_back(" one more");
  // data.push_back("hello world                                                                     ");
  // data.push_back("012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789abcdefghijkl");
  // data.push_back("          0123456789012345678901234567890123456789012345678901234567890123456789");
  // data.push_back("0123456789          012345678901234567890123456789012345678901234567890123456789");
  // data.push_back(" hey workld");
  // data.push_back(" one more");
  // data.push_back("hello world                                                                     ");
  // data.push_back("012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789abcdefghijkl");
  // data.push_back("          0123456789012345678901234567890123456789012345678901234567890123456789");
  // data.push_back("0123456789          012345678901234567890123456789012345678901234567890123456789");
  // data.push_back(" hey workld");
  // data.push_back(" one more");
  // data.push_back("hello world                                                                     ");
  // data.push_back("012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789abcdefghijkl");
  // data.push_back("          0123456789012345678901234567890123456789012345678901234567890123456789");
  // data.push_back("0123456789          012345678901234567890123456789012345678901234567890123456789");
  data.push_back(" hey world");
  data.push_back(" one more");

  ZDS zds = {0};
  IO_CTRL *ioc = nullptr;
  rc = zds_open_output_bpam(&zds, "ZOWETEST.IO.O.FB80(data)", ioc);
  // rc = zds_open_output_bpam(&zds, "DKELOSKY.IO.O.V25(OMGNESS)", ioc);
  // rc = zds_open_output_bpam(&zds, "DKELOSKY.IO.O.V256(OMGNESS)", ioc);
  // rc = zds_open_output_bpam(&zds, "DKELOSKY.IO.O.VB256(OMGNESS)", ioc);
  if (0 != rc)
  {
    std::cout << "zds_open_output_bpam failed: " << rc << std::endl;
    std::cout << "  Details: " << zds.diag.e_msg << std::endl;

    return -1;
  }

  for (std::string &line : data)
  {
    rc = zds_write_output_bpam(&zds, ioc, line);
    if (0 != rc)
    {
      std::cout << "zds_write_output_bpam failed: " << rc << std::endl;
      std::cout << "  Details: " << zds.diag.e_msg << std::endl;
      return -1;
    }
  }

  rc = zds_close_output_bpam(&zds, ioc);
  if (0 != rc && RTNCD_WARNING != rc) // ignore warnings
  {
    std::cout << "zds_close_output_bpam failed: " << rc << std::endl;
    std::cout << "  Details: " << zds.diag.e_msg << std::endl;
    return -1;
  }

  std::cout << "zds_close_output_bpam success" << std::endl;
  std::cout << "  Data set: ZOWETEST.IO.O.FB80(data)" << std::endl;
  // Run command to view data set
  std::string command = "../../build-out/zo ds view 'ZOWETEST.IO.O.FB80(data)'";
  std::cout << "  Run: " << command << std::endl;
  rc = system(command.c_str());
  if (0 != rc)
  {
    std::cout << "Failed to run command: " << command << std::endl;
    return -1;
  }
  return 0;
}
