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
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <pwd.h>
#include <grp.h>
#include <ctime>

#include "ztest.hpp"
#include "../zusf.hpp"
#include "ztype.h"
#include "zutils.hpp"

using namespace ztst;

void zusf_tests()
{
  describe("zusf_copy_uss_file_or_dir tests",
           [&]() -> void
  {
    ZUSF zusf{};
    ListOptions short_list_opts{false, false, 1};
    ListOptions long_list_opts{false, true, 1};
    ListOptions all_long_list_opts{true, true, 1};
    const CopyOptions copts_preserve(false, false, true, false);
    const CopyOptions copts_recurse_symlink_preserve(true, true, true, false);
    const CopyOptions copts_all_off(false, false, false, false);
    const CopyOptions copts_recurse_preserve(true, false, true, false);
    const CopyOptions copts_force(false, false, false, true);
    const CopyOptions copts_recursive(true, false, false, false);
    const CopyOptions copts_recursive_force(true, false, false, true);
    const std::string tmp_base = "/tmp/zusf_copy_tests_" + get_random_string(10);
    std::string file_a;
    std::string file_b;
    std::string dir_a;
    std::string dir_b;

    beforeEach([&]() -> void
    {
      zusf = {};
      std::string discard;
      execute_command_with_output("rm -rf " + tmp_base, discard);
      mkdir(tmp_base.c_str(), 0755);
      file_a = tmp_base + "/test_file_a";
      file_b = tmp_base + "/test_file_b";
      dir_a = tmp_base + "/test_dir_a";
      dir_b = tmp_base + "/test_dir_b";
    });

    afterAll([tmp_base]() -> void
    {
      std::string discard;
      execute_command_with_output("rm -rf " + tmp_base, discard);
    });

    it("file->file tests", [&]() -> void
    {
      const std::string source_file = file_a;
      const std::string dest_file = file_b;
      std::string list_response;
      zusf_create_uss_file_or_dir(&zusf, source_file, 0664, CreateOptions());
      zusf_create_uss_file_or_dir(&zusf, dest_file, 0775, CreateOptions(true));
      int rc;
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_file, copts_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);
      // copy over an existing file
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_file, copts_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);
      // copy over an existing file with -RL
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_file, copts_recurse_symlink_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);

      // copy a file to itself
      rc = zusf_copy_file_or_dir(&zusf, source_file, source_file, copts_preserve);
      Expect(rc).ToBe(-1);

      // with and without -p
      zusf_chmod_uss_file_or_dir(&zusf, source_file, 0777, false);
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_file, copts_all_off);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rw-rw-r--"); // NOT 777

      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_file, copts_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rwxrwxrwx"); // keeps 777
    });

    it("file->dir tests", [&]() -> void
    {
      const std::string source_file = file_a;
      const std::string dest_dir = dir_b;
      std::string list_response;
      const std::string dest_copied_file = dest_dir + "/" + get_basename(source_file);
      zusf_create_uss_file_or_dir(&zusf, source_file, 0664, CreateOptions());
      zusf_create_uss_file_or_dir(&zusf, dest_dir, 0775, CreateOptions(true));
      int rc;
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_preserve);
      Expect(rc).ToBe(0);

      // overwrite
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_copied_file, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);
      // overwrite with -RL
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_recurse_symlink_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_copied_file, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);

      rc = zusf_copy_file_or_dir(&zusf, source_file, "/some/dir/doesnt/exist", copts_preserve);
      Expect(rc).ToBe(-1);

      // with and without -p. dir is 0775 and file is 0664
      // file already exists from last tests: replacement does not change permissions
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_all_off);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_copied_file, list_response, long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rw-rw-r--"); // 0644

      // after removing, we should see 0644 (file attributes not preserved)
      rc = zusf_delete_uss_item(&zusf, dest_copied_file, false);
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_all_off);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_copied_file, list_response, long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rw-r--r--"); // 0644

      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_copied_file, list_response, long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rw-rw-r--"); // keeps file's 0664
    });

    // Not allowed - all errors
    it("dir->file tests", [&]() -> void
    {
      const std::string source_dir = dir_a;
      const std::string dest_file = file_b;
      std::string list_response;
      zusf_create_uss_file_or_dir(&zusf, source_dir, 0775, CreateOptions(true));
      zusf_create_uss_file_or_dir(&zusf, dest_file, 0664, CreateOptions());
      int rc;

      // copy to an existing file: error and no change to file/dir
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_file, copts_preserve);
      Expect(rc).ToBe(-1);
      rc = zusf_list_uss_file_path(&zusf, dest_file, list_response, all_long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("-rw-rw-r--");
      rc = zusf_list_uss_file_path(&zusf, source_dir, list_response, all_long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("drwxrwxr-x");

      // copy to an existing file with -RL, also fail
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_file, copts_recurse_preserve);
      Expect(rc).ToBe(-1);
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_file, copts_recurse_symlink_preserve);
      Expect(rc).ToBe(-1);

      const std::string unused_path = file_a;
      // copy to a non-existent file or dir (creates it)
      rc = zusf_copy_file_or_dir(&zusf, source_dir, unused_path, copts_preserve);
      Expect(rc).ToBe(-1); // missing -R

      rc = zusf_copy_file_or_dir(&zusf, source_dir, unused_path, copts_recurse_symlink_preserve);
      Expect(rc).ToBe(0); // now exists
      rc = zusf_list_uss_file_path(&zusf, unused_path, list_response, all_long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("drwxrwxr-x");
    });

    it("dir->dir tests", [&]() -> void
    {
      const std::string source_dir = dir_a;
      const std::string source_nested = dir_a + "/some/nested/directories";
      const std::string dest_dir = dir_b;
      const std::string dest_nested = dir_b + "/some/nested/directories";
      int rc;
      std::string list_response;

      zusf_create_uss_file_or_dir(&zusf, source_dir, 0775, CreateOptions(true));

      // bad source
      rc = zusf_copy_file_or_dir(&zusf, "/noway/src/noexist", dest_dir, copts_preserve);
      Expect(rc).ToBe(-1);
      // bad target
      rc = zusf_copy_file_or_dir(&zusf, source_dir, "/noway/dest/noexist", copts_preserve);
      Expect(rc).ToBe(-1);

      // missing -R
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_dir, copts_preserve);
      Expect(rc).ToBe(-1);
      rc = zusf_list_uss_file_path(&zusf, dest_dir, list_response, short_list_opts, false);
      Expect(rc).ToBe(-1);

      // with -R
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_dir, copts_recurse_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_dir, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);
      zusf_delete_uss_item(&zusf, dest_dir, true);

      // nested dirs and symlinks
      const std::string symlink_nested_dir = tmp_base + "/find/me/with/symlink";
      const std::string symlink_target = tmp_base + "/find";
      const std::string source_symlink_filepath = source_dir + "/find";
      const std::string dest_link_filepath = dest_dir + "/find";
      const std::string dest_symlink_nested_dir = dest_dir + "/find/me/with/symlink";

      zusf_create_uss_file_or_dir(&zusf, source_nested, 0775, CreateOptions(true));
      zusf_create_uss_file_or_dir(&zusf, symlink_nested_dir, 0775, CreateOptions(true));

      // symlink
      std::string cmd_output;
      execute_command_with_output("ln -s " + symlink_target + " " + source_symlink_filepath, cmd_output);

      // link is copied
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_dir, copts_recurse_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_symlink_nested_dir, list_response, short_list_opts, false);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, dest_dir, list_response, all_long_list_opts, false);
      Expect(rc).ToBe(0);
      // chmod hack won't work here, can't modify symlink permissions
      // Expect(list_response).ToContain("lrwxr-xr-x"); //TODO: https://github.com/zowe/zowex/issues/791
      // rc = zusf_delete_uss_item(&zusf, dest_dir, true);
      // Expect(rc).ToBe(-1); // TODO: https://github.com/zowe/zowex/issues/792

      execute_command_with_output("rm -rf " + dest_dir, cmd_output);

      // data is copied (--follow-symlinks)
      rc = zusf_copy_file_or_dir(&zusf, source_dir, dest_dir, copts_recurse_symlink_preserve);
      Expect(rc).ToBe(0);
      rc = zusf_list_uss_file_path(&zusf, symlink_target, list_response, short_list_opts, true);
      Expect(rc).ToBe(0);
      zusf_chmod_uss_file_or_dir(&zusf, dest_link_filepath, 0755, false); // hack: make this 755 for Expect() check
      rc = zusf_list_uss_file_path(&zusf, dest_dir, list_response, all_long_list_opts, false);
      Expect(rc).ToBe(0);
      Expect(list_response).ToContain("drwxr-xr-x"); // it should be a dir
    });
    it("should not copy to/from fifo pipes", [&]() -> void
    {
      const std::string pipe = file_a;
      const std::string non_pipe = file_b;
      std::string dispose;
      execute_command_with_output("mkfifo -m 0666 " + pipe, dispose);
      ExpectWithContext(zusf_copy_file_or_dir(&zusf, pipe, non_pipe, copts_all_off), zusf.diag.e_msg).ToBe(-1);
      ExpectWithContext(zusf_copy_file_or_dir(&zusf, non_pipe, pipe, copts_all_off), zusf.diag.e_msg).ToBe(-1);
    });

    it("force copy tests", [&]() -> void
    {
      const std::string source_file = file_a;
      const std::string target_file = file_b;

      const std::string source_dir = dir_a;
      const std::string target_dir = dir_b;

      ExpectWithContext(zusf_create_uss_file_or_dir(&zusf, source_file, 0664, CreateOptions()), zusf.diag.e_msg)
          .ToBe(0);
      ExpectWithContext(zusf_create_uss_file_or_dir(&zusf, target_file, 0400, CreateOptions()), zusf.diag.e_msg)
          .ToBe(0);
      ExpectWithContext(zusf_create_uss_file_or_dir(&zusf, source_dir, 0664, CreateOptions(true)), zusf.diag.e_msg)
          .ToBe(0);
      ExpectWithContext(zusf_create_uss_file_or_dir(&zusf, target_dir, 0775, CreateOptions(true)), zusf.diag.e_msg)
          .ToBe(0);
      ExpectWithContext(
          zusf_create_uss_file_or_dir(&zusf, target_dir + "/" + get_basename(source_dir), 0400, CreateOptions(true)),
          zusf.diag.e_msg)
          .ToBe(0);

      // can't overwrite 0400 target without force
      zusf_chmod_uss_file_or_dir(&zusf, target_file, 0400, false);
      ExpectWithContext(zusf_copy_file_or_dir(&zusf, source_file, target_file, copts_all_off), zusf.diag.e_msg)
          .ToBe(-1);
      ExpectWithContext(zusf_copy_file_or_dir(&zusf, source_file, target_file, copts_force), zusf.diag.e_msg).ToBe(0);
    });

    it("insufficient permissions tests", [&]() -> void
    {
      const std::string source_file = file_a;
      const std::string dest_dir = dir_b;

      zusf_create_uss_file_or_dir(&zusf, source_file, 0664, CreateOptions());
      zusf_create_uss_file_or_dir(&zusf, dest_dir, 0400,
                                  CreateOptions(true)); // TODO: this does not set permissions to 0400. why? `zo uss
                                                        // create-dir test_dir --mode 0400` works!
      // chmod must succeed for copy to fail
      ExpectWithContext(zusf_chmod_uss_file_or_dir(&zusf, dest_dir, 0400, true), zusf.diag.e_msg).ToBe(0);
      int rc;
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_preserve);
      ExpectWithContext(rc, zusf.diag.e_msg).ToBe(-1);

      zusf_chmod_uss_file_or_dir(&zusf, dest_dir, 0775, true);
      rc = zusf_copy_file_or_dir(&zusf, source_file, dest_dir, copts_preserve);
      Expect(rc).ToBe(0);
    });
  }

  );
  describe("zusf_chown_uss_file_or_dir tests", [&]() -> void
  {
    ZUSF zusf{};

    const std::string tmp_base = "/tmp/zusf_chown_tests_" + get_random_string(10);
    const std::string file_path = tmp_base + "/one.txt";
    const std::string dir_path = tmp_base + "/tree";
    const std::string dir_a = dir_path + "/subA";
    const std::string dir_b = dir_path + "/subA/subB";
    const std::string f_top = dir_path + "/file_top.txt";
    const std::string f_mid = dir_a + "/file_mid.txt";
    const std::string f_bot = dir_b + "/file_bottom.txt";

    // Ensure clean slate
    zusf_delete_uss_item(&zusf, tmp_base, true);
    mkdir(tmp_base.c_str(), 0755);

    // Discover current primary group (gid and name)
    gid_t primary_gid = getgid();
    struct group *gr = getgrgid(primary_gid);
    const char *primary_group_name = (gr && gr->gr_name) ? gr->gr_name : nullptr;

    it("should fail when path does not exist", [&]() -> void
    {
      std::string not_there = tmp_base + "/does_not_exist.txt";
      int rc = zusf_chown_uss_file_or_dir(&zusf, not_there, ":somegroup", false);
      Expect(rc).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("does not exist");
    });

    it("should fail for invalid user name", [&]() -> void
    {
      // Create a real file to exercise the user validation path
      {
        std::ofstream f(file_path);
        f << "x";
        f.close();
      }
      int rc = zusf_chown_uss_file_or_dir(&zusf, file_path, "nosuchuser_xyz", false);
      Expect(rc).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("invalid user 'nosuchuser_xyz'");
      unlink(file_path.c_str());
    });

    it("should fail for invalid group name", [&]() -> void
    {
      {
        std::ofstream f(file_path);
        f << "x";
        f.close();
      }
      int rc = zusf_chown_uss_file_or_dir(&zusf, file_path, ":nosuchgroup_xyz", false);
      Expect(rc).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("invalid group 'nosuchgroup_xyz'");
      unlink(file_path.c_str());
    });

    it("should fail for no-op guard ':' (no user or group specified, empty)", [&]() -> void
    {
      {
        std::ofstream f(file_path);
        f << "x";
        f.close();
      }
      int rc = zusf_chown_uss_file_or_dir(&zusf, file_path, ":", false);
      Expect(rc).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("neither user nor group specified");
      unlink(file_path.c_str());
    });

    it("should fail on directory without --recursive", [&]() -> void
    {
      mkdir(dir_path.c_str(), 0755);
      int rc = zusf_chown_uss_file_or_dir(&zusf, dir_path, ":somegroup", false);
      Expect(rc).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("is a folder and recursive is false");
      rmdir(dir_path.c_str());
    });

    it("should chown group-only on a single file when caller owns it", [&]() -> void
    {
      if (!primary_group_name)
      {
        // If we cannot resolve a group name, skip meaningfully
        std::cout << "[SKIP] primary group name not available\n";
        return;
      }

      {
        std::ofstream f(file_path);
        f << "hello";
      }

      std::string owner_str = std::string(":") + primary_group_name;
      int rc = zusf_chown_uss_file_or_dir(&zusf, file_path, owner_str, false);
      Expect(rc).ToBe(RTNCD_SUCCESS);

      struct stat st{};
      Expect(stat(file_path.c_str(), &st)).ToBe(0);
      Expect((int)st.st_gid).ToBe((int)primary_gid);

      unlink(file_path.c_str());
    });

    it("should chown group-only recursively over a directory tree", [&]() -> void
    {
      if (!primary_group_name)
      {
        std::cout << "[SKIP] primary group name not available\n";
        return;
      }

      // project_dir/subdir1/subdir2 with three files
      mkdir(dir_path.c_str(), 0755);
      mkdir(dir_a.c_str(), 0755);
      mkdir(dir_b.c_str(), 0755);
      {
        std::ofstream f(f_top);
        f << "first";
      }
      {
        std::ofstream f(f_mid);
        f << "second";
      }
      {
        std::ofstream f(f_bot);
        f << "third";
      }

      std::string owner_str = std::string(":") + primary_group_name;
      int rc = zusf_chown_uss_file_or_dir(&zusf, dir_path, owner_str, true);
      Expect(rc).ToBe(RTNCD_SUCCESS);

      // Verify all dirs and files now have primary_gid
      auto expect_gid = [&](const std::string &p)
      {
        struct stat st{};
        Expect(stat(p.c_str(), &st)).ToBe(0);
        Expect((int)st.st_gid).ToBe((int)primary_gid);
      };
      expect_gid(dir_path);
      expect_gid(dir_a);
      expect_gid(dir_b);
      expect_gid(f_top);
      expect_gid(f_mid);
      expect_gid(f_bot);

      // Cleanup
      unlink(f_bot.c_str());
      unlink(f_mid.c_str());
      unlink(f_top.c_str());
      rmdir(dir_b.c_str());
      rmdir(dir_a.c_str());
      rmdir(dir_path.c_str());
    });

    // Final cleanup
    rmdir(tmp_base.c_str());
  });

  describe("zusf_chmod_uss_file_or_dir tests", [&]() -> void
  {
    ZUSF zusf{};
    const std::string test_file = "/tmp/zusf_test_file.txt";
    const std::string test_dir = "/tmp/zusf_test_dir";
    const std::string nonexistent_file = "/tmp/nonexistent_file.txt";

    afterEach([&]() -> void
    {
      std::string inner_file = test_dir + "/inner_file.txt";
      unlink(inner_file.c_str());
      unlink(test_file.c_str());
      rmdir(test_dir.c_str());
      unlink(nonexistent_file.c_str());
    });

    it("should fail when file does not exist", [&]() -> void
    {
      // Ensure the file doesn't exist
      unlink(nonexistent_file.c_str());

      int result = zusf_chmod_uss_file_or_dir(&zusf, nonexistent_file, 0644, false);
      Expect(result).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToBe("Path '/tmp/nonexistent_file.txt' does not exist");
    });

    it("should fail when trying to chmod directory without recursive flag", [&]() -> void
    {
      // Create test directory
      mkdir(test_dir.c_str(), 0755);

      int result = zusf_chmod_uss_file_or_dir(&zusf, test_dir, 0644, false);
      Expect(result).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToBe("Path '/tmp/zusf_test_dir' is a folder and recursive is false");
    });

    it("should successfully chmod a regular file", [&]() -> void
    {
      // Create test file
      std::ofstream file(test_file);
      file << "test content";
      file.close();

      // Set initial permissions
      chmod(test_file.c_str(), 0600);

      // Test chmod function
      int result = zusf_chmod_uss_file_or_dir(&zusf, test_file, 0644, false);
      Expect(result).ToBe(RTNCD_SUCCESS);

      // Verify permissions were changed
      struct stat file_stat;
      stat(test_file.c_str(), &file_stat);
      mode_t actual_mode = file_stat.st_mode & 0777; // Mask to get only permission bits
      Expect((int)actual_mode).ToBe(0644);
    });

    it("should successfully chmod a directory with recursive flag", [&]() -> void
    {
      // Create test directory structure
      mkdir(test_dir.c_str(), 0700);

      // Create a file inside the directory to test recursive behavior
      std::string inner_file = test_dir + "/inner_file.txt";
      std::ofstream file(inner_file);
      file << "inner content";
      file.close();
      chmod(inner_file.c_str(), 0600);

      // Test chmod function with recursive flag
      int result = zusf_chmod_uss_file_or_dir(&zusf, test_dir, 0755, true);
      Expect(result).ToBe(RTNCD_SUCCESS);

      // Verify directory permissions were changed
      struct stat dir_stat;
      stat(test_dir.c_str(), &dir_stat);
      mode_t actual_dir_mode = dir_stat.st_mode & 0777;
      Expect((int)actual_dir_mode).ToBe(0755);

      // Verify file permissions were also changed (recursive)
      struct stat file_stat;
      stat(inner_file.c_str(), &file_stat);
      mode_t actual_file_mode = file_stat.st_mode & 0777;
      Expect((int)actual_file_mode).ToBe(0755);
    });

    it("should handle different permission modes correctly", [&]() -> void
    {
      // Create test file
      std::ofstream file(test_file);
      file << "test content";
      file.close();

      // Test different permission modes
      mode_t test_modes[] = {0600, 0644, 0755, 0777};

      for (mode_t mode : test_modes)
      {
        int result = zusf_chmod_uss_file_or_dir(&zusf, test_file, mode, false);
        Expect(result).ToBe(RTNCD_SUCCESS);

        struct stat file_stat;
        stat(test_file.c_str(), &file_stat);
        mode_t actual_mode = file_stat.st_mode & 0777;
        Expect((int)actual_mode).ToBe((int)mode);
      }
    });
  });

  describe("zusf_get_file_ccsid tests", [&]() -> void
  {
    ZUSF zusf{};
    const std::string test_file = "/tmp/zusf_ccsid_test_file.txt";
    const std::string test_dir = "/tmp/zusf_ccsid_test_dir";
    const std::string nonexistent_file = "/tmp/nonexistent_ccsid_file.txt";

    // --- Hooks ---

    afterEach([&]() -> void
    {
      unlink(test_file.c_str());
      rmdir(test_dir.c_str());
      unlink(nonexistent_file.c_str());
    });

    // --- Tests ---
    it("should fail when file does not exist", [&]() -> void
    {
      // Ensure the file doesn't exist
      unlink(nonexistent_file.c_str());

      int result = zusf_get_file_ccsid(&zusf, nonexistent_file);
      Expect(result).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToBe("Path '/tmp/nonexistent_ccsid_file.txt' does not exist");
    });

    it("should fail when path is a directory", [&]() -> void
    {
      // Create test directory
      mkdir(test_dir.c_str(), 0755);

      int result = zusf_get_file_ccsid(&zusf, test_dir);
      Expect(result).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToBe("Path '/tmp/zusf_ccsid_test_dir' is a directory");
    });

    it("should return CCSID for a regular file", [&]() -> void
    {
      // Create test file
      std::ofstream file(test_file);
      file << "test content";
      file.close();

      int result = zusf_get_file_ccsid(&zusf, test_file);
      // The result should be a valid CCSID (non-negative) or 0 for untagged
      Expect(result).ToBeGreaterThanOrEqualTo(0);
    });
  });

  describe("zusf_get_ccsid_display_name tests", [&]() -> void
  {
    it("should return 'untagged' for CCSID 0", [&]() -> void
    {
      std::string result = zusf_get_ccsid_display_name(0);
      Expect(result).ToBe("untagged");
    });

    it("should return 'untagged' for negative CCSID", [&]() -> void
    {
      std::string result = zusf_get_ccsid_display_name(-1);
      Expect(result).ToBe("untagged");
    });

    it("should return 'binary' for CCSID 65535", [&]() -> void
    {
      std::string result = zusf_get_ccsid_display_name(65535);
      Expect(result).ToBe("binary");
    });

    it("should return known CCSID display names", [&]() -> void
    {
      // Test some well-known CCSIDs
      std::string result37 = zusf_get_ccsid_display_name(37);
      Expect(result37).ToBe("IBM-037");

      std::string result819 = zusf_get_ccsid_display_name(819);
      Expect(result819).ToBe("ISO8859-1");

      std::string result1047 = zusf_get_ccsid_display_name(1047);
      Expect(result1047).ToBe("IBM-1047");
    });

    it("should return CCSID number as string for unknown CCSIDs", [&]() -> void
    {
      // Test with an unknown CCSID
      std::string result = zusf_get_ccsid_display_name(99999);
      Expect(result).ToBe("99999");

      // Test with another unknown CCSID
      std::string result2 = zusf_get_ccsid_display_name(12345);
      Expect(result2).ToBe("12345");
    });

    it("should handle unrealistic inputs", [&]() -> void
    {
      // We don't support these inputs, but we should handle them gracefully
      // and leave it up to the caller to decide what to do with them.

      // Test maximum positive CCSID
      std::string result = zusf_get_ccsid_display_name(2147483647); // INT_MAX
      Expect(result).ToBe("2147483647");

      // Test very negative CCSID
      std::string result2 = zusf_get_ccsid_display_name(-999);
      Expect(result2).ToBe("untagged");
    });
  });

  describe("zusf_format_ls_time tests", [&]() -> void
  {
    it("should format time in ls-style format by default", [&]() -> void
    {
      // Use a known timestamp: January 1, 2024 12:00:00 UTC
      time_t test_time = 1704110400; // 2024-01-01 12:00:00 UTC

      std::string result = zusf_format_ls_time(test_time, false);

      // Should be in format like "Jan  1 12:00" (but will be in local time)
      // We'll just check that it contains expected elements
      Expect(result.length()).ToBeGreaterThan(0);
      Expect(result.length()).ToBeLessThan(20); // Should be reasonable length
    });

    it("should format time in CSV/ISO format when requested", [&]() -> void
    {
      // Use a known timestamp: January 1, 2024 12:00:00 UTC
      time_t test_time = 1704110400; // 2024-01-01 12:00:00 UTC

      std::string result = zusf_format_ls_time(test_time, true);

      // Should be in format "2024-01-01T12:00:00"
      Expect(result).ToBe("2024-01-01T12:00:00");
    });

    it("should handle zero timestamp in CSV format", [&]() -> void
    {
      time_t test_time = 0; // Unix epoch

      std::string result = zusf_format_ls_time(test_time, true);

      // Should be in format "1970-01-01T00:00:00"
      Expect(result).ToBe("1970-01-01T00:00:00");
    });

    it("should handle negative timestamp gracefully", [&]() -> void
    {
      time_t test_time = -1; // Invalid time

      std::string result = zusf_format_ls_time(test_time, true);

      // Should fallback to epoch time
      Expect(result).ToBe("1970-01-01T00:00:00");
    });

    it("should handle very large timestamp", [&]() -> void
    {
      // Use year 2038 (close to 32-bit time_t limit)
      time_t test_time = 2147483647; // 2038-01-19 03:14:07 UTC

      std::string result = zusf_format_ls_time(test_time, true);

      // Should format correctly
      Expect(result).ToBe("2038-01-19T03:14:07");
    });
  });

  describe("zusf_get_owner_from_uid tests", [&]() -> void
  {
    it("should return username for current user UID", [&]() -> void
    {
      uid_t current_uid = getuid();
      std::string result = zusf_get_owner_from_uid(current_uid);

      // Should return a valid username (not null)
      Expect(result).Not().ToBe("");

      // Should match what getpwuid returns
      struct passwd *pwd = getpwuid(current_uid);
      if (pwd != nullptr)
      {
        Expect(std::string(result)).ToBe(std::string(pwd->pw_name));
      }
    });

    it("should return null for invalid UID", [&]() -> void
    {
      // Use a UID that's very unlikely to exist
      uid_t invalid_uid = 99999;
      std::string result = zusf_get_owner_from_uid(invalid_uid);

      // Should return null for non-existent UID
      Expect(result).ToBe("");
    });
  });

  describe("zusf_get_group_from_gid tests", [&]() -> void
  {
    it("should return group name for current user GID", [&]() -> void
    {
      gid_t current_gid = getgid();
      std::string result = zusf_get_group_from_gid(current_gid);

      // Should return a valid group name (not null)
      Expect(result).Not().ToBe("");

      // Should match what getgrgid returns
      struct group *grp = getgrgid(current_gid);
      if (grp != nullptr)
      {
        Expect(std::string(result)).ToBe(std::string(grp->gr_name));
      }
    });

    it("should return null for invalid GID", [&]() -> void
    {
      // Use a GID that's very unlikely to exist
      gid_t invalid_gid = 99999;
      std::string result = zusf_get_group_from_gid(invalid_gid);

      // Should return null for non-existent GID
      Expect(result).ToBe("");
    });

    it("should handle root GID (0)", [&]() -> void
    {
      gid_t root_gid = 0;
      std::string result = zusf_get_group_from_gid(root_gid);

      // May or may not exist depending on system, but should handle gracefully
      // If it exists, commonly "root" or "wheel"
      if (result != "")
      {
        Expect(result.length()).ToBeGreaterThan(0);
      }
    });
  });

  describe("zusf_format_file_entry tests", [&]() -> void
  {
    ZUSF zusf{};
    const std::string test_file = "/tmp/zusf_format_test_file.txt";
    const std::string test_dir = "/tmp/zusf_format_test_dir";

    afterEach([&]() -> void
    {
      unlink(test_file.c_str());
      rmdir(test_dir.c_str());
    });

    it("should format short listing for regular file", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content";
      file.close();

      struct stat file_stats;
      stat(test_file.c_str(), &file_stats);

      ListOptions options{false, false, 1};
      ; // not all files, not long format, no recursion
      std::string result = zusf_format_file_entry(&zusf, file_stats, test_file, "testfile.txt", options, false);

      // Short format should just be filename + newline
      Expect(result).ToBe("testfile.txt\n");
    });

    it("should format long listing for regular file in ls-style", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content";
      file.close();
      chmod(test_file.c_str(), 0644);

      struct stat file_stats;
      stat(test_file.c_str(), &file_stats);

      ListOptions options{false, true, 1};
      ; // not all files, long format, no recursion
      std::string result = zusf_format_file_entry(&zusf, file_stats, test_file, "testfile.txt", options, false);

      // Long format should contain permissions, size, time, filename
      Expect(result).ToContain("-rw-r--r--");   // permissions
      Expect(result).ToContain("testfile.txt"); // filename
      Expect(result).ToContain("12");           // file size (test content = 12 bytes)
      Expect(result.back()).ToBe('\n');         // should end with newline
    });

    it("should format long listing for regular file in CSV format", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content";
      file.close();
      chmod(test_file.c_str(), 0644);

      struct stat file_stats;
      stat(test_file.c_str(), &file_stats);

      ListOptions options{false, true, 1};
      ; // not all files, long format, no recursion
      std::string result = zusf_format_file_entry(&zusf, file_stats, test_file, "testfile.txt", options, true);

      // CSV format should have comma-separated values
      Expect(result).ToContain(",");            // should have commas
      Expect(result).ToContain("-rw-r--r--");   // permissions
      Expect(result).ToContain("testfile.txt"); // filename
      Expect(result).ToContain("12");           // file size
      Expect(result.back()).ToBe('\n');         // should end with newline

      // Should contain exactly 7 commas (8 fields total)
      int comma_count = 0;
      for (char c : result)
      {
        if (c == ',')
          comma_count++;
      }
      Expect(comma_count).ToBe(7);
    });

    it("should format directory entry correctly", [&]() -> void
    {
      // Create test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      struct stat dir_stats;
      stat(test_dir.c_str(), &dir_stats);

      ListOptions options{false, true, 1};
      ; // not all files, long format, no recursion
      std::string result = zusf_format_file_entry(&zusf, dir_stats, test_dir, "testdir", options, false);

      // Directory should start with 'd'
      Expect(result).ToContain("drwxr-xr-x"); // directory permissions
      Expect(result).ToContain("testdir");    // directory name
    });

    it("should handle files with different CCSID tags", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content";
      file.close();

      struct stat file_stats;
      stat(test_file.c_str(), &file_stats);

      ListOptions options{false, true, 1};
      ; // long format, no recursion
      std::string result = zusf_format_file_entry(&zusf, file_stats, test_file, "testfile.txt", options, false);

      // Should contain CCSID information (could be "untagged" or a specific CCSID)
      bool has_ccsid_info = result.find("untagged") != std::string::npos || result.find("IBM-") != std::string::npos ||
                            result.find("UTF-8") != std::string::npos || result.find("binary") != std::string::npos;
      Expect(has_ccsid_info).ToBe(true);
    });
  });

  describe("zusf_list_uss_file_path tests", [&]() -> void
  {
    ZUSF zusf{};
    const std::string test_file = "/tmp/zusf_list_test_file.txt";
    const std::string test_dir = "/tmp/zusf_list_test_dir";
    const std::string nonexistent_path = "/tmp/nonexistent_path_for_list";

    it("should fail for nonexistent path", [&]() -> void
    {
      std::string response;
      ListOptions options{false, false, 1};
      ; // no recursion

      int result = zusf_list_uss_file_path(&zusf, nonexistent_path, response, options, false);

      Expect(result).ToBe(RTNCD_FAILURE);
      Expect(std::string(zusf.diag.e_msg)).ToContain("does not exist");
    });

    it("should list single file successfully", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content for listing";
      file.close();

      std::string response;
      ListOptions options{false, false, 1};
      ; // short format, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_file, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("zusf_list_test_file.txt");

      // Cleanup
      unlink(test_file.c_str());
    });

    it("should list single file in long format", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "test content for long listing";
      file.close();

      std::string response;
      ListOptions options{false, true, 1};
      ; // long format, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_file, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("zusf_list_test_file.txt");
      Expect(response).ToContain("-rw-"); // file permissions

      // Cleanup
      unlink(test_file.c_str());
    });

    it("should list directory contents", [&]() -> void
    {
      // Create test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      // Create files in directory
      std::string file1 = test_dir + "/file1.txt";
      std::string file2 = test_dir + "/file2.txt";
      std::string subdir = test_dir + "/subdir";

      std::ofstream f1(file1);
      f1 << "content1";
      f1.close();

      std::ofstream f2(file2);
      f2 << "content2";
      f2.close();

      mkdir(subdir.c_str(), 0755);

      std::string response;
      ListOptions options{false, false, 1};
      ; // short format, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("file1.txt");
      Expect(response).ToContain("file2.txt");
      Expect(response).ToContain("subdir");

      // Files should be listed in alphabetical order
      size_t pos1 = response.find("file1.txt");
      size_t pos2 = response.find("file2.txt");
      Expect(pos1).ToBeLessThan(pos2);

      // Cleanup
      unlink(file1.c_str());
      unlink(file2.c_str());
      rmdir(subdir.c_str());
      rmdir(test_dir.c_str());
    });

    it("should list directory contents in long format", [&]() -> void
    {
      // Create test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      // Create file in directory
      std::string file1 = test_dir + "/testfile.txt";
      std::ofstream f1(file1);
      f1 << "test content";
      f1.close();

      std::string response;
      ListOptions options{false, true, 1};
      ; // long format, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("testfile.txt");
      Expect(response).ToContain("-rw-"); // file permissions

      // Cleanup
      unlink(file1.c_str());
      rmdir(test_dir.c_str());
    });

    it("should handle hidden files based on all_files option", [&]() -> void
    {
      // Create test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      // Create regular and hidden files
      std::string regular_file = test_dir + "/regular.txt";
      std::string hidden_file = test_dir + "/.hidden.txt";

      std::ofstream f1(regular_file);
      f1 << "regular content";
      f1.close();

      std::ofstream f2(hidden_file);
      f2 << "hidden content";
      f2.close();

      // Test without all_files option
      std::string response1;
      ListOptions options1{false, false, 1}; // no all_files, no recursion
      int result1 = zusf_list_uss_file_path(&zusf, test_dir, response1, options1, false);

      Expect(result1).ToBe(RTNCD_SUCCESS);
      Expect(response1).ToContain("regular.txt");
      Expect(response1).Not().ToContain(".hidden.txt");

      // Test with all_files option
      std::string response2;
      ListOptions options2{true, false, 1}; // with all_files, no recursion
      int result2 = zusf_list_uss_file_path(&zusf, test_dir, response2, options2, false);

      Expect(result2).ToBe(RTNCD_SUCCESS);
      Expect(response2).ToContain("regular.txt");
      Expect(response2).ToContain(".hidden.txt");

      // Cleanup
      unlink(regular_file.c_str());
      unlink(hidden_file.c_str());
      rmdir(test_dir.c_str());
    });

    it("should handle CSV format output", [&]() -> void
    {
      // Create test file
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "csv test content";
      file.close();

      std::string response;
      ListOptions options{false, true, 1};
      ; // long format, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_file, response, options, true); // CSV format

      Expect(result).ToBe(RTNCD_SUCCESS);

      // CSV format should have commas
      Expect(response).ToContain(",");
      Expect(response).ToContain("zusf_list_test_file.txt");

      // Should have proper number of fields (8 fields = 7 commas)
      int comma_count = 0;
      for (char c : response)
      {
        if (c == ',')
          comma_count++;
      }
      Expect(comma_count).ToBe(7);

      // Cleanup
      unlink(test_file.c_str());
    });

    it("should handle empty directory", [&]() -> void
    {
      // Create empty test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      std::string response;
      ListOptions options{false, false, 1};
      ; // no recursion

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToBe(""); // Should be empty for empty directory

      // Cleanup
      rmdir(test_dir.c_str());
    });

    it("should handle directory with only hidden files when all_files is false", [&]() -> void
    {
      // Create test directory
      rmdir(test_dir.c_str());
      mkdir(test_dir.c_str(), 0755);

      // Create only hidden files
      std::string hidden_file = test_dir + "/.hidden1.txt";
      std::ofstream f1(hidden_file);
      f1 << "hidden content";
      f1.close();

      std::string response;
      ListOptions options{false, false, 1};
      ; // no all_files, no recursion

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToBe(""); // Should be empty since no visible files

      // Cleanup
      unlink(hidden_file.c_str());
      rmdir(test_dir.c_str());
    });
  });

  describe("zusf_list_uss_file_path recursive tests", [&]() -> void
  {
    ZUSF zusf{};

    it("should list immediate children only with depth 1", [&]() -> void
    {
      // Create test directory structure
      std::string test_dir = "/tmp/test_depth1_dir";
      std::string sub_dir = test_dir + "/subdir";
      std::string file1 = test_dir + "/file1.txt";
      std::string file2 = sub_dir + "/file2.txt";

      // Cleanup first
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());

      // Create directories
      mkdir(test_dir.c_str(), 0755);
      mkdir(sub_dir.c_str(), 0755);

      // Create files
      std::ofstream f1(file1);
      f1 << "content1";
      f1.close();

      std::ofstream f2(file2);
      f2 << "content2";
      f2.close();

      std::string response;
      ListOptions options{false, false, 1};
      ; // depth 1 = immediate children only

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("file1.txt");
      Expect(response).ToContain("subdir");
      Expect(response).Not().ToContain("subdir/file2.txt"); // Should NOT include recursive content

      // Cleanup
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());
    });

    it("should return directory info when depth is 0 (like 'ls -d')", [&]() -> void
    {
      // Create test directory structure
      std::string test_dir = "/tmp/test_depth0_dir";
      std::string sub_dir = test_dir + "/subdir";
      std::string file1 = test_dir + "/file1.txt";
      std::string file2 = sub_dir + "/file2.txt";

      // Cleanup first
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());

      // Create directories
      mkdir(test_dir.c_str(), 0755);
      mkdir(sub_dir.c_str(), 0755);

      // Create files
      std::ofstream f1(file1);
      f1 << "content1";
      f1.close();

      std::ofstream f2(file2);
      f2 << "content2";
      f2.close();

      std::string response;
      ListOptions options{false, false, 0};
      ; // all_files=false, depth=0 = show directory itself

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).Not().ToBe("");               // Should return directory info, not empty
      Expect(response).ToContain("test_depth0_dir"); // Should contain the directory name
      Expect(response).Not().ToContain("file1.txt"); // Should NOT include actual directory contents
      Expect(response).Not().ToContain("subdir");
      Expect(response).Not().ToContain("subdir/file2.txt");

      // Cleanup
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());
    });

    it("should include one level of subdirectories with depth 2", [&]() -> void
    {
      // Create test directory structure
      std::string test_dir = "/tmp/test_depth2_dir";
      std::string sub_dir = test_dir + "/subdir";
      std::string file1 = test_dir + "/file1.txt";
      std::string file2 = sub_dir + "/file2.txt";

      // Cleanup first
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());

      // Create directories
      mkdir(test_dir.c_str(), 0755);
      mkdir(sub_dir.c_str(), 0755);

      // Create files
      std::ofstream f1(file1);
      f1 << "content1";
      f1.close();

      std::ofstream f2(file2);
      f2 << "content2";
      f2.close();

      std::string response;
      ListOptions options{false, false, 2};
      ; // depth 2 = immediate children + 1 level of subdirs

      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("file1.txt");
      Expect(response).ToContain("subdir");
      Expect(response).ToContain("subdir/file2.txt"); // Should include one level of recursive content

      // Cleanup
      unlink(file2.c_str());
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());
    });

    it("should handle '.' and '..' entries correctly when all_files is set", [&]() -> void
    {
      // Create test directory structure
      std::string test_dir = "/tmp/test_all_files_dir";
      std::string sub_dir = test_dir + "/subdir";
      std::string file1 = test_dir + "/file1.txt";

      // Cleanup first
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());

      // Create directories
      mkdir(test_dir.c_str(), 0755);
      mkdir(sub_dir.c_str(), 0755);

      // Create files
      std::ofstream f1(file1);
      f1 << "content1";
      f1.close();

      std::string response;

      // Test with depth 0 and all_files = true (should behave like 'ls -d')
      ListOptions options_depth0{true, false, 0}; // all_files=true, depth=0
      int result = zusf_list_uss_file_path(&zusf, test_dir, response, options_depth0, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain("test_all_files_dir"); // Should contain the directory name itself
      Expect(response).Not().ToContain(".");            // Should NOT contain current directory entry with depth=0
      Expect(response).Not().ToContain("..");           // Should NOT contain parent directory entry with depth=0
      Expect(response).Not().ToContain("file1.txt");    // Should NOT include directory contents with depth 0
      Expect(response).Not().ToContain("subdir");

      // Test with depth 1 and all_files = true
      response.clear();
      ListOptions options_depth1{true, false, 1}; // all_files=true, depth=1
      result = zusf_list_uss_file_path(&zusf, test_dir, response, options_depth1, false);

      Expect(result).ToBe(RTNCD_SUCCESS);
      Expect(response).ToContain(".");         // Should contain current directory entry
      Expect(response).ToContain("..");        // Should contain parent directory entry
      Expect(response).ToContain("file1.txt"); // Should include directory contents
      Expect(response).ToContain("subdir");

      // Cleanup
      unlink(file1.c_str());
      rmdir(sub_dir.c_str());
      rmdir(test_dir.c_str());
    });
  });

  describe("zusf source encoding tests", [&]() -> void
  {
    ZUSF zusf{};

    it("should use default source encoding (UTF-8) when not specified", [&]() -> void
    {
      // Set target encoding but no source encoding
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      zusf.encoding_opts.data_type = eDataTypeText;
      // source_codepage should be empty/null

      // The encoding conversion logic should use UTF-8 as source when source_codepage is empty
      Expect(std::strlen(zusf.encoding_opts.source_codepage)).ToBe(0);
      Expect(std::strlen(zusf.encoding_opts.codepage)).ToBe(8); // "IBM-1047"
    });

    it("should use specified source encoding when provided", [&]() -> void
    {
      // Set both target and source encoding
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      strcpy(zusf.encoding_opts.source_codepage, "IBM-037");
      zusf.encoding_opts.data_type = eDataTypeText;

      // Verify both encodings are set correctly
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("IBM-1047");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("IBM-037");
      Expect(zusf.encoding_opts.data_type).ToBe(eDataTypeText);
    });

    it("should handle binary data type correctly with source encoding", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "binary");
      strcpy(zusf.encoding_opts.source_codepage, "UTF-8");
      zusf.encoding_opts.data_type = eDataTypeBinary;

      // For binary data, encoding should not be used for conversion
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("binary");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("UTF-8");
      Expect(zusf.encoding_opts.data_type).ToBe(eDataTypeBinary);
    });

    it("should handle empty source encoding gracefully", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      // Explicitly set source_codepage to empty
      memset(zusf.encoding_opts.source_codepage, 0, sizeof(zusf.encoding_opts.source_codepage));
      zusf.encoding_opts.data_type = eDataTypeText;

      // Should handle empty source encoding (will default to UTF-8 in actual conversion)
      Expect(std::strlen(zusf.encoding_opts.source_codepage)).ToBe(0);
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("IBM-1047");
    });

    it("should preserve encoding settings in file operations struct", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      strcpy(zusf.encoding_opts.source_codepage, "ISO8859-1");
      zusf.encoding_opts.data_type = eDataTypeText;

      // Simulate what would happen in a file operation
      ZUSF zusf_copy = zusf;

      // Verify encodings are preserved
      Expect(std::string(zusf_copy.encoding_opts.codepage)).ToBe("IBM-1047");
      Expect(std::string(zusf_copy.encoding_opts.source_codepage)).ToBe("ISO8859-1");
      Expect(zusf_copy.encoding_opts.data_type).ToBe(eDataTypeText);
    });

    it("should handle USS file encoding with source encoding set", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "UTF-8");
      strcpy(zusf.encoding_opts.source_codepage, "IBM-1047");
      zusf.encoding_opts.data_type = eDataTypeText;

      const std::string test_file = "/tmp/zusf_source_encoding_test.txt";

      // Create a test file with some content
      unlink(test_file.c_str());
      std::ofstream file(test_file);
      file << "Test content for source encoding";
      file.close();

      // Test that the encoding options are properly set for file operations
      // We can't easily test the actual encoding conversion without mainframe-specific
      // file tags, but we can verify the struct is configured correctly
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("UTF-8");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("IBM-1047");

      // Test that file exists and can be accessed
      struct stat file_stats;
      int stat_result = stat(test_file.c_str(), &file_stats);
      Expect(stat_result).ToBe(0);
      Expect(S_ISREG(file_stats.st_mode)).ToBe(true);

      // Cleanup
      unlink(test_file.c_str());
    });

    it("should validate encoding field sizes and limits", [&]() -> void
    {
      // Test maximum length encoding names (15 chars + null terminator)
      std::string long_target = "IBM-1234567890A"; // 15 characters
      std::string long_source = "UTF-1234567890B"; // 15 characters

      strncpy(zusf.encoding_opts.codepage, long_target.c_str(), sizeof(zusf.encoding_opts.codepage) - 1);
      strncpy(zusf.encoding_opts.source_codepage, long_source.c_str(), sizeof(zusf.encoding_opts.source_codepage) - 1);

      // Ensure null termination
      zusf.encoding_opts.codepage[sizeof(zusf.encoding_opts.codepage) - 1] = '\0';
      zusf.encoding_opts.source_codepage[sizeof(zusf.encoding_opts.source_codepage) - 1] = '\0';

      Expect(std::string(zusf.encoding_opts.codepage)).ToBe(long_target);
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe(long_source);

      // Verify the struct size assumptions
      Expect(sizeof(zusf.encoding_opts.codepage)).ToBe(16);
      Expect(sizeof(zusf.encoding_opts.source_codepage)).ToBe(16);
    });

    it("should handle various encoding combinations for USS files", [&]() -> void
    {
      zusf.encoding_opts.data_type = eDataTypeText;

      // Test common USS file encoding conversions
      struct EncodingPair
      {
        const char *source;
        const char *target;
        const char *description;
      };

      EncodingPair pairs[] = {
          {"UTF-8", "IBM-1047", "UTF-8 to EBCDIC"}, {"IBM-037", "UTF-8", "EBCDIC to UTF-8"}, {"IBM-1047", "ISO8859-1", "EBCDIC to ASCII"}, {"ISO8859-1", "UTF-8", "ASCII to UTF-8"}, {"UTF-8", "binary", "Text to binary"}, {"IBM-1208", "IBM-037", "UTF-8 CCSID to EBCDIC"}};

      for (const auto &pair : pairs)
      {
        memset(&zusf.encoding_opts, 0, sizeof(zusf.encoding_opts));
        strcpy(zusf.encoding_opts.source_codepage, pair.source);
        strcpy(zusf.encoding_opts.codepage, pair.target);
        zusf.encoding_opts.data_type = eDataTypeText;

        // Verify encoding std::pair is set correctly
        Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe(std::string(pair.source));
        Expect(std::string(zusf.encoding_opts.codepage)).ToBe(std::string(pair.target));
        Expect(zusf.encoding_opts.data_type).ToBe(eDataTypeText);
      }
    });

    it("should handle source encoding with file creation operations", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      strcpy(zusf.encoding_opts.source_codepage, "UTF-8");
      zusf.encoding_opts.data_type = eDataTypeText;

      const std::string test_file = "/tmp/zusf_create_with_source_encoding.txt";

      // Cleanup any existing file
      unlink(test_file.c_str());

      // Test file creation with encoding options set
      // This simulates what would happen during file write operations
      int result = zusf_create_uss_file_or_dir(&zusf, test_file, 0644, CreateOptions());
      Expect(result).ToBe(RTNCD_SUCCESS);

      // Verify file was created
      struct stat file_stats;
      Expect(stat(test_file.c_str(), &file_stats)).ToBe(0);
      Expect(S_ISREG(file_stats.st_mode)).ToBe(true);

      // Verify encoding options are still set correctly
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("IBM-1047");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("UTF-8");

      // Cleanup
      unlink(test_file.c_str());
    });

    it("should handle source encoding with directory operations", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "UTF-8");
      strcpy(zusf.encoding_opts.source_codepage, "IBM-037");
      zusf.encoding_opts.data_type = eDataTypeText;

      const std::string test_dir = "/tmp/zusf_dir_source_encoding_test";

      // Cleanup any existing directory
      rmdir(test_dir.c_str());

      // Test directory creation with encoding options set
      int result = zusf_create_uss_file_or_dir(&zusf, test_dir, 0755, CreateOptions(true));
      Expect(result).ToBe(RTNCD_SUCCESS);

      // Verify directory was created
      struct stat dir_stats;
      Expect(stat(test_dir.c_str(), &dir_stats)).ToBe(0);
      Expect(S_ISDIR(dir_stats.st_mode)).ToBe(true);

      // Verify encoding options are still preserved
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("UTF-8");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("IBM-037");

      // Cleanup
      rmdir(test_dir.c_str());
    });

    it("should maintain source encoding through error conditions", [&]() -> void
    {
      strcpy(zusf.encoding_opts.codepage, "IBM-1047");
      strcpy(zusf.encoding_opts.source_codepage, "UTF-8");
      zusf.encoding_opts.data_type = eDataTypeText;

      const std::string nonexistent_file = "/tmp/nonexistent_path_for_encoding_test.txt";

      // Ensure the file doesn't exist
      unlink(nonexistent_file.c_str());

      // Test that encoding options are preserved even when operations fail
      int result = zusf_chmod_uss_file_or_dir(&zusf, nonexistent_file, 0644, false);
      Expect(result).ToBe(RTNCD_FAILURE);

      // Verify encoding options are still set correctly after error
      Expect(std::string(zusf.encoding_opts.codepage)).ToBe("IBM-1047");
      Expect(std::string(zusf.encoding_opts.source_codepage)).ToBe("UTF-8");
      Expect(zusf.encoding_opts.data_type).ToBe(eDataTypeText);

      // Verify error message was set but encoding preserved
      Expect(std::strlen(zusf.diag.e_msg)).ToBeGreaterThan(0);
    });
    it("should report from=file encoding, to=local encoding when iconv fails on read", [&]() -> void
    {
      ZUSF zusf{};
      const std::string test_file = "/tmp/zusf_iconv_fail_" + get_random_string(8) + ".txt";
      {
        std::ofstream out(test_file, std::ios::binary);
        out << "invalid utf8: \xff\xfe\xfd";
        out.close();
      }
      strcpy(zusf.encoding_opts.codepage, "UTF-8");
      strcpy(zusf.encoding_opts.source_codepage, "IBM-1047");
      zusf.encoding_opts.data_type = eDataTypeText;

      std::string content;
      int rc = zusf_read_from_uss_file(&zusf, test_file, content);

      Expect(rc).ToBe(RTNCD_FAILURE);
      const std::string msg(zusf.diag.e_msg);
      Expect(msg).ToContain("from UTF-8 to IBM-1047");

      unlink(test_file.c_str());
    });
  });

  describe("zusf_move_uss_file_or_dir tests", [&]() -> void
  {
    std::string zusf_test_dir = "/tmp/zusf_test_dir_" + get_random_string(10);
    TestDirGuard test_dir(zusf_test_dir.c_str());

    ZUSF zusf{};
    beforeEach([&zusf]() -> void
    {
      zusf = {};
    });

    describe("error conditions", [&]() -> void
    {
      it("should fail when source or destination is empty", [&]() -> void
      {
        int result = zusf_move_uss_file_or_dir(&zusf, "", "");
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");

        result = zusf_move_uss_file_or_dir(&zusf, "test_file.txt", "");
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");

        result = zusf_move_uss_file_or_dir(&zusf, "", "test_file.txt");
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");
      });

      it("should fail when source or target path is too long", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        source.append(PATH_MAX, 'a');
        target.append(PATH_MAX, 'a');

        int rc = zusf_move_uss_file_or_dir(&zusf, source, zusf_test_dir);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");

        rc = zusf_move_uss_file_or_dir(&zusf, zusf_test_dir, target);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");

        rc = zusf_move_uss_file_or_dir(&zusf, source, target);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source or target path is empty or too long");
      });

      it("should fail when source does not exist", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        int result = zusf_move_uss_file_or_dir(&zusf, source, "test_file.txt");
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Source path '" + source + "' does not exist");
      });

      it("should fail when source and target are the same", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        TestFileGuard file(source.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, source);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg))
            .ToContain("Source '" + source + "' and target '" + source + "' are identical");
      });

      it("should fail when source is a directory and target is not a directory", [&]() -> void
      {
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard file(target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, zusf_test_dir, target);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg))
            .ToContain("Cannot move directory '" + zusf_test_dir + "'. Target '" + target + "' is not a directory");
      });

      it("should fail when source is a pipe and target is not a pipe", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard file(source.c_str(), 'p');
        TestFileGuard target_file(target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg))
            .ToContain("Cannot move pipe '" + source + "'. Target '" + target + "' is not a pipe");
      });

      it("should fail to move a symlink to a file if the symlink points to the same file", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard target_file(target.c_str());
        TestFileGuard file(source.c_str(), 'l', target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg))
            .ToContain("Source '" + source + "' and target '" + target + "' are identical");
      });

      it("should fail to move a symlink to a file if the symlink points to a directory", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        std::string test_file = get_random_uss(zusf_test_dir);
        TestDirGuard target_dir(target.c_str());
        TestFileGuard file(source.c_str(), 'l', target.c_str());
        TestFileGuard test_file_guard(test_file.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, test_file);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg))
            .ToContain("Cannot move directory '" + source + "'. Target '" + test_file + "' is not a directory");
      });

      it("should fail when using force false and target exists", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard source_file(source.c_str());
        TestFileGuard target_file(target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target, false);
        Expect(result).ToBe(RTNCD_FAILURE);
        Expect(std::string(zusf.diag.e_msg)).ToContain("Target path '" + target + "' already exists");
      });
      // Done with error conditions
    });
    describe("target missing - happy paths", [&]() -> void
    {
      it("should move a file to a new location", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard file(source.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        file.reset(target.c_str());
        Expect(result).ToBe(RTNCD_SUCCESS);
        Expect(std::string(zusf.diag.e_msg)).ToBe("");

        struct stat file_stats;
        Expect(stat(target.c_str(), &file_stats)).ToBe(0);
        Expect(S_ISREG(file_stats.st_mode)).ToBe(true);
      });

      it("should move a file to a new location with special characters in the path", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        source = source.append(" (&$");
        target = target.append(" (&$");
        TestFileGuard file(source.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        file.reset(target.c_str());
        Expect(std::string(zusf.diag.e_msg)).ToBe("");
        Expect(result).ToBe(RTNCD_SUCCESS);

        struct stat file_stats;
        Expect(stat(target.c_str(), &file_stats)).ToBe(0);
        Expect(S_ISREG(file_stats.st_mode)).ToBe(true);
      });

      it("should resolve the path of the source, even when navigating back", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard file(source.c_str());

        std::string temp_source = zusf_test_dir + "/../" + zusf_test_dir.substr(zusf_test_dir.find_last_of("/") + 1) +
                                  "/" + source.substr(source.find_last_of("/") + 1);

        int result = zusf_move_uss_file_or_dir(&zusf, temp_source, target);
        Expect(result).ToBe(RTNCD_SUCCESS);
        Expect(std::string(zusf.diag.e_msg)).ToBe("");

        struct stat file_stats;
        Expect(stat(target.c_str(), &file_stats)).ToBe(0);
        Expect(S_ISREG(file_stats.st_mode)).ToBe(true);
      });
      it("should move a directory inside a directory", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestDirGuard source_dir(source.c_str());
        TestDirGuard target_dir(target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        Expect(result).ToBe(RTNCD_SUCCESS);
        Expect(std::string(zusf.diag.e_msg)).ToBe("");

        std::string new_target = target + "/" + source.substr(source.find_last_of("/") + 1);
        source_dir.reset(new_target.c_str());

        struct stat dir_stats;
        Expect(stat(new_target.c_str(), &dir_stats)).ToBe(0);
        Expect(S_ISDIR(dir_stats.st_mode)).ToBe(true);
      });
      it("should move a FIFO pipe to a new location", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        TestFileGuard file(source.c_str(), 'p');

        int result = zusf_move_uss_file_or_dir(&zusf, source, target);
        file.reset(target.c_str());
        Expect(result).ToBe(RTNCD_SUCCESS);
        Expect(std::string(zusf.diag.e_msg)).ToBe("");

        struct stat file_stats;
        Expect(stat(target.c_str(), &file_stats)).ToBe(0);
        Expect(S_ISFIFO(file_stats.st_mode)).ToBe(true);
      });
      it("should move a symlink to a new location", [&]() -> void
      {
        std::string source = get_random_uss(zusf_test_dir);
        std::string target = get_random_uss(zusf_test_dir);
        std::string test_file = get_random_uss(zusf_test_dir);
        TestFileGuard target_file_guard(target.c_str());
        TestFileGuard file(source.c_str(), 'l', target.c_str());

        int result = zusf_move_uss_file_or_dir(&zusf, source, test_file);
        file.reset(test_file.c_str());
        Expect(result).ToBe(RTNCD_SUCCESS);
        Expect(std::string(zusf.diag.e_msg)).ToBe("");

        struct stat file_stats;
        Expect(lstat(test_file.c_str(), &file_stats)).ToBe(0);
        Expect(S_ISLNK(file_stats.st_mode)).ToBe(true);
      });
      // Done with target missing
    });
  });
}
