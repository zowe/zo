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

#pragma runopts("TRAP(ON,NOSPIE)")

#include <iostream>
#include "commands/console.hpp"
#include "commands/core.hpp"

// Version information
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

/**
 * zoa is the minimal APF-authorized alternative to zo.
 */
int main(int argc, char *argv[])
{
  try
  {
    auto &root_cmd = core::setup_root_command(argv, false);
    core::set_version(PACKAGE_VERSION);
    core::set_program_name("zoa");
    // zoa runs APF-authorized and its console commands stay privileged, so the authorization-drop hook never fires for them.
    // Keep the HWTJ-backed JSON serializer out of an authorized job step entirely.
    core::disable_json_output();

    console::register_commands(root_cmd);

    return core::execute_command(argc, argv);
  }
  catch (const std::exception &e)
  {
    std::cerr << "Fatal error encountered in zoa: " << e.what() << std::endl;
    return 1;
  }
}
