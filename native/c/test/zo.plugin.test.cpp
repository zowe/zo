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

#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "ztest.hpp"
#include "zutils.hpp"
#include "zo.plugin.test.hpp"

using namespace ztst;

namespace
{
const std::string LEGACY_FALLBACK_DIR = "../build-out/plugins";
const std::string EXPLICIT_OPT_IN_DIR = "zowex_plugin_opt_in_test";

const std::string UNREGISTERED_BANNER = "The following unregistered plug-ins were found in the plugins/ dir:";

void write_regular_file(const std::string &path, const std::string &contents)
{
  std::ofstream file(path, std::ios::binary);
  file << contents;
}

void remove_dir_with_file(const std::string &dir, const std::string &filename)
{
  unlink((dir + "/" + filename).c_str());
  rmdir(dir.c_str());
}

void ensure_dir(const std::string &dir)
{
  struct stat dir_stats;
  if (stat(dir.c_str(), &dir_stats) != 0)
  {
    mkdir(dir.c_str(), 0755);
  }
}
} // namespace

void zo_plugin_tests()
{
  describe("zo ZO_PLUGINS_DIR opt-in", []() -> void
  {
    it("does not load plug-ins from the legacy <exec_dir>/plugins fallback when ZO_PLUGINS_DIR is unset", []()
    {
      const std::string probe_file = "legacy_fallback_probe.so";
      ensure_dir(LEGACY_FALLBACK_DIR);
      write_regular_file(LEGACY_FALLBACK_DIR + "/" + probe_file, "not a real shared object, just needs to exist");

      int rc = 0;
      std::string response;
      // Explicitly clear ZO_PLUGINS_DIR for this invocation only, so a value
      // leaked from the outer test environment can't mask a regression here.
      rc = execute_command_with_output("ZO_PLUGINS_DIR= " + zo_command + " plugins list", response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).Not().ToContain(probe_file);
      Expect(response).Not().ToContain(UNREGISTERED_BANNER);

      remove_dir_with_file(LEGACY_FALLBACK_DIR, probe_file);
    });

    it("does not touch any plugins directory when ZO_PLUGINS_DIR is unset and no legacy directory exists", []()
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output("ZO_PLUGINS_DIR= " + zo_command + " plugins list", response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).Not().ToContain(UNREGISTERED_BANNER);
    });

    it("honors an explicitly-set ZO_PLUGINS_DIR and surfaces its contents", []()
    {
      const std::string probe_file = "opt_in_probe.so";
      ensure_dir(EXPLICIT_OPT_IN_DIR);
      write_regular_file(EXPLICIT_OPT_IN_DIR + "/" + probe_file, "not a real shared object, just needs to exist");

      int rc = 0;
      std::string response;
      rc = execute_command_with_output("ZO_PLUGINS_DIR=" + EXPLICIT_OPT_IN_DIR + " " + zo_command + " plugins list",
                                       response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain(UNREGISTERED_BANNER);
      Expect(response).ToContain(probe_file);

      remove_dir_with_file(EXPLICIT_OPT_IN_DIR, probe_file);
    });

    it("does not crash when ZO_PLUGINS_DIR is explicitly set but points at a nonexistent directory", []()
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output(
          "ZO_PLUGINS_DIR=" + EXPLICIT_OPT_IN_DIR + "/does_not_exist " + zo_command + " plugins list", response);

      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).Not().ToContain(UNREGISTERED_BANNER);
    });
  });
}
