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

#include "ztest.hpp"
#include <cstddef>
#include <cstring>
#include <ctime>
#include <iterator>
#include <sstream>
#include <cstdlib>
#include <string>
#include "zutils.hpp"
#include <cstdio>
#include "test_utils.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../zut.hpp"

using namespace ztst;

const std::string ussTestDir = "/tmp/zowex-uss_" + get_random_string(10);

void zo_uss_tests()
{
  int rc;
  std::string response;
  describe("uss", [&]() -> void
  {
    // Start by creating a /tmp/zowex-uss test directory
    beforeAll([&response]() -> void
    {
      execute_command_with_output(zo_command + " uss create-dir " + ussTestDir + " --mode 777", response);
    });

    // Reset the RC to zero before each test
    beforeEach([&rc]() -> void
    {
      rc = 0;
    });

    // Clean up the test directory
    afterAll([&response]() -> void
    {
      execute_command_with_output(zo_command + " uss delete " + ussTestDir + " --recursive", response);
    });

    // Helper function to create a test file
    auto create_test_file_cmd = [&](const std::string &uss_file, const std::string &options = "") -> void
    {
      std::string command = zo_command + " uss create-file " + uss_file + " " + options;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("USS file '" + uss_file + "' created");
    };

    // Helper function to create a test directory
    auto create_test_dir_cmd = [&](const std::string &uss_dir, const std::string &options = "") -> void
    {
      std::string command = zo_command + " uss create-dir " + uss_dir + " " + options;
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("USS directory '" + uss_dir + "' created");
    };

    describe("copy uss tests", [&]() -> void
    {
      // Helper functions
      auto copy_cmd = [&](const std::string &source, const std::string &dest,
                          const std::string &options = "") -> std::pair<std::string, int>
      {
        int lrc;
        std::string lresponse;
        std::string command = zo_command + " uss copy \"" + source + "\" \"" + dest + "\" " + options;
        lrc = execute_command_with_output(command, lresponse);
        return {lresponse, lrc};
      };

      auto delete_cmd = [&](const std::string &file_path,
                            const std::string &options = "") -> std::pair<std::string, int>
      {
        int lrc;
        std::string lresponse;
        std::string command = zo_command + " uss delete \"" + file_path + "\" " + options;
        lrc = execute_command_with_output(command, lresponse);
        return {lresponse, lrc};
      };

      auto list_cmd = [&](const std::string &file_path,
                          const std::string &options = "") -> std::pair<std::string, int>
      {
        int lrc;
        std::string lresponse;
        std::string command = zo_command + " uss list \"" + file_path + "\" " + options;
        lrc = execute_command_with_output(command, lresponse);
        return {lresponse, lrc};
      };

      auto chmod_cmd = [&](const std::string &file_path, const std::string &permissions,
                           const std::string &options = "") -> std::pair<std::string, int>
      {
        int lrc;
        std::string lresponse;
        std::string command = zo_command + " uss chmod " + permissions + " \"" + file_path + "\"";
        lrc = execute_command_with_output(command, lresponse);
        return {lresponse, lrc};
      };

      std::string uss_path;
      std::string file_a;
      std::string file_b;
      std::string dir_a;
      std::string dir_b;

      beforeEach([&]()
      {
        uss_path = get_random_uss(ussTestDir);
        file_a = uss_path + "/test_file_a";
        file_b = uss_path + "/test_file_b";
        dir_a = uss_path + "/test_dir_a";
        dir_b = uss_path + "/test_dir_b";
        create_test_dir_cmd(uss_path);
      });

      it("copy file->file smoke", [&]()
      {
        std::string source_file = file_a;
        std::string target_file = file_b;
        create_test_file_cmd(file_a);
        std::pair<std::string, int> copy_result, list_file_result;

        // normal copy
        copy_result = copy_cmd(source_file, target_file);
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(0);
        list_file_result = list_cmd(target_file);
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);

        // copy when target already exists: replaces it
        copy_result = copy_cmd(source_file, target_file);
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(0);
        list_file_result = list_cmd(target_file);
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(0);
        list_file_result = list_cmd(target_file, "-al");
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);
        Expect(list_file_result.first).ToContain("-rw-r--r--");

        // copy with --preserve-attributes
        std::string output;
        delete_cmd(target_file);
        zut_run_program("chmod", {"777", source_file}, output);
        copy_result = copy_cmd(source_file, target_file, "--preserve-attributes");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(0);
        list_file_result = list_cmd(target_file, "-al");
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);
        Expect(list_file_result.first).ToContain("-rwxrwxrwx");
      });

      it("copy dir->dir smoke", [&]()
      {
        std::string source_dir = dir_a;
        std::string target_dir = dir_b;
        create_test_dir_cmd(source_dir);
        std::pair<std::string, int> copy_result, list_file_result;

        copy_result = copy_cmd(source_dir, target_dir, "--recursive --follow-symlinks --force --preserve-attributes");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(0);
        list_file_result = list_cmd(target_dir); // dir created
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);
      });

      it("copy dir->file smoke", [&]()
      {
        std::string source_dir = dir_a;
        std::string target_file = file_b;
        create_test_dir_cmd(source_dir);
        create_test_file_cmd(target_file);
        std::pair<std::string, int> copy_result, list_file_result;

        copy_result = copy_cmd(source_dir, target_file);
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
        list_file_result = list_cmd(target_file); // file still exists
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(0);
      });

      it("copy file->dir smoke", [&]()
      {
        const std::string source_file = file_a;
        const std::string target_dir = dir_a;
        std::string output_file = target_dir + "/" + get_basename(source_file);
        create_test_dir_cmd(source_file);
        create_test_file_cmd(target_dir);
        std::pair<std::string, int> copy_result, list_file_result;

        copy_result = copy_cmd(source_file, target_dir);
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
        list_file_result = list_cmd(output_file);
        ExpectWithContext(list_file_result.second, list_file_result.first).ToBe(255);
      });

      it("special characters", [&]()
      {
        const std::vector<std::string> test_cases = {
            "abc*",
            "abc\\*",
            "abc\\\\*",
            "def?ghi",
            "\"with*quotes\"",
            "tes[a-z]+",
            "'lit*'",
        };

        const std::string test_file = file_a;
        const std::string test_dir = dir_a;
        create_test_file_cmd(test_file);
        create_test_dir_cmd(test_dir);

        for (int i = 0; i < test_cases.size(); i++)
        {
          std::pair<std::string, int> result;
          std::string cmd_output, list_output;
          const std::string test_basename = test_cases.at(i);
          const std::string test = uss_path + "/" + test_basename;

          // copy static file -> new file w/ special chars
          result = copy_cmd(test_file, test);
          ExpectWithContext(result.second, result.first).ToBe(0);

          result = list_cmd(test, "-al");
          ExpectWithContext(result.second, result.first).ToBe(0);
          Expect(result.first).ToContain("-rw-r--r--");

          std::pair<std::string, int> del_res = delete_cmd(test, "-r");
          ExpectWithContext(del_res.second, del_res.first).ToBe(0);

          // copy static dir -> new dir w/ special chars
          result = copy_cmd(test_dir, test, "-r");
          ExpectWithContext(result.second, result.first).ToBe(0);

          result = list_cmd(test, "-al");
          ExpectWithContext(result.second, result.first).ToBe(0);
          Expect(result.first).ToContain("drwxr-xr-x");
        }
      });

      it("illegal syntax tests", [&]()
      {
        std::pair<std::string, int> copy_result;

        copy_result = copy_cmd("", "some-destination");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
        copy_result = copy_cmd("/some/source", "");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
        copy_result = copy_cmd("", "");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
        copy_result = copy_cmd("/some/source", "/some/dest", "--illegal-parameter");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(1);
      });

      it("illegal parameter combinations", [&]()
      {
        std::pair<std::string, int> copy_result;
        std::string source_dir = dir_a;
        std::string target_dir = dir_b;

        create_test_dir_cmd(source_dir);

        // requires --recursive
        copy_result = copy_cmd(source_dir, target_dir, "--follow-symlinks");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);

        // still requires --recursive
        copy_result = copy_cmd(source_dir, target_dir, "--follow-symlinks --preserve-attributes --force");
        ExpectWithContext(copy_result.second, copy_result.first).ToBe(255);
      });
    });

    describe("chmod", [&]() -> void
    {
      std::string uss_path;

      describe("on a file", [&]() -> void
      {
        beforeEach([&]() -> void
        {
          uss_path = get_random_uss(ussTestDir);
          create_test_file_cmd(uss_path);
        });

        it("should properly chmod a file", [&]() -> void
        {
          std::string getPermissionsCommand = "ls -l " + uss_path;
          std::string chmodFileCommand = zo_command + " uss chmod 777 " + uss_path;

          rc = execute_command_with_output(getPermissionsCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(("-rw-r--r--"));

          rc = execute_command_with_output(chmodFileCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          rc = execute_command_with_output(getPermissionsCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(("-rwxrwxrwx"));
        });
      });

      describe("on a directory", [&]() -> void
      {
        beforeEach([&]() -> void
        {
          uss_path = get_random_uss(ussTestDir) + "_dir";
          create_test_dir_cmd(uss_path);
        });

        it("should properly chmod a directory", [&]() -> void
        {
          std::string getPermissionsCommand = "ls -ld " + uss_path;
          std::string chmodFileCommand = zo_command + " uss chmod 777 " + uss_path + " -r";

          rc = execute_command_with_output(getPermissionsCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(("drwxr-xr-x"));

          rc = execute_command_with_output(chmodFileCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          rc = execute_command_with_output(getPermissionsCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(("drwxrwxrwx"));
        });
      });

      it("should properly handle calling chmod on a file that does not exist", [&]() -> void
      {
        std::string uss_file = ussTestDir + "/test_does_not_exist";
        std::string chmodFileCommand = zo_command + " uss chmod 777 " + uss_file;
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(chmodFileCommand, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Path '" + uss_file + "' does not exist");
      });
    });
    describe("chown", [&]() -> void
    {
      std::string uss_path;
      static int testUid;
      static int testGid;

      beforeAll([&]() -> void
      {
        std::string resp;
        int rc;

        // Use the current user's own UID — chown to self is always permitted
        rc = execute_command_with_output("id -u", resp);
        if (rc != 0)
          throw std::runtime_error("Could not determine current UID");
        resp.erase(resp.find_last_not_of(" \t\r\n") + 1);
        testUid = std::stoi(resp);

        // Pick a supplementary group that differs from the primary GID so the
        // change is observable. Fall back to the primary GID if none found.
        std::string primaryResp;
        rc = execute_command_with_output("id -g", primaryResp);
        if (rc != 0)
          throw std::runtime_error("Could not determine primary GID");
        primaryResp.erase(primaryResp.find_last_not_of(" \t\r\n") + 1);
        int primaryGid = std::stoi(primaryResp);

        std::string allGroupsResp;
        rc = execute_command_with_output("id -G", allGroupsResp);
        if (rc != 0)
          throw std::runtime_error("Could not determine supplementary GIDs");

        testGid = primaryGid;
        std::istringstream iss(allGroupsResp);
        std::string token;
        while (iss >> token)
        {
          int gid = std::stoi(token);
          if (gid != primaryGid)
          {
            testGid = gid;
            break;
          }
        }
      });

      describe("on a file", [&]() -> void
      {
        beforeEach([&]() -> void
        {
          uss_path = get_random_uss(ussTestDir);
          create_test_file_cmd(uss_path);
        });

        it("should properly change the group on a file using numeric GID", [&]() -> void
        {
          std::string newGroupChownCommand = zo_command + " uss chown :" + std::to_string(testGid) + " " + uss_path;
          std::string listUser = "ls -n " + uss_path;

          rc = execute_command_with_output(newGroupChownCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("USS path '" + uss_path + "' owner changed to ':" + std::to_string(testGid) + "'");

          rc = execute_command_with_output(listUser, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(std::to_string(testGid));
        });

        it("should properly change the group and user on a file using numeric IDs", [&]() -> void
        {
          std::string newUserNewGroupChownCommand =
              zo_command + " uss chown " + std::to_string(testUid) + ":" + std::to_string(testGid) + " " + uss_path;
          std::string listUser = "ls -n " + uss_path;

          rc = execute_command_with_output(newUserNewGroupChownCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("USS path '" + uss_path + "' owner changed to '" + std::to_string(testUid) + ":" +
                                     std::to_string(testGid) + "'");

          rc = execute_command_with_output(listUser, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(std::to_string(testUid));
          Expect(response).ToContain(std::to_string(testGid));
        });
      });

      describe("on a directory", [&]() -> void
      {
        beforeEach([&]() -> void
        {
          uss_path = get_random_uss(ussTestDir) + "_dir";
          create_test_dir_cmd(uss_path);
        });

        it("should properly change the group and user on a directory using numeric IDs", [&]() -> void
        {
          std::string newUserNewGroupChownCommand = zo_command + " uss chown " + std::to_string(testUid) + ":" +
                                                    std::to_string(testGid) + " " + uss_path + " -r";

          // Use -nd to list directory details numerically without entering it
          std::string listUser = "ls -nd " + uss_path;

          rc = execute_command_with_output(newUserNewGroupChownCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("USS path '" + uss_path + "' owner changed to '" + std::to_string(testUid) + ":" +
                                     std::to_string(testGid) + "'");

          rc = execute_command_with_output(listUser, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(std::to_string(testUid));
          Expect(response).ToContain(std::to_string(testGid));
        });
      });
    });
    describe("chtag", [&]() -> void
    {
      std::string uss_file;

      describe("on a file", [&]() -> void
      {
        beforeEach([&]() -> void
        {
          uss_file = get_random_uss(ussTestDir);
          create_test_file_cmd(uss_file);
        });

        it("should properly change the file tag on a file", [&]() -> void
        {
          std::string chtagCommand = zo_command + " uss chtag " + uss_file + " IBM-1047";
          std::string listUser = "ls -alT " + uss_file;

          rc = execute_command_with_output(chtagCommand, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("USS path '" + uss_file + "' tag changed to 'IBM-1047'");

          rc = execute_command_with_output(listUser, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("IBM-1047");
        });

        it("should properly handle setting an invalid file tag", [&]() -> void
        {
          std::string chtagCommand = zo_command + " uss chtag " + uss_file + " bad-tag";
          std::string listUser = "ls -alT " + uss_file;
          {
            test_utils::ErrorStreamCapture c;
            rc = execute_command_with_output(chtagCommand, response);
          }
          ExpectWithContext(rc, response).ToBe(255);
          Expect(response).ToContain("Invalid tag 'bad-tag' - not a valid CCSID or display name");
        });
      });

      it("should properly change the file tag recursively (on a file)", [&]() -> void
      {
        std::string uss_dir_like_file = get_random_uss(ussTestDir) + "_dir";
        create_test_file_cmd(uss_dir_like_file);
        std::string chtagCommand = zo_command + " uss chtag " + uss_dir_like_file + " IBM-1047 -r";
        std::string listUser = "ls -alT " + uss_dir_like_file;

        rc = execute_command_with_output(chtagCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS path '" + uss_dir_like_file + "' tag changed to 'IBM-1047'");

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("IBM-1047");
      });
    });
    describe("create-dir", [&]() -> void
    {
      it("should properly create a directory with mode 777 specified", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir) + "_dir";
        create_test_dir_cmd(uss_dir, "--mode 777");
        std::string listUser = "ls -ld " + uss_dir;

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("drwxrwxrwx");
      });
      it("should properly create a directory with mode 700 specified", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir) + "_dir";
        create_test_dir_cmd(uss_dir, "--mode 700");
        std::string listUser = "ls -ld " + uss_dir;

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("drwx------");
      });
      it("should properly handle recursive directory creation", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir) + "_dir/recursive_test";
        create_test_dir_cmd(uss_dir);
        std::string listUser = "ls -ld " + uss_dir;

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(uss_dir);
      });
      it("should properly handle an invalid directory path creation", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir);
        std::string command = zo_command + " uss create-dir " + uss_dir + "//";
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(command, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Failed to create directory '" + uss_dir + "/'");
      });
    });
    describe("create-file", [&]() -> void
    {
      it("should properly create a file with mode 777 specified", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir);
        create_test_file_cmd(uss_file, "--mode 777");
        std::string listUser = "ls -l " + uss_file;

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("-rwxrwxrwx");
      });
      it("should properly create a file with mode 644 by default", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir);
        create_test_file_cmd(uss_file);
        std::string listUser = "ls -l " + uss_file;

        rc = execute_command_with_output(listUser, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("-rw-r--r--");
      });
      it("should properly handle a creating a file in a non-existent directory", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir) + "/does_not_exist";
        std::string command = zo_command + " uss create-file " + uss_file;
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(command, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("could not create USS file: '" + uss_file + "'");
      });
      it("should not overwrite existing files", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir);
        std::string command = zo_command + " uss create-file " + uss_file;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS file '" + uss_file + "' created");

        // Write "test" data
        command = "echo test | " + zo_command + " uss write " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to");

        // Read "test" data to confirm
        command = zo_command + " uss view " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");

        // Create the same file again
        command = zo_command + " uss create-file " + uss_file;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Warning");

        // Read "test" data to confirm
        command = zo_command + " uss view " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");
      });
      it("should overwrite existing files if --overwrite is specified", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir);
        std::string command = zo_command + " uss create-file " + uss_file;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS file '" + uss_file + "' created");

        // Write "test" data
        command = "echo test | " + zo_command + " uss write " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to");

        // Read "test" data to confirm
        command = zo_command + " uss view " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");

        // Create the same file again
        command = zo_command + " uss create-file " + uss_file + " --overwrite";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS file '" + uss_file + "' created");

        // Read to confirm data was overwritten
        command = zo_command + " uss view " + uss_file + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("test");
      });
      it("should not create a file through a dangling symbolic link", [&]() -> void
      {
        std::string target = get_random_uss(ussTestDir);
        std::string link = get_random_uss(ussTestDir);
        TestFileGuard link_guard(link.c_str(), 'l', target.c_str());

        std::string command = zo_command + " uss create-file " + link + " --mode 777 --overwrite";
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(command, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Path '" + link + "' is a symbolic link");

        struct stat st{};
        Expect(lstat(target.c_str(), &st)).ToBe(-1);
      });
    });
    describe("delete", [&]() -> void
    {
      it("should properly delete a file", [&]() -> void
      {
        std::string uss_file = get_random_uss(ussTestDir);
        create_test_file_cmd(uss_file);

        std::string deleteCommand = zo_command + " uss delete " + uss_file;
        rc = execute_command_with_output(deleteCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS item '" + uss_file + "' deleted");

        std::string listCommand = "ls " + uss_file;
        rc = execute_command_with_output(listCommand, response);
        Expect(rc).ToBe(1);
      });

      it("should properly delete a directory recursively", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir) + "_dir";
        create_test_dir_cmd(uss_dir);
        std::string uss_file_in_dir = uss_dir + "/test_file.txt";
        create_test_file_cmd(uss_file_in_dir);

        std::string deleteCommand = zo_command + " uss delete " + uss_dir + " -r";
        rc = execute_command_with_output(deleteCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS item '" + uss_dir + "' deleted");

        std::string listCommand = "ls -d " + uss_dir;
        rc = execute_command_with_output(listCommand, response);
        Expect(rc).ToBe(1);
      });
      it("should properly handle deleting a non-existent item", [&]() -> void
      {
        std::string uss_file = ussTestDir + "/test_does_not_exist";
        std::string deleteCommand = zo_command + " uss delete " + uss_file;
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(deleteCommand, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Path '" + uss_file + "' does not exist");
      });

      it("should properly handle deleting a directory without the recursive flag", [&]() -> void
      {
        std::string uss_dir = get_random_uss(ussTestDir) + "_dir";
        create_test_dir_cmd(uss_dir);
        std::string deleteCommand = zo_command + " uss delete " + uss_dir;
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(deleteCommand, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Path '" + uss_dir + "' is a directory and recursive was false");
      });

      it("should properly delete a symlink to a directory without recursive flag", [&]() -> void
      {
        std::string target_dir = get_random_uss(ussTestDir) + "_target_dir";
        std::string link_path = get_random_uss(ussTestDir) + "_dir_link";
        create_test_dir_cmd(target_dir);

        int link_rc = symlink(target_dir.c_str(), link_path.c_str());
        Expect(link_rc).ToBe(0);

        std::string deleteCommand = zo_command + " uss delete " + link_path;
        rc = execute_command_with_output(deleteCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS item '" + link_path + "' deleted");

        std::string listLinkCommand = "ls -ld " + link_path;
        rc = execute_command_with_output(listLinkCommand, response);
        Expect(rc).ToBe(1);

        std::string listTargetCommand = "ls -ld " + target_dir;
        rc = execute_command_with_output(listTargetCommand, response);
        ExpectWithContext(rc, response).ToBe(0);

        execute_command_with_output(zo_command + " uss delete " + target_dir + " -r", response);
      });

      it("should not traverse symlink targets during recursive directory delete", [&]() -> void
      {
        std::string external_dir = get_random_uss(ussTestDir) + "_external_dir";
        std::string external_file = external_dir + "/preserve.txt";
        std::string parent_dir = get_random_uss(ussTestDir) + "_parent_dir";
        std::string link_path = parent_dir + "/external_link";

        create_test_dir_cmd(external_dir);
        create_test_file_cmd(external_file);
        create_test_dir_cmd(parent_dir);

        int link_rc = symlink(external_dir.c_str(), link_path.c_str());
        Expect(link_rc).ToBe(0);

        std::string deleteCommand = zo_command + " uss delete " + parent_dir + " -r";
        rc = execute_command_with_output(deleteCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("USS item '" + parent_dir + "' deleted");

        std::string listParentCommand = "ls -ld " + parent_dir;
        rc = execute_command_with_output(listParentCommand, response);
        Expect(rc).ToBe(1);

        std::string listExternalFileCommand = "ls -l " + external_file;
        rc = execute_command_with_output(listExternalFileCommand, response);
        ExpectWithContext(rc, response).ToBe(0);

        execute_command_with_output(zo_command + " uss delete " + external_dir + " -r", response);
      });
    });

    describe("write and view", [&]() -> void
    {
      std::string uss_path;
      beforeEach([&]() -> void
      {
        uss_path = get_random_uss(ussTestDir);
        create_test_file_cmd(uss_path);
      });
      it("should properly write and view files", [&]() -> void
      {
        std::string viewCommand = zo_command + " uss view " + uss_path + " --ec UTF-8";
        std::string writeCommand = "echo 'Hello World!' | " + zo_command + " uss write " + uss_path + " --ec UTF-8";
        std::string view_response;

        rc = execute_command_with_output(writeCommand, response);
        ExpectWithContext(rc, "Write command failed").ToBe(0);

        rc = execute_command_with_output(viewCommand, view_response);
        ExpectWithContext(rc, view_response).ToBe(0);

        Expect(view_response).ToContain("Hello World!");
      });
      it("should successfully write using a valid etag", [&]() -> void
      {
        std::string viewCommand = zo_command + " uss view " + uss_path + " --return-etag --ec UTF-8";
        std::string writeCommand = "echo 'Initial Content' | " + zo_command + " uss write " + uss_path + " --ec UTF-8";
        std::string view_response;

        rc = execute_command_with_output(writeCommand, response);
        ExpectWithContext(rc, "Initial write failed").ToBe(0);

        rc = execute_command_with_output(viewCommand, view_response);
        ExpectWithContext(rc, view_response).ToBe(0);
        Expect(view_response).ToContain("Initial Content");
        std::string valid_etag = parse_etag_from_output(view_response);
        ExpectWithContext(valid_etag, "Failed to parse ETag from view output.").Not().ToBe("");

        std::string writeWithValidEtagCmd =
            "echo 'Updated Content' | " + zo_command + " uss write " + uss_path + " --ec UTF-8 --etag " + valid_etag;
        rc = execute_command_with_output(writeWithValidEtagCmd, response);
        ExpectWithContext(rc, "Write with valid etag failed").ToBe(0);

        std::string final_view_response;
        std::string simpleViewCmd = zo_command + " uss view " + uss_path + " --ec UTF-8";
        rc = execute_command_with_output(simpleViewCmd, final_view_response);
        Expect(final_view_response).ToContain("Updated Content");
      });
      it("should fail to view a file that does not exist", [&]() -> void
      {
        std::string viewCommand = zo_command + " uss view /tmp/does/not/exist";
        std::string view_response;
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(viewCommand, view_response);
        }
        ExpectWithContext(rc, view_response).ToBe(255);
        Expect(view_response).ToContain("Path /tmp/does/not/exist does not exist");
      });
      it("should properly translate EBCDIC text to ASCII/UTF-8", [&]() -> void
      {
        std::string ebcdic_text = "Hello World - This is a test.";

        std::string expected_ascii_text = "\x48\x65\x6c\x6c\x6f\x20" // "Hello "
                                          "\x57\x6f\x72\x6c\x64\x20" // "World "
                                          "\x2d\x20"                 // "- "
                                          "\x54\x68\x69\x73\x20"     // "This "
                                          "\x69\x73\x20"             // "is "
                                          "\x61\x20"                 // "a "
                                          "\x74\x65\x73\x74\x2e";    // "test."

        std::string writeCommand = "printf '%s' '" + ebcdic_text + "' | " + zo_command + " uss write " + uss_path +
                                   " --lec IBM-1047 --ec UTF-8";
        rc = execute_command_with_output(writeCommand, response);
        ExpectWithContext(rc, "Write command failed").ToBe(0);

        std::string viewCommand = zo_command + " uss view " + uss_path + " --ec UTF-8";
        std::string view_response_hex_dump;
        rc = execute_command_with_output(viewCommand, view_response_hex_dump);
        ExpectWithContext(rc, view_response_hex_dump).ToBe(0);

        Expect(memcmp(view_response_hex_dump.data(), expected_ascii_text.data(), view_response_hex_dump.length()))
            .ToBe(0);
        Expect(view_response_hex_dump.length()).ToBe(expected_ascii_text.length());
      });
      it("should write content to a USS file with multibyte encoding", [&]() -> void
      {
        std::string response;
        std::string command = "echo '\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf' | " + zo_command +
                              " uss write " + uss_path + " --encoding IBM-939";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + uss_path + "'");

        command = zo_command + " uss view " + uss_path + " --rfb --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("0e 44 8a 44 bd 44 97 44 92 44 9d 0f");
      });
      it("should handle write and view for a FIFO pipe", [&]() -> void
      {
        std::string writeCommand = "echo 'Hello World!' | " + zo_command + " uss write " + uss_path + " --ec binary";
        std::string viewCommand = zo_command + " uss view " + uss_path + " --ec binary";
        std::string view_response;
        mkfifo(uss_path.c_str(), 0777);

        rc = execute_command_with_output(writeCommand, response);
        ExpectWithContext(rc, "Write command failed").ToBe(0);

        rc = execute_command_with_output(viewCommand, view_response);
        ExpectWithContext(rc, view_response).ToBe(0);

        Expect(view_response).ToContain("Hello World!");
      });
      it("should handle write and view for a symlink", [&]() -> void
      {
        // Create symlink
        std::string symPath = uss_path + "_sym";
        symlink(uss_path.c_str(), symPath.c_str());

        // Write to sym link
        std::string writeCommand = "echo 'Hello World!' | " + zo_command + " uss write " + symPath + " --ec binary";

        // Read from original path
        std::string viewCommand = zo_command + " uss view " + uss_path + " --ec binary";
        std::string listCommand = zo_command + " uss ls " + uss_path + " -l";
        std::string view_response;

        rc = execute_command_with_output(writeCommand, response);
        ExpectWithContext(rc, "Write command failed").ToBe(0);

        rc = execute_command_with_output(viewCommand, view_response);
        ExpectWithContext(rc, view_response).ToBe(0);

        Expect(view_response).ToContain("Hello World!");
        unlink(symPath.c_str());
      });
    });

    describe("list (ls)", [&]() -> void
    {
      beforeAll([&response]() -> void
      {
        execute_command_with_output(
            zo_command + " uss create-dir " + ussTestDir + "/subDir1/subDir2/subDir3" + " --mode 777", response);
        execute_command_with_output(zo_command + " uss create-file " + ussTestDir + "/subDir1/subFile1" + " --mode 777",
                                    response);
        execute_command_with_output(
            zo_command + " uss create-file " + ussTestDir + "/subDir1/subDir2/subFile2" + " --mode 777", response);
        execute_command_with_output(zo_command + " uss create-file " + ussTestDir +
                                        "/subDir1/subDir2/subDir3/subFile3" + " --mode 777",
                                    response);
      });
      it("should properly list files", [&]() -> void
      {
        std::string listCommand = zo_command + " uss ls " + ussTestDir;
        rc = execute_command_with_output(listCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("subDir1");
      });
      it("should properly list files with the --all option", [&]() -> void
      {
        std::string listAllCommand = zo_command + " uss ls " + ussTestDir + " --all";
        rc = execute_command_with_output(listAllCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(".");
        Expect(response).ToContain("..");
        Expect(response).ToContain("subDir1");
      });
      it("should properly list files with the --depth option set to 1 and should match the output of no depth option specified",
         [&]() -> void
      {
        std::string testResponse;
        std::string listCommand = zo_command + " uss ls " + ussTestDir;
        std::string listDepthCommand = zo_command + " uss ls " + ussTestDir + " --depth 1";
        rc = execute_command_with_output(listDepthCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("subDir1");
        execute_command_with_output(listCommand, testResponse);
        Expect(response).ToBe(testResponse);
      });
      it("should properly list files with the --depth option set to 2", [&]() -> void
      {
        std::string listDepthCommand = zo_command + " uss ls " + ussTestDir + " --depth 2";
        rc = execute_command_with_output(listDepthCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("subDir1/subDir2");
        Expect(response).ToContain("subDir1/subFile1");
      });
      it("should properly list files with the --depth option set to 3", [&]() -> void
      {
        std::string listDepthCommand = zo_command + " uss ls " + ussTestDir + " --depth 3";
        rc = execute_command_with_output(listDepthCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("subDir1/subDir2");
        Expect(response).ToContain("subDir1/subDir2/subDir3");
        Expect(response).ToContain("subDir1/subDir2/subFile2");
        Expect(response).ToContain("subDir1/subFile1");
      });
      it("should properly list files with --response-format-csv", [&]() -> void
      {
        std::string listDepthCommand = zo_command + " uss ls " + ussTestDir + " -l --rfc";
        rc = execute_command_with_output(listDepthCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("drwxrwxrwx,");
        Expect(response).ToContain(",subDir1");
      });
      it("should properly handle file names with commas with --response-format-csv", [&]() -> void
      {
        std::string commaFile = ussTestDir + "/subDir1/comma,file,test.txt";
        execute_command_with_output(zo_command + " uss create-file " + commaFile, response);

        std::string listDepthCommand = zo_command + " uss ls " + ussTestDir + "/subDir1 -l --rfc";
        rc = execute_command_with_output(listDepthCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("comma,file,test.txt");

        // cleanup
        execute_command_with_output(zo_command + " uss delete " + commaFile, response);
      });
      it("should preserve symlink file type in long listing", [&]() -> void
      {
        std::string linkTargetDir = get_random_uss(ussTestDir) + "_target";
        std::string linkPath = get_random_uss(ussTestDir) + "_link";

        create_test_dir_cmd(linkTargetDir, "--mode 777");
        rc = symlink(linkTargetDir.c_str(), linkPath.c_str());
        ExpectWithContext(rc, "Failed to create symlink").ToBe(0);

        std::string listCommand = zo_command + " uss ls " + ussTestDir + " -l";
        rc = execute_command_with_output(listCommand, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("lrwx");
        Expect(response).ToContain(linkPath.substr(linkPath.find_last_of("/") + 1));

        unlink(linkPath.c_str());
        execute_command_with_output(zo_command + " uss delete " + linkTargetDir + " --recursive", response);
      });
      it("should properly handle missing options", [&]() -> void
      {
        std::string incompleteCommand = zo_command + " uss ls";
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(incompleteCommand, response);
        }
        ExpectWithContext(rc, response).ToBe(1);
        Expect(response).ToContain("missing required positional argument: file-path");
      });
      it("should properly handle listing a path that does not exist", [&]() -> void
      {
        std::string incompleteCommand = zo_command + " uss ls /does/not/exist";
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(incompleteCommand, response);
        }
        ExpectWithContext(rc, response).ToBe(255);
        Expect(response).ToContain("Path '/does/not/exist' does not exist");
      });
    });
    describe("issue command", [&]() -> void
    {
      it("should issue whoami and return a non-empty user ID", [&]() -> void
      {
        std::string command = zo_command + " uss issue whoami";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response.size()).ToBeGreaterThan(0);
      });

      it("should issue echo and return the expected output", [&]() -> void
      {
        std::string command = zo_command + " uss issue \"echo hello\"";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("hello");
      });

      it("should return non-zero for an invalid command", [&]() -> void
      {
        std::string command = zo_command + " uss issue \"command_that_does_not_exist_xyz\"";
        {
          test_utils::ErrorStreamCapture c;
          rc = execute_command_with_output(command, response);
        }
        Expect(rc).Not().ToBe(0);
      });
    });
  });
}