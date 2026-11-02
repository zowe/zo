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

#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "../zlog_util.hpp"
#include <string>
#include "ztest.hpp"
#include "ztype.h"
#include "zutils.hpp"
#include "zo.test.hpp"
#include "zo.ds.test.hpp"
#include "zo.uss.test.hpp"
#include "zo.job.test.hpp"
#include "zo.system.test.hpp"
#include "zoa.console.test.hpp"
#include "zo.tso.test.hpp"
#include "zo.plugin.test.hpp"

using namespace ztst;

namespace
{
void remove_dsect_fixture(const std::string &path)
{
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0)
    return;
  if (!S_ISDIR(info.st_mode))
  {
    unlink(path.c_str());
    return;
  }
  DIR *dir = opendir(path.c_str());
  if (dir != nullptr)
  {
    while (const auto *entry = readdir(dir))
    {
      const std::string name = entry->d_name;
      if (name != "." && name != "..")
        remove_dsect_fixture(path + "/" + name);
    }
    closedir(dir);
  }
  rmdir(path.c_str());
}

class DsectOutputFixture
{
public:
  const std::string home = get_random_uss(zlog_util::get_home_dir());
  const std::string output_dir = home + "/.zowex/ccnedsct";

  DsectOutputFixture()
  {
    Expect(home.find('\'')).ToBe(std::string::npos);
    Expect(mkdir(home.c_str(), 0700)).ToBe(0);
    Expect(mkdir((home + "/.zowex").c_str(), 0700)).ToBe(0);
  }

  ~DsectOutputFixture()
  {
    remove_dsect_fixture(home);
  }

  int run(std::string &response, const std::string &options = "") const
  {
    // Use invalid data set names to skip conversion.
    return execute_command_with_output(
        "HOME='" + home + "' ZO_LOGS_DIR='" + home + "/logger' ZO_PLUGINS_DIR= " + zo_command +
            " tool ccnedsct --ad INVALID-DATASET --cd INVALID-DATASET " + options,
        response);
  }
};

void expect_private_dsect_path(const std::string &path, bool directory)
{
  struct stat info{};
  ExpectWithContext(lstat(path.c_str(), &info), path).ToBe(0);
  Expect(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)).ToBe(true);
  Expect(info.st_uid).ToBe(geteuid());
  Expect(info.st_mode & 07777).ToBe(0700);
}
} // namespace

void zo_tests()
{

  describe("zo", []() -> void
  {
    it("should list a version of the tool", []() -> void
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output(zo_command + " version", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("zo");
      Expect(response).ToContain("Version:");

      // Version information
      static const std::regex rev(R"(Version:\s*(\S+))");
      std::smatch mv;

      Expect(std::regex_search(response, mv, rev)).ToBe(true);
      const std::string version = mv[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum

      Expect(response).ToContain("Build Date:");

      // Build date information
      static const std::regex red(R"(Build Date:\s*([^\r\n]+))");
      std::smatch md;

      Expect(std::regex_search(response, md, red)).ToBe(true);
      const std::string buildDate = md[1].str();

      Expect(buildDate.length()).ToBeGreaterThanOrEqualTo(11); // MMM DD YYYY at minimum

      Expect(response).Not().ToContain("unknown");
    });
    it("should list a short version of the tool", []() -> void
    {
      int rc = 0;
      std::string response;
      rc = execute_command_with_output(zo_command + " --version", response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).Not().ToContain("zo");
      Expect(response).Not().ToContain("Version");
      Expect(response).Not().ToContain("Build Date");
      Expect(response).Not().ToContain("unknown");

      static const std::regex rev(R"(^\s*(\d+\.\d+\.\d+)(?:[-+][^\r\n]*)?\s*$)");
      std::smatch mv;

      Expect(std::regex_search(response, mv, rev)).ToBe(true);
      const std::string version = mv[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum
    });
#ifdef RELEASE_BUILD
    it("should remain less than 10mb in size",
#else
    xit("should remain less than 10mb in size",
#endif
       []() -> void
    {
      std::string response;
      execute_command_with_output("cat ../build-out/zowex | wc -c", response);
      int file_size = std::stoi(response);
      ExpectWithContext(file_size, response).ToBeLessThan(10 * 1024 * 1024);
    });
    describe("--json output", []() -> void
    {
      it("should emit a structured envelope for the version command", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " version --json", response);
        ExpectWithContext(rc, response).ToBe(0);

        // One line, one JSON object.
        Expect(response.find('\n')).ToBe(response.length() - 1);
        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"exitCode\":0");
        Expect(response).ToContain("\"version\"");
        Expect(response).ToContain("\"buildDate\"");

        // The human-readable banner must stay out of the payload.
        Expect(response).Not().ToContain("Copyright Contributors");
      });

      it("should accept --json before the subcommand", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " --json uss list /tmp", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"items\"");
      });

      it("should list USS files as structured items without --rfc", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " uss list /tmp --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("\"items\"");
        Expect(response).ToContain("\"returnedRows\"");
        Expect(response).ToContain("\"name\"");
      });

      it("should report a failing command in the envelope", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set list NO.SUCH.HLQ.AT.ALL --json", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("\"success\":false");
        // Diagnostics land in the envelope rather than bare stderr.
        Expect(response).ToContain("\"stderr\"");
      });

      it("should still emit an envelope when the options are invalid", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set list SOME.HLQ --no-such-option --json", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        // The pre-scan covers failures that never reach a handler.
        Expect(response).ToContain("\"success\":false");
      });

      it("should print help rather than JSON for a command group", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " data-set --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).ToContain("view");
      });

      it("should print help rather than JSON for the root command", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " --json", response);
        ExpectWithContext(rc, response).ToBe(0);
        // The root handler prints help; wrapping that serves nobody.
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).ToContain("data-set");
      });
      it("should leave output unchanged when --json is absent", []() -> void
      {
        std::string response;
        int rc = execute_command_with_output(zo_command + " uss list /tmp", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("\"success\"");
        Expect(response).Not().ToContain("\"items\"");
      });
    });
    describe("tool ccnedsct output security", []() -> void
    {
      it("keeps private, distinct output files across invocations", []() -> void
      {
        DsectOutputFixture fixture;
        std::string response;
        Expect(fixture.run(response)).Not().ToBe(0);
        std::string adata, chdr, sysprint, sysout;
        std::istringstream(response) >> adata >> chdr >> sysprint >> sysout;
        Expect(sysprint.find(fixture.output_dir + "/")).ToBe(size_t(0));
        Expect(sysout.find(fixture.output_dir + "/")).ToBe(size_t(0));
        expect_private_dsect_path(fixture.home + "/.zowex", true);
        expect_private_dsect_path(fixture.output_dir, true);
        expect_private_dsect_path(sysprint, false);
        expect_private_dsect_path(sysout, false);
        Expect(sysprint).Not().ToBe(sysout);
        std::ofstream(sysprint) << "keep previous listing";

        response.clear();
        Expect(fixture.run(response)).Not().ToBe(0);
        std::string next_sysprint, next_sysout;
        std::istringstream(response) >> adata >> chdr >> next_sysprint >> next_sysout;
        Expect(next_sysprint).Not().ToBe(sysprint);
        Expect(next_sysout).Not().ToBe(sysout);
        expect_private_dsect_path(next_sysprint, false);
        expect_private_dsect_path(next_sysout, false);
        std::string previous;
        std::ifstream previous_file(sysprint);
        std::getline(previous_file, previous);
        Expect(previous).ToBe("keep previous listing");
      });

      it("rejects symlinked private directories without writing through them", []() -> void
      {
        for (const char *suffix : {"/.zowex", "/.zowex/ccnedsct"})
        {
          DsectOutputFixture fixture;
          const std::string target = fixture.home + "/target";
          Expect(mkdir(target.c_str(), 0700)).ToBe(0);
          const std::string link = fixture.home + suffix;
          if (link == fixture.home + "/.zowex")
            Expect(rmdir(link.c_str())).ToBe(0);
          Expect(symlink(target.c_str(), link.c_str())).ToBe(0);
          std::string response;
          Expect(fixture.run(response)).Not().ToBe(0);
          // The target must remain empty.
          Expect(rmdir(target.c_str())).ToBe(0);
        }
      });

      it("rejects non-private directories without changing their permissions", []() -> void
      {
        DsectOutputFixture fixture;
        Expect(mkdir(fixture.output_dir.c_str(), 0700)).ToBe(0);
        Expect(chmod(fixture.output_dir.c_str(), 0770)).ToBe(0);
        std::string response;
        Expect(fixture.run(response)).Not().ToBe(0);
        struct stat info{};
        Expect(lstat(fixture.output_dir.c_str(), &info)).ToBe(0);
        Expect(info.st_mode & 07777).ToBe(0770);
        Expect(rmdir(fixture.output_dir.c_str())).ToBe(0);
      });

      it("secures omitted sysout when sysprint is explicitly overridden", []() -> void
      {
        DsectOutputFixture fixture;
        const std::string explicit_sysprint = fixture.home + "/listing.txt";
        std::string response;
        Expect(fixture.run(response, "--sysprint '" + explicit_sysprint + "'")).Not().ToBe(0);
        std::string adata, chdr, sysprint, sysout;
        std::istringstream(response) >> adata >> chdr >> sysprint >> sysout;
        Expect(sysprint).ToBe(explicit_sysprint);
        Expect(sysout.find(fixture.output_dir + "/")).ToBe(size_t(0));
        expect_private_dsect_path(sysout, false);
      });
    });
    zo_ds_tests();
    zo_uss_tests();
    zo_job_tests();
    zo_system_tests();
    zo_tso_tests();
    zo_plugin_tests();
  });

  describe("zoa", []() -> void
  {
    zoa_console_tests();
  });
}
