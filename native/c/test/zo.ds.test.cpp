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
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "../zbase64.h"
#include "ztest.hpp"
#include "zutils.hpp"
#include "ztype.h"
#include "zo.test.hpp"
#include "zo.ds.test.hpp"
#include "../zusf.hpp"
#include "../zjb.hpp"
using namespace ztst;

// Generic helper function for creating data sets
void _create_ds(const std::string &ds_name, const std::string &ds_options = "")
{
  std::string response;
  std::string command = zo_command + " data-set create " + ds_name + " " + ds_options;
  int rc = execute_command_with_output(command, response);
  ExpectWithContext(rc, response).ToBe(0);
  Expect(response).ToContain("Data set created");
}

void zo_ds_tests()
{
  std::vector<std::string> _ds;
  std::vector<std::string> created_aliases;
  describe("data-set", [&]() -> void
  {
    TEST_OPTIONS long_test_opts = {false, 60};

    afterAll([&]() -> void
    {
      TestLog("Deleting " + std::to_string(_ds.size()) + " data sets...");
      for (const auto &ds : _ds)
      {
        try
        {
          std::string command = zo_command + " data-set delete " + ds;
          std::string response;
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Data set '" + ds + "' deleted"); // ds deleted
        }
        catch (const std::exception &deleteErr)
        {
          try
          {
            std::string response;
            std::string command = zo_command + " data-set list " + ds + " --no-warn --me 1";
            int rc = execute_command_with_output(command, response);
            ExpectWithContext(rc, response).ToBe(0);
            Expect(response).Not().ToContain(ds);
          }
          catch (...)
          {
            TestLog("Failed to delete: " + ds + ", error: " + deleteErr.what());
          }
        }
      }
    }, long_test_opts);
    it("should display help", []() -> void
    {
      int rc = 0;
      std::string response;
      std::string command = zo_command + " data-set";
      rc = execute_command_with_output(command, response);
      ExpectWithContext(rc, response).ToBe(0);
      Expect(response).ToContain("compress");
      Expect(response).ToContain("create");
      Expect(response).ToContain("create-adata");
      Expect(response).ToContain("create-fb");
      Expect(response).ToContain("create-loadlib");
      Expect(response).ToContain("create-member");
      Expect(response).ToContain("create-vb");
      Expect(response).ToContain("delete");
      Expect(response).ToContain("delete-vsam");
      Expect(response).ToContain("list");
      Expect(response).ToContain("list-members");
      Expect(response).ToContain("restore");
      Expect(response).ToContain("view");
      Expect(response).ToContain("write");
    });

    describe("compress", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should error when the data set is PS", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");

        std::string response;
        std::string command = zo_command + " data-set compress " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: data set '" + ds + "' is not a PDS");
      });
      it("should error when the data set is PDS/E", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

        std::string response;
        std::string command = zo_command + " data-set compress " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: data set '" + ds + "' is not a PDS");
      });

      it("should error when the data set doesn't exist", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";

        std::string response;
        std::string command = zo_command + " data-set compress " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: data set '" + ds + "' is not a PDS");
      });

      // NOTE: This test may fail if we don't save/clear/restore registers for PDSMAN/IEBCOPY
      // See https://github.com/zowe/zowex/issues/790
      it("should compress a data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        std::string command = zo_command + " data-set compress " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set");
        Expect(response).ToContain("compressed");
      });

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should error when the data set is VSAM", []() -> void {});
      xit("should error when the data set is GDG", []() -> void {});
      xit("should error when the data set is ALIAS", []() -> void {});
    });
    describe("create", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should error when the data set already exists", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);

        std::string response;
        std::string command = zo_command + " data-set create " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set");
      });

      it("should create a data set with default attributes", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);

        std::string response;
        std::string command = zo_command + " data-set list " + ds + " -a --rfc";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        // NOTE: Non-SMS managed systems return `--` for dsorg if not specified
        Expect(tokens[3]).ToBe("PS");
        Expect(tokens[4]).ToBe("FB");
      });

      it("should create a simple PDS/E data set - dsorg: PO and dsntype: LIBRARY", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

        std::string response;
        std::string command = zo_command + " data-set list " + ds + " -a --rfc";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        // DSNTYPE may be LIBRARY or PDS depending on system SMS settings
        Expect(tokens[9] == "LIBRARY" || tokens[9] == "PDS").ToBe(true);
      });

      it("should create a data set - recfm:VB dsorg:PO", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--recfm VB --dsorg PO --dirblk 2");

        std::string response;

        std::string command = zo_command + " data-set list " + ds + " -a --rfc";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        Expect(tokens[4]).ToBe("VB");
      });

      it("should create a PS data set - recfm:VB dsorg:PS", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--recfm VB --dsorg PS");

        std::string response;
        std::string command = zo_command + " data-set list " + ds + " -a --rfc";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PS");
        Expect(tokens[4]).ToBe("VB");
      });

      // TODO: https://github.com/zowe/zowex/pull/625
      it("should create a data set - dsorg: PO, primary: 10, secondary: 2, lrecl: 20, blksize:10, dirblk: 5, alcunit: CYL",
         [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --primary 10 --secondary 2 --lrecl 20 --blksize 10 --dirblk 5 --alcunit CYL");

        std::string response;
        std::string command = zo_command + " data-set list " + ds + " -a --rfc";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO"); // dsorg
        Expect(tokens[4]).ToBe("FB"); // recfm
        Expect(tokens[5]).ToBe("20"); // lrecl
        Expect(tokens[6]).ToBe("10"); // blksize
        Expect(tokens[7]).ToBe("10"); // primary
        Expect(tokens[8]).ToBe("2");  // secondary
      });
      it("should fail to create a data set if the data set name is too long", []() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set create " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
    });
    describe("create-adata", [&]() -> void
    {
      it("should create a data set with default attributes", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-adata " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        Expect(tokens[4]).ToBe("VB");
        // DSNTYPE may be LIBRARY or PDS depending on system SMS settings
        Expect(tokens[9] == "LIBRARY" || tokens[9] == "PDS").ToBe(true);
        // lrecl = 32756
      });

      it("should fail to create a data set if the data set already exists", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-adata " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set create-adata " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
      it("should fail to create a data set if the data set name is too long", []() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set create-adata " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
    });

    describe("create-fb", [&]() -> void
    {
      it("should create a data set with default attributes", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-fb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        Expect(tokens[4]).ToBe("FB");
        // DSNTYPE may be LIBRARY or PDS depending on system SMS settings
        Expect(tokens[9] == "LIBRARY" || tokens[9] == "PDS").ToBe(true);
        // lrecl = 80
      });
      it("should fail to create a data set if the data set already exists", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-fb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set create-fb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
      it("should fail to create a data set if the data set name is too long", []() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set create-fb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
    });
    describe("create-loadlib", [&]() -> void
    {
      it("should create a data set with default attributes", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-loadlib " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        Expect(tokens[4]).ToBe("U");
        Expect(tokens[5]).ToBe("0"); // lrecl
        // DSNTYPE may be LIBRARY or PDS depending on system SMS settings
        Expect(tokens[9] == "LIBRARY" || tokens[9] == "PDS").ToBe(true);
      });
      it("should fail to create a data set if the data set already exists", [&]() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-loadlib " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set create-loadlib " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
      it("should fail to create a data set if the data set name is too long", []() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set create-loadlib " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
    });
    describe("create-member", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        std::string ds = get_random_ds();
        _ds.push_back(ds);

        std::string response;
        std::string command = zo_command + " data-set create-fb " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");
      });
      it("should error if no member name is specified", [&]() -> void
      {
        std::string ds = _ds.back();
        std::string response;
        std::string command = zo_command + " data-set create-member " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not find member name in dsn");
      });
      it("should error if the data set doesn't exist", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";
        std::string response;
        std::string command = zo_command + " data-set create-member '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set member");
        Expect(response).ToContain("Not found in any catalog");
      });
      it("should create a member in a PDS", [&]() -> void
      {
        std::string ds = _ds.back();
        std::string response;
        std::string command = zo_command + " data-set create-member '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created: '" + ds + "(TEST)'");
      });
      it("should not overwrite existing members", [&]() -> void
      {
        std::string ds = "'" + _ds.back() + "(TEST)'";
        std::string response;
        std::string command = zo_command + " data-set create-member " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created");

        // Write "test" data
        command = "echo test | " + zo_command + " data-set write " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to");

        // Read "test" data to confirm
        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");

        // Create the same TEST member
        command = zo_command + " data-set create-member " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Warning");

        // Read "test" data to confirm
        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");
      });
      it("should overwrite existing members if --overwrite is specified", [&]() -> void
      {
        std::string ds = "'" + _ds.back() + "(TEST)'";
        std::string response;
        std::string command = zo_command + " data-set create-member " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created");

        // Write "test" data
        command = "echo test | " + zo_command + " data-set write " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to");

        // Read "test" data to confirm
        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test");

        // Create the same TEST member
        command = zo_command + " data-set create-member " + ds + " --overwrite";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created");

        // Read to confirm data was overwritten
        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).Not().ToContain("test");
      });
    });
    describe("create-vb", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should create a data set with default attributes", [&]() -> void
      {
        std::string ds = _ds.back();

        std::string response;
        std::string command = zo_command + " data-set create-vb " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[3]).ToBe("PO");
        Expect(tokens[4]).ToBe("VB");
        // DSNTYPE may be LIBRARY or PDS depending on system SMS settings
        Expect(tokens[9] == "LIBRARY" || tokens[9] == "PDS").ToBe(true);
        Expect(tokens[5]).ToBe("255"); // lrecl
      });
      it("should error when the data set already exists", [&]() -> void
      {
        std::string ds = _ds.back();

        std::string response;
        std::string command = zo_command + " data-set create-vb " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set created");

        command = zo_command + " data-set create-vb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
      it("should fail to create a data set if the data set name is too long", []() -> void
      {
        int rc = 0;
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set create-vb " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not create data set: '" + ds + "'");
      });
    });
    describe("delete", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });

      it("should delete a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");

        std::string response;
        std::string command = zo_command + " data-set delete " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set '" + ds + "' deleted");
      });
      it("should delete a partitioned data set (PDS)", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        std::string command = zo_command + " data-set delete " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set '" + ds + "' deleted");
      });
      it("should delete a partitioned data set extended (PDSE)", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

        std::string response;
        std::string command = zo_command + " data-set delete " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set '" + ds + "' deleted");
      });
      it("should delete a member of a partitioned data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        execute_command_with_output(zo_command + " data-set create-member '" + ds + "(TESTMEM)'", response);

        std::string command = zo_command + " data-set delete '" + ds + "(TESTMEM)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set '" + ds + "(TESTMEM)' deleted");
      });

      it("should fail to delete a non-existent data set", [&]() -> void
      {
        std::string ds = _ds.back();

        std::string response;
        std::string command = zo_command + " data-set delete " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not delete data set: '" + ds + "'");
      });
      it("should fail to delete a data set if the data set name is too long", []() -> void
      {
        std::string ds = get_random_ds(8);

        std::string response;
        std::string command = zo_command + " data-set delete " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not delete data set: '" + ds + "'");
      });

      // TODO: What do?
      xit("should fail to delete a data set if not authorized", []() -> void {});
      // TODO: What do?
      xit("should fail to delete a data set that is currently in use", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/665
      xit("should delete multiple data sets specified in a list", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/664
      xit("should delete a data set using the force option even if it has members", []() -> void {});
      xit("should not delete a data set with the force option if it is in use", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should delete a VSAM KSDS data set", []() -> void {});
      xit("should delete a VSAM ESDS data set", []() -> void {});
      xit("should delete a VSAM RRDS data set", []() -> void {});
      xit("should delete a VSAM LDS data set", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should delete a generation data group (GDG) base when empty", []() -> void {});
      xit("should delete a generation data group (GDG) base and all its generations", []() -> void {});
      xit("should delete a specific generation of a GDG", []() -> void {});
      xit("should error when attempting to delete a GDG base with generations without the PURGE or FORCE option",
          []() -> void {});
    });
    describe("delete VSAM", [&]() -> void
    {
      for (const std::string organization : {"INDEXED", "NONINDEXED", "NUMBERED", "LINEAR"})
      {
        it("should delete a " + organization + " cluster with default options", [organization]() -> void
        {
          TestVsamGuard cluster(organization);
          std::string response;
          const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "'", response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("VSAM cluster '" + cluster.name() + "' deleted");

          const int list_rc = execute_command_with_output(zo_command + " ds list '" + cluster.name() + "' --exact-match", response);
          ExpectWithContext(list_rc, response).Not().ToBe(0);
        });
      }

      for (const std::string options : {"--erase true", "--erase false", "--purge", "--erase true --purge"})
      {
        it("should delete a cluster with " + options, [options]() -> void
        {
          TestVsamGuard cluster;
          std::string response;
          const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "' " + options, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("VSAM cluster '" + cluster.name() + "' deleted");
        });
      }

      it("should accept lowercase cluster names", []() -> void
      {
        TestVsamGuard cluster;
        std::string lower_name = cluster.name();
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c)
        { return std::tolower(c); });
        std::string response;
        const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + lower_name + "'", response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("VSAM cluster '" + cluster.name() + "' deleted");
      });

      it("should forward erase options to IDCAMS and require purge for a retained cluster", []() -> void
      {
        TestVsamGuard cluster("NONINDEXED", true);
        std::string response;
        int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "'", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("IDCAMS returned nonzero RC");
        Expect(response).Not().ToContain("ERASE");

        rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "' --erase false", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain(" NOERASE -");

        rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "' --erase true", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("IDCAMS returned nonzero RC");
        Expect(response).ToContain(" ERASE -");
        Expect(response).Not().ToContain(" NOERASE -");

        rc = execute_command_with_output(zo_command + " ds delete-vsam '" + cluster.name() + "' --purge", response);
        ExpectWithContext(rc, response).ToBe(0);
      });

      it("should report IDCAMS diagnostics for a missing cluster", []() -> void
      {
        std::string response;
        const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + get_random_ds(3) + ".VSAM'", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("IDCAMS returned nonzero RC");
        Expect(response).ToContain("IDC3012I");
      });

      it("should reject empty names and dangerous IDCAMS characters", []() -> void
      {
        const std::vector<std::string> invalid_names = {
            "", "USER.*", "USER.VSAM(MEMBER)", "USER.VSAM,OTHER.VSAM", "USER.VSAM;DELETE",
            "USER.VSAM CLUSTER PURGE", "USER.VSAM\n DELETE USER.OTHER CLUSTER PURGE"};
        for (const auto &dsn : invalid_names)
        {
          std::string response;
          const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + dsn + "'", response);
          ExpectWithContext(rc, response).Not().ToBe(0);
          Expect(response).ToContain("invalid VSAM cluster name");
        }
      });

      it("should let IDCAMS validate data set naming rules", []() -> void
      {
        const std::string dsn = get_random_ds(3) + ".LONGQUALIFIER";
        std::string response;
        const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + dsn + "'", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("IDCAMS returned nonzero RC");
      });

      it("should leave a non-VSAM data set intact", [&]() -> void
      {
        const std::string dsn = get_random_ds(3);
        _ds.push_back(dsn);
        _create_ds(dsn, "--dsorg PS");
        std::string response;
        const int rc = execute_command_with_output(zo_command + " ds delete-vsam '" + dsn + "' --purge", response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        const int list_rc = execute_command_with_output(zo_command + " ds list '" + dsn + "' --exact-match", response);
        Expect(list_rc).ToBe(0);
      });
    });
    describe("list", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should list a data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);

        std::string response;
        std::string command = zo_command + " data-set list " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(ds);
      });
      it("should list data sets based on pattern and warn about listing too many data sets", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);
        _create_ds(ds + ".T00");
        _ds.push_back(ds + ".T00");

        std::string response;
        std::string pattern = ds;
        std::string command = zo_command + " data-set list " + pattern + " --me 1";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(RTNCD_WARNING);
        Expect(response).ToContain(ds);
        Expect(response).Not().ToContain(ds + ".T00");
      });

      it("should list up to the max entries specified and not warn", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);
        _create_ds(ds + ".T00");
        _ds.push_back(ds + ".T00");

        std::string response;
        std::string pattern = ds;
        std::string command = zo_command + " data-set list " + pattern + " --no-warn --me 1";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(ds);
        Expect(response).Not().ToContain(ds + ".T00");
      });

      it("should match descendant data sets by default", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);
        _create_ds(ds + ".T00");
        _ds.push_back(ds + ".T00");

        // Without --exact-match the pattern gains a trailing
        // ".**", so descendants are matched too.
        std::string response;
        std::string command = zo_command + " data-set list " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(ds);
        Expect(response).ToContain(ds + ".T00");
      });

      it("should match only the given data set with --exact-match", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds);
        _create_ds(ds + ".T00");
        _ds.push_back(ds + ".T00");

        // With --exact-match the pattern is used verbatim, so
        // the descendant is excluded.
        std::string response;
        std::string command = zo_command + " data-set list " + ds + " --exact-match";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(ds);
        Expect(response).Not().ToContain(ds + ".T00");
      });

      it("should warn when --exact-match finds no exact match", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds + ".T00");
        _ds.back() = ds + ".T00";

        // The parent qualifier only exists as a prefix of the
        // child, so an exact match must find nothing.
        std::string response;
        std::string command = zo_command + " data-set list " + ds + " --exact-match";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(RTNCD_WARNING);
        Expect(response).ToContain("Warning: no matching results found");
      });

      it("should warn when listing a non-existent data set", [&]() -> void
      {
        std::string ds = _ds.back();

        std::string response;
        std::string command = zo_command + " data-set list " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(RTNCD_WARNING);
        Expect(response).ToContain("Warning: no matching results found");
      });
      it("should error when the data set name is too long", [&]() -> void
      {
        std::string ds = get_random_ds(8);

        std::string response;
        std::string command = zo_command + " data-set list " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: data set pattern exceeds 44 character length limit");
      });

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should list information for a VSAM KSDS data set", []() -> void {});
      xit("should list information for a VSAM ESDS data set", []() -> void {});
      xit("should list information for a VSAM RRDS data set", []() -> void {});
      xit("should list information for a VSAM LDS data set", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should list generations of a generation data group (GDG) base", []() -> void {});
      xit("should list specific generation of a GDG", []() -> void {});
    });
    describe("list-members", [&]() -> void
    {
      std::string data_set = "SYS1.MACLIB";
      it("should list a member of a data set", [&]() -> void
      {
        std::string response;
        std::string command = zo_command + " data-set lm " + data_set + " --no-warn --me 1";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
      });
      it("should warn when listing members of a data set with many members", [&]() -> void
      {
        int rc = 0;
        std::string response;
        std::string command = zo_command + " data-set lm " + data_set + " --me 1";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(RTNCD_WARNING);
      });
      it("should error when the data set name is too long", [&]() -> void
      {
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set lm " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
      });
      it("should fail if the data set doesn't exist", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";
        std::string response;
        std::string command = zo_command + " data-set lm " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: Could not list members for '" + ds + "'");
      });
      it("should list members matching a specific pattern", [&]() -> void
      {
        std::string ds = get_random_ds();
        _ds.push_back(ds);
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        execute_command_with_output(zo_command + " data-set create-member '" + ds + "(ABC1)'", response);
        execute_command_with_output(zo_command + " data-set create-member '" + ds + "(ABC2)'", response);
        execute_command_with_output(zo_command + " data-set create-member '" + ds + "(XYZ1)'", response);

        std::string command = zo_command + " data-set lm " + ds + " --pattern \"ABC*\"";
        int rc = execute_command_with_output(command, response);

        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("ABC1");
        Expect(response).ToContain("ABC2");
        Expect(response).Not().ToContain("XYZ1");
      });
      it("should return ISPF statistics attributes when listing members with attributes", [&]() -> void
      {
        auto ds = get_random_ds();
        _ds.push_back(ds);

        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response{};

        execute_command_with_output(zo_command + " data-set create-member '" + ds + "(STAT1)'", response);

        execute_command_with_output("echo \"TEST\" | " + zo_command + " data-set write '" + ds + "(STAT1)'", response);
        std::string command = zo_command + " data-set lm " + ds + " --attributes";

        int rc = execute_command_with_output(command, response);

        ExpectWithContext(rc, response).ToBe(0);

        Expect(response).ToContain("STAT1");
        Expect(response).ToContain("/");
        Expect(response).ToContain(":");
        Expect(response).ToContain("N");
      });
    });
    describe("GDG", [&]() -> void
    {
      const std::string user = get_user();
      const std::string base = get_random_ds(3, user);
      const std::string gdg_base_dsn = base + ".GDG";
      const std::string gdg_gen1_dsn = gdg_base_dsn + ".G0001V00";
      const std::string gdg_gen2_dsn = gdg_base_dsn + ".G0002V00";

      auto submit_and_wait = [](const std::string &jcl, int max_polls = 600, bool check_rc = false)
      {
        ZJB zjb = {0};
        std::string jobid;
        const int rc = zjb_submit(&zjb, jcl, jobid);
        if (rc != 0)
          throw std::runtime_error("Failed to submit JCL: " + std::string(zjb.diag.e_msg));
        TestLog("Submitted job " + jobid + " (" + std::to_string(jcl.size()) + " bytes)");
        const std::string correlator(zjb.correlator, sizeof(zjb.correlator));
        ZJob final_job = {};
        for (int i = 0; i < max_polls; ++i)
        {
          ZJB zjb_v = {0};
          final_job = {};
          const int vrc = zjb_view(&zjb_v, correlator, final_job);
          if (vrc != 0)
            throw std::runtime_error("Failed to view job: " + std::string(zjb_v.diag.e_msg));
          if (!final_job.retcode.empty())
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        TestLog("Job " + jobid + " retcode='" + final_job.retcode + "'");
        zjb_delete(&zjb, jobid);
        if (check_rc && final_job.retcode.find("CC 0000") == std::string::npos)
          throw std::runtime_error("Job failed with retcode='" + final_job.retcode + "'");
      };

      TEST_OPTIONS gdg_opts = {false, 60};

      beforeAll([&]() -> void
      {
        // Clean up any leftover data sets from previous test runs
        std::string cleanup_output;
        std::string cleanup_err;
        std::string cleanup_idcams;
        cleanup_idcams += "  DELETE " + gdg_gen2_dsn + " NONVSAM PURGE\n";
        cleanup_idcams += "  DELETE " + gdg_gen1_dsn + " NONVSAM PURGE\n";
        cleanup_idcams += "  DELETE " + gdg_base_dsn + " GDG PURGE\n";
        cleanup_idcams += "  SET MAXCC = 0\n";
        int rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          throw std::runtime_error("Output and error from failed IDCAMS cleanup:" + cleanup_err + "\n" +
                                   cleanup_output + "\n");
        }
        // Define the GDG base via IDCAMS
        std::string setup_idcams;
        std::string setup_output;
        std::string setup_err;
        setup_idcams += "  DEFINE GDG ( -\n";
        setup_idcams += "    NAME(" + gdg_base_dsn + ") -\n";
        setup_idcams += "    LIMIT(5) -\n";
        setup_idcams += "    NOEMPTY -\n";
        setup_idcams += "    SCRATCH )\n";
        rc = zds_idcams(setup_idcams, setup_output, setup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          throw std::runtime_error("Output and error from failed IDCAMS setup:" + setup_err + "\n" + setup_output +
                                   "\n");
        }
        // Create generation 1 via IEBGENER
        std::string gen1_jcl;
        gen1_jcl += "//GDGGEN1 JOB IZUACCT\n";
        gen1_jcl += "//STEP1    EXEC PGM=IEBGENER\n";
        gen1_jcl += "//SYSPRINT DD SYSOUT=*\n";
        gen1_jcl += "//SYSIN    DD DUMMY\n";
        gen1_jcl += "//SYSUT1   DD *\n";
        gen1_jcl += "GENERATION ONE CONTENT\n";
        gen1_jcl += "/*\n";
        gen1_jcl += "//SYSUT2   DD DSN=" + gdg_base_dsn + "(+1),DISP=(NEW,CATLG),\n";
        gen1_jcl += "//            UNIT=SYSALLDA,SPACE=(TRK,(1,1)),\n";
        gen1_jcl += "//            RECFM=FB,LRECL=80,BLKSIZE=800\n";
        submit_and_wait(gen1_jcl, 600, true);

        // Create generation 2 via IEBGENER
        std::string gen2_jcl;
        gen2_jcl += "//GDGGEN2 JOB IZUACCT\n";
        gen2_jcl += "//STEP1    EXEC PGM=IEBGENER\n";
        gen2_jcl += "//SYSPRINT DD SYSOUT=*\n";
        gen2_jcl += "//SYSIN    DD DUMMY\n";
        gen2_jcl += "//SYSUT1   DD *\n";
        gen2_jcl += "GENERATION TWO CONTENT\n";
        gen2_jcl += "/*\n";
        gen2_jcl += "//SYSUT2   DD DSN=" + gdg_base_dsn + "(+1),DISP=(NEW,CATLG),\n";
        gen2_jcl += "//            UNIT=SYSALLDA,SPACE=(TRK,(1,1)),\n";
        gen2_jcl += "//            RECFM=FB,LRECL=80,BLKSIZE=800\n";
        submit_and_wait(gen2_jcl, 600, true);
      }, gdg_opts);

      afterAll([gdg_base_dsn, gdg_gen1_dsn, gdg_gen2_dsn]() -> void
      {
        std::string cleanup_idcams;
        std::string cleanup_err;
        std::string cleanup_output;
        cleanup_idcams += "  DELETE " + gdg_gen2_dsn + " NONVSAM PURGE\n";
        cleanup_idcams += "  DELETE " + gdg_gen1_dsn + " NONVSAM PURGE\n";
        cleanup_idcams += "  DELETE " + gdg_base_dsn + " GDG PURGE\n";
        cleanup_idcams += "  SET MAXCC = 0\n";
        int rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          throw std::runtime_error("Output and error from failed IDCAMS cleanup:" + cleanup_err + "\n" +
                                   cleanup_output + "\n");
        }
      }, gdg_opts);

      it("should list the GDG base in the catalog", [&]() -> void
      {
        std::string response;
        const std::string command = zo_command + " data-set list " + gdg_base_dsn + " --no-warn";
        const int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(gdg_base_dsn);
      }, gdg_opts);

      it("should report ?????? volser for GDG base when listing with attributes", [&]() -> void
      {
        std::string response;
        const std::string command = zo_command + " data-set list " + gdg_base_dsn + " -a --rfc";
        const int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("??????");
      }, gdg_opts);

      it("should list GDG generations using a wildcard pattern", [&]() -> void
      {
        std::string response;
        const std::string command = zo_command + " data-set list '" + gdg_base_dsn + ".*' --no-warn";
        const int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(gdg_gen1_dsn);
        Expect(response).ToContain(gdg_gen2_dsn);
      }, gdg_opts);

      it("should view the content of a GDG generation by absolute name", [&]() -> void
      {
        std::string response;
        const std::string command = zo_command + " data-set view " + gdg_gen1_dsn;
        const int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("GENERATION ONE CONTENT");
      }, gdg_opts);

      it("should view the most recent GDG generation via relative reference", [&]() -> void
      {
        std::string response;
        const std::string command = zo_command + " data-set view '" + gdg_base_dsn + "(0)'";
        const int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("GENERATION TWO CONTENT");
      }, gdg_opts);

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should delete a specific GDG generation", []() -> void {});
      xit("should delete a GDG base when it has no active generations", []() -> void {});
      xit("should delete a GDG base and all its generations", []() -> void {});
      xit("should error when deleting a GDG base with active generations without the PURGE option", []() -> void {});
    });
    // TODO: https://github.com/zowe/zowex/issues/380
    xdescribe("restore", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
        _create_ds(_ds.back());
      });
      it("should restore a data set", [&]() -> void
      {
        std::string ds = _ds.back();

        std::string response;
        std::string command = zo_command + " data-set restore " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Data set '" + ds + "' restored");
      });
      it("should fail to restore a non-existent backup", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";

        std::string response;
        std::string command = zo_command + " data-set restore " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not restore data set: '" + ds + "'");
      });
      // TODO: What do?
      xit("should fail to restore if not authorized", [&]() -> void {});
    });
    // CA Disk archival/restore is tested manually via native/c/test/test.cadisk.sh.
    // See doc/ref/ds/ca-disk.md for instructions.
    // Manual test for https://github.com/zowe/zowex/issues/1007
    // Bug: "data-set restore" returns success for a data set that was never archived.
    //
    // To verify manually once the fix is in place:
    //   1. Create a plain data set (never archived):
    //        zowex data-set create HLQ.UNARCHIVED --dsorg PS
    //   2. Attempt to restore it:
    //        zowex data-set restore HLQ.UNARCHIVED
    //   3. Expected (after fix): non-zero RC and a message such as
    //        "Error: data set 'HLQ.UNARCHIVED' is not archived"
    //   4. Actual (current bug): RC 0 and "Data set 'HLQ.UNARCHIVED' restored"
    //
    // For the full end-to-end archival/restore flow, see test.cadisk.sh.
    xit("should fail to restore a data set that was never archived", []() -> void {});
    describe("view", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should view the content of a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;

        std::string random_string = get_random_string(80, false);
        std::string command = "echo " + random_string + " | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
      });
      it("should view the content of a PDS member", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");
        std::string response;

        std::string random_string = get_random_string(80, false);
        std::string command = "echo " + random_string + " | " + zo_command + " data-set write '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "(TEST)'");

        command = zo_command + " data-set view '" + ds + "(TEST)'";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
      });
      it("should view a data set with different encoding", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command =
            "echo 'test!' | " + zo_command + " data-set write " + ds + " --local-encoding IBM-1047 --encoding IBM-1147";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test|");

        command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1047";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test|");

        command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1147";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test!");
      });
      it("should view multibyte-encoded content correctly", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        // Write Japanese こんにちは encoded as DBCS EBCDIC (IBM-939)
        std::string command = "printf '\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf' | " + zo_command +
                              " data-set write " + ds + " --encoding IBM-939";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        // View raw bytes to verify the stored DBCS EBCDIC encoding
        command = zo_command + " data-set view " + ds + " --rfb";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("0e 44 8a 44 bd 44 97 44 92 44 9d 0f");
      });
      it("should fail to view a non-existent data set", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";
        std::string response;
        std::string command = zo_command + " data-set view " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not read data set: '" + ds + "'");
      });
      it("should error when the data set name too long", [&]() -> void
      {
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = zo_command + " data-set view " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not read data set: '" + ds + "'");
      });

      // What do?
      xit("should fail to view a data set if not authorized", []() -> void {});

      // TODO: https://github.com/zowe/zowex/issues/666
      xit("should view the content of a VSAM KSDS data set", []() -> void {});
      xit("should view the content of a VSAM ESDS data set", []() -> void {});
      xit("should view the content of a VSAM RRDS data set", []() -> void {});
      xit("should view the content of a VSAM LDS data set", []() -> void {});

      // zds_read uses POSIX fopen("r") which allocates the data set with DISP=SHR.
      // SHR allocation allows multiple concurrent openers without ENQ or RESERVE,
      // making concurrent reads safe on both PDS and PDSE. These tests verify that
      // behavior by calling zds_read directly from threads (no fork/popen involved).
      describe("concurrent reads", [&]() -> void
      {
        TEST_OPTIONS concurrent_opts = {false, 60};

        it("should not block concurrent reads of different PDSE members: fopen(r) does not take an exclusive ENQ",
           [&]() -> void
        {
          const std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 5 --dsntype LIBRARY");

          const int thread_count = 4;
          for (int i = 0; i < thread_count; ++i)
          {
            const std::string member = "MEM" + std::to_string(i);
            const std::string data = "content for member " + std::to_string(i);
            const std::string command =
                "echo '" + data + "' | " + zo_command + " data-set write '" + ds + "(" + member + ")'";
            std::string response;
            const int rc = execute_command_with_output(command, response);
            ExpectWithContext(rc, response).ToBe(0);
          }

          std::vector<int> results(thread_count, -1);
          std::vector<std::string> contents(thread_count);
          std::vector<std::thread> threads;

          for (int i = 0; i < thread_count; ++i)
          {
            threads.emplace_back([&, i]()
            {
              ZDS zds = {};
              const std::string member_path = ds + "(MEM" + std::to_string(i) + ")";
              ZDSReadOpts read_opts{.zds = &zds, .dsname = member_path};
              results[i] = zds_read(read_opts, contents[i]);
            });
          }

          for (auto &t : threads)
            t.join();

          for (int i = 0; i < thread_count; ++i)
          {
            ExpectWithContext(results[i], contents[i]).ToBe(0);
            Expect(contents[i]).ToContain("content for member " + std::to_string(i));
          }
        }, concurrent_opts);

        it("should not block concurrent reads of the same PDSE member: fopen(r) uses SHR not exclusive ENQ",
           [&]() -> void
        {
          const std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 5 --dsntype LIBRARY");

          const std::string expected = "shared member content";
          const std::string write_cmd =
              "echo '" + expected + "' | " + zo_command + " data-set write '" + ds + "(SHARED)'";
          std::string setup_response;
          const int setup_rc = execute_command_with_output(write_cmd, setup_response);
          ExpectWithContext(setup_rc, setup_response).ToBe(0);

          const int thread_count = 4;
          std::vector<int> results(thread_count, -1);
          std::vector<std::string> contents(thread_count);
          std::vector<std::thread> threads;

          for (int i = 0; i < thread_count; ++i)
          {
            threads.emplace_back([&, i]()
            {
              ZDS zds = {};
              const std::string member_path = ds + "(SHARED)";
              ZDSReadOpts read_opts{.zds = &zds, .dsname = member_path};
              results[i] = zds_read(read_opts, contents[i]);
            });
          }

          for (auto &t : threads)
            t.join();

          for (int i = 0; i < thread_count; ++i)
          {
            ExpectWithContext(results[i], contents[i]).ToBe(0);
            Expect(contents[i]).ToContain(expected);
          }
        }, concurrent_opts);
      });
    });
    describe("write", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should write content to a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");

        std::string response;
        std::string random_string = get_random_string(80, false);
        std::string command = "echo " + random_string + " | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
      });
      it("should write content to a PDS member", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        std::string command = zo_command + " data-set create-member '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created");

        std::string random_string = get_random_string(80, false);
        command = "echo " + random_string + " | " + zo_command + " data-set write '" + ds + "(TEST)'";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "(TEST)'");

        command = zo_command + " data-set view '" + ds + "(TEST)'";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
      });

      it("should fail to write to a RECFM=U data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2 --recfm U --lrecl 0 --blksize 32760");

        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Writing to RECFM=U data sets is not supported");
      });
      it("should be able to write to a RECFM=A data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS --recfm A --lrecl 80 --blksize 800");

        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");
      });
      it("should preserve LRECL and BLKSIZE when writing to a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS --recfm FB --lrecl 133 --blksize 13300");

        std::string response;
        std::string random_string = get_random_string(80, false);
        std::string command = "echo " + random_string + " | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[4]).ToBe("FB");    // recfm
        Expect(tokens[5]).ToBe("133");   // lrecl
        Expect(tokens[6]).ToBe("13300"); // blksize
      });
      it("should preserve LRECL when writing to a variable-length sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS --recfm VB --lrecl 259 --blksize 263");

        std::string response;
        std::string random_string = get_random_string(80, false);
        std::string command = "echo " + random_string + " | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set list " + ds + " -a --rfc";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        std::vector<std::string> tokens = parse_rfc_response(response, ",");
        Expect(tokens[4]).ToBe("VB");  // recfm
        Expect(tokens[5]).ToBe("259"); // lrecl
      });
      it("should overwrite content in a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");

        std::string response;
        std::string random_string = get_random_string(80, false);
        std::string random_string1 = get_random_string(80, false);
        std::string random_string2 = get_random_string(80, false);
        std::string command =
            "echo '" + random_string + "\n" + random_string1 + "' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
        Expect(response).ToContain(random_string1);
        Expect(response).Not().ToContain(random_string2);

        command = "echo " + random_string2 + " | " + zo_command + " data-set write " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string2);
        Expect(response).Not().ToContain(random_string);
        Expect(response).Not().ToContain(random_string1);
      });
      it("should overwrite content in a PDS member", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PO --dirblk 2");

        std::string response;
        std::string command = zo_command + " data-set create-member '" + ds + "(TEST)'";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Member created");

        std::string random_string = get_random_string(80, false);
        std::string random_string1 = get_random_string(80, false);
        std::string random_string2 = get_random_string(80, false);
        command = "echo '" + random_string + "\n" + random_string1 + "' | " + zo_command + " data-set write '" + ds +
                  "(TEST)' --local-encoding IBM-1047";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "(TEST)'");

        command = zo_command + " data-set view '" + ds + "(TEST)'";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string);
        Expect(response).ToContain(random_string1);
        Expect(response).Not().ToContain(random_string2);

        command = "echo " + random_string2 + " | " + zo_command + " data-set write '" + ds +
                  "(TEST)' --local-encoding IBM-1047";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "(TEST)'");

        command = zo_command + " data-set view '" + ds + "(TEST)'";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(random_string2);
        Expect(response).Not().ToContain(random_string);
        Expect(response).Not().ToContain(random_string1);
      });

      it("should only print the etag when requested", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write " + ds + " --etag-only";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("etag: 8890283"); // etag for "test"
        Expect(response).Not().ToContain("Wrote data to '" + ds + "'");
      });

      it("should fail if the provided etag is different from the current etag", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = "echo 'zowe' | " + zo_command + " data-set write " + ds + " --etag 8bb0280"; // etag for "zowe"
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Etag mismatch: expected 8bb0280, actual 8890283");
        Expect(response).Not().ToContain("Wrote data to '" + ds + "'");
      });

      it("should fail if the provided etag is different and evaluates to a number", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command = "echo 'zowe' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = "echo 'test' | " + zo_command + " data-set write " + ds + " --etag 8890283"; // etag for "test"
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Etag mismatch: expected 8890283, actual 8bb0280");
        Expect(response).Not().ToContain("Wrote data to '" + ds + "'");
      });

      it("should write content to a data set with different encoding", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command =
            "echo 'test!' | " + zo_command + " data-set write " + ds + " --local-encoding IBM-1047 --encoding IBM-1147";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("test|");

        command = zo_command + " data-set view " + ds + " --rfb";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("a3 85 a2 a3 4f 15");
      });

      it("should write content to a data set with multibyte encoding", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS");
        std::string response;
        std::string command = "printf '\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf' | " + zo_command +
                              " data-set write " + ds + " --encoding IBM-939";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds + " --rfb";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("0e 44 8a 44 bd 44 97 44 92 44 9d 0f");
      });

      it("should fail to write to a non-existent data set", [&]() -> void
      {
        std::string ds = _ds.back() + ".GHOST";
        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not write to data set: '" + ds + "'");
      });
      it("should error when the data set name is too long", []() -> void
      {
        std::string ds = get_random_ds(8);
        std::string response;
        std::string command = "echo 'test' | " + zo_command + " data-set write " + ds;
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).Not().ToBe(0);
        Expect(response).ToContain("Error: could not write to data set: '" + ds + "'");
      });
      it("should write content from a USS file to a sequential data set", [&]() -> void
      {
        std::string ds = _ds.back();
        _create_ds(ds, "--dsorg PS --lrecl 120 --blksize 18480");
        std::string response;

        // Create a temporary USS file
        std::string temp_file = "/tmp/zowex_test_uss_" + get_random_string(8, false);
        std::string content = "This is a test content for USS file write.";

        ZUSF zusf = {};
        std::string write_content = content;
        int write_rc = zusf_write_to_uss_file(&zusf, temp_file, write_content);
        Expect(write_rc).ToBe(RTNCD_SUCCESS);

        // View the USS file as binary to avoid BPXK_AUTOCVT interference in the pipe,
        // then write the raw bytes into the data set (also as binary).
        std::string get_uss_file_command = zo_command + " uss view '" + temp_file + "' --ec binary";
        std::string command = get_uss_file_command + " | " + zo_command + " data-set write " + ds + " --ec binary";
        int rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("Wrote data to '" + ds + "'");

        command = zo_command + " data-set view " + ds + " --ec binary";
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain(content);

        // Cleanup
        remove(temp_file.c_str());
      });

      describe("BPAM member writes", [&]() -> void
      {
        it("should write and read a PDS member using BPAM", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(PDS1)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          std::string payload = "PDSDATA";
          command = "echo " + payload + " | " + zo_command + " data-set write '" + ds + "(PDS1)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(PDS1)'");

          command = zo_command + " data-set view '" + ds + "(PDS1)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(payload);
        });
        it("should write and read a PDSE member using BPAM", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(MEM1)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          std::string payload = "MEMBERDATA";
          command = "echo " + payload + " | " + zo_command + " data-set write '" + ds + "(MEM1)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(MEM1)'");

          command = zo_command + " data-set view '" + ds + "(MEM1)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(payload);
        });

        it("should write multi-line content to a PDSE member", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(MEM2)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          std::string command_payload =
              "printf 'LINEA\\nLINEB\\n' | " + zo_command + " data-set write '" + ds + "(MEM2)'";
          rc = execute_command_with_output(command_payload, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(MEM2)'");

          command = zo_command + " data-set view '" + ds + "(MEM2)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("LINEA");
          Expect(response).ToContain("LINEB");
        });

        it("should overwrite a PDSE member using BPAM", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(MEM3)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          std::string command_payload = "echo FIRSTDATA | " + zo_command + " data-set write '" + ds + "(MEM3)'";
          rc = execute_command_with_output(command_payload, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(MEM3)'");

          command_payload = "echo SECONDDATA | " + zo_command + " data-set write '" + ds + "(MEM3)'";
          rc = execute_command_with_output(command_payload, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(MEM3)'");

          command = zo_command + " data-set view '" + ds + "(MEM3)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("SECONDDATA");
          Expect(response).Not().ToContain("FIRSTDATA");
        });

        it("should warn when a PDSE member line exceeds LRECL", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY --lrecl 10 --blksize 80");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(MEM4)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          std::string long_line = "ABCDEFGHIJKLMNOPQRST";
          command = "echo " + long_line + " | " + zo_command + " data-set write '" + ds + "(MEM4)'";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Warning:");
          Expect(response).ToContain("truncated");
        });
      });

      describe("ASA control characters", [&]() -> void
      {
        it("should add ASA control characters for a PDS member", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(ASA2)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          command = "printf 'AAA\\nBBB\\n' | " + zo_command + " data-set write '" + ds +
                    "(ASA2)' --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(ASA2)'");

          command = zo_command + " data-set view '" + ds + "(ASA2)' --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(" AAA");
          Expect(response).ToContain(" BBB");
        });
        it("should add ASA control characters for FBA data sets", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PS --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = "printf 'AAA\\nBBB\\n' | " + zo_command + " data-set write " + ds +
                                " --local-encoding IBM-1047 --encoding IBM-1047";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "'");

          command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(" AAA");
          Expect(response).ToContain(" BBB");
        });

        it("should convert a single blank line into ASA double space", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PS --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = "printf 'AAA\\n\\nBBB\\n' | " + zo_command + " data-set write " + ds +
                                " --local-encoding IBM-1047 --encoding IBM-1047";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "'");

          command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(" AAA");
          Expect(response).ToContain("0BBB");
        });

        it("should handle multiple blank lines with ASA overflow", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PS --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = "printf 'AAA\\n\\n\\n\\nDDD\\n' | " + zo_command + " data-set write " + ds +
                                " --local-encoding IBM-1047 --encoding IBM-1047";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "'");

          command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(" AAA");
          Expect(response).ToContain("-");
          Expect(response).ToContain("\n DDD");
        });

        it("should convert form feed to ASA page break", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PS --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = "printf '\\fEEE\\n' | " + zo_command + " data-set write " + ds +
                                " --local-encoding IBM-1047 --encoding IBM-1047";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "'");

          command = zo_command + " data-set view " + ds + " --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("1EEE");
        });
        it("should read ASA VBA without RDW bytes", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PS --recfm VBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = "printf ' ABC\\n' | " + zo_command + " data-set write " + ds +
                                " --local-encoding IBM-1047 --encoding IBM-1047";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "'");

          command = zo_command + " data-set view " + ds + " --rfb --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);

          const size_t first_byte_pos = response.find_first_not_of(" \r\n\t");
          ExpectWithContext(first_byte_pos != std::string::npos, response).ToBe(true);
          std::string first_byte = response.substr(first_byte_pos, 2);
          Expect(first_byte).ToBe("40");
        });

        it("should stream ASA conversion to a member via pipe-path", [&]() -> void
        {
          std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 2 --dsntype LIBRARY --recfm FBA --lrecl 81 --blksize 810");

          std::string response;
          std::string command = zo_command + " data-set create-member '" + ds + "(ASA3)'";
          int rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Member created");

          const std::string pipe_path = "/tmp/zowex_ds_pipe_" + get_random_string(10);
          mkfifo(pipe_path.c_str(), 0777);

          const std::string payload = "AAA\n\nBBB\n";
          const auto encoded = zbase64::encode(payload.c_str(), payload.size());
          const std::string encoded_payload(encoded.begin(), encoded.end());

          std::thread writer = start_pipe_writer_thread(pipe_path, encoded_payload);

          command = zo_command + " data-set write '" + ds + "(ASA3)' --pipe-path " + pipe_path +
                    " --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          writer.join();
          unlink(pipe_path.c_str());

          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain("Wrote data to '" + ds + "(ASA3)'");

          command = zo_command + " data-set view '" + ds + "(ASA3)' --local-encoding IBM-1047 --encoding IBM-1047";
          rc = execute_command_with_output(command, response);
          ExpectWithContext(rc, response).ToBe(0);
          Expect(response).ToContain(" AAA");
          Expect(response).ToContain("0BBB");
        });
      });

      // TODO: What do?
      xit("should fail if the data set is deleted before the write operation is completed", []() -> void {});
      // TODO: What do?
      xit("should fail to write to a data set if not authorized", []() -> void {});

      describe("concurrent writes", [&]() -> void
      {
        TEST_OPTIONS concurrent_opts = {false, 60};

        // ENQ rname = DSNAME+MEMBER (member-level): different members don't conflict at ENQ.
        // RESERVE rname = DSNAME (PDSE-level): all concurrent BPAM writes to the same PDSE
        // compete for the same RESERVE regardless of member. EXCLUSIVE+RET=USE means the
        // holder gets RC=0; concurrent requestors get RC=4 immediately. At least one write
        // must succeed, and every member that was written must contain coherent data.
        it("should serialize concurrent BPAM writes to the same PDSE via RESERVE: even across different members",
           [&]() -> void
        {
          const std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 5 --dsntype LIBRARY");

          const int thread_count = 4;
          std::vector<std::thread> threads;
          std::vector<int> results(thread_count, -1);
          std::vector<std::string> errors(thread_count);

          for (int i = 0; i < thread_count; ++i)
          {
            threads.emplace_back([&, i]()
            {
              ZDS zds = {};
              const std::string data = "thread " + std::to_string(i) + " data";
              ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(MEM" + std::to_string(i) + ")"};
              results[i] = zds_write(write_opts, data);
              errors[i] = zds.diag.e_msg;
            });
          }

          for (auto &t : threads)
            t.join();

          // RESERVE (PDSE-level) serializes all BPAM writes: at least one must succeed
          int successes = 0;
          for (int i = 0; i < thread_count; ++i)
            if (results[i] == 0)
              ++successes;
          Expect(successes).ToBeGreaterThan(0);

          // Every member that was written must contain coherent data from its thread
          for (int i = 0; i < thread_count; ++i)
          {
            if (results[i] != 0)
              continue;
            ZDS read_zds = {};
            std::string content;
            ZDSReadOpts read_opts{.zds = &read_zds, .dsname = ds + "(MEM" + std::to_string(i) + ")"};
            ExpectWithContext(zds_read(read_opts, content), content).ToBe(0);
            Expect(content).ToContain("thread " + std::to_string(i) + " data");
          }
        }, concurrent_opts);

        // Same member = same ENQ rname. ENQ uses EXCLUSIVE+RET=USE: the holder gets RC=0,
        // concurrent requestors get RC=4 immediately (no wait). At least one write must
        // succeed, and the final content must come from exactly one write (no interleaving).
        it("should serialize concurrent writes to the same PDSE member via exclusive ENQ", [&]() -> void
        {
          const std::string ds = _ds.back();
          _create_ds(ds, "--dsorg PO --dirblk 5 --dsntype LIBRARY");

          const int thread_count = 4;
          std::vector<std::thread> threads;
          std::vector<int> results(thread_count, -1);

          for (int i = 0; i < thread_count; ++i)
          {
            threads.emplace_back([&, i]()
            {
              ZDS zds = {};
              const std::string data = "thread " + std::to_string(i) + " data";
              ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(SHARED)"};
              results[i] = zds_write(write_opts, data);
            });
          }

          for (auto &t : threads)
            t.join();

          // At least one write must succeed
          int successes = 0;
          for (int i = 0; i < thread_count; ++i)
            if (results[i] == 0)
              ++successes;
          Expect(successes).ToBeGreaterThan(0);

          // Content must be coherent: from exactly one thread, not interleaved
          ZDS read_zds = {};
          std::string content;
          ZDSReadOpts read_opts{.zds = &read_zds, .dsname = ds + "(SHARED)"};
          ExpectWithContext(zds_read(read_opts, content), content).ToBe(0);

          int matching_threads = 0;
          for (int i = 0; i < thread_count; ++i)
            if (content.find("thread " + std::to_string(i) + " data") != std::string::npos)
              ++matching_threads;
          Expect(matching_threads).ToBe(1);
        }, concurrent_opts);
      });
    });
    describe("resolve-alias", [&]() -> void
    {
      beforeEach([&]() -> void
      {
        _ds.push_back(get_random_ds());
      });
      it("should resolve an alias to a sequential data set", [&]() -> void
      {
        std::string target = _ds.back();
        _create_ds(target, "--dsorg PS");

        std::string alias_name = get_random_ds() + ".ALIAS";
        std::string makeAliasOutput;
        std::string makeAliasError;

        int rc = zds_idcams(" DEFINE ALIAS (NAME(" + alias_name + ") -\n RELATE(" + target + "))\n", makeAliasOutput,
                            makeAliasError);

        ExpectWithContext(rc, "Failed to create alias with rc " + std::to_string(rc) +
                                  ". Idcams output/error:" + makeAliasOutput + "\n" + makeAliasError)
            .ToBe(0);

        created_aliases.push_back(alias_name);
        std::string response;
        std::string command = zo_command + " data-set resolve-alias " + alias_name;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("'" + target + "'");
      });

      it("should resolve an alias to a PDS", [&]() -> void
      {
        std::string target = _ds.back();
        _create_ds(target, "--dsorg PO");

        std::string alias_name = get_random_ds() + ".ALIAS";
        std::string makeAliasOutput;
        std::string makeAliasError;

        int rc = zds_idcams(" DEFINE ALIAS (NAME(" + alias_name + ") -\n RELATE(" + target + "))\n", makeAliasOutput,
                            makeAliasError);

        ExpectWithContext(rc, "Failed to create alias with rc " + std::to_string(rc) +
                                  ". Idcams output/error:" + makeAliasOutput + "\n" + makeAliasError)
            .ToBe(0);

        created_aliases.push_back(alias_name);
        std::string response;
        std::string command = zo_command + " data-set resolve-alias " + alias_name;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("'" + target + "'");
      });

      it("should resolve an alias to a PDSE", [&]() -> void
      {
        std::string target = _ds.back();
        _create_ds(target, "--dsorg PO --dsntype LIBRARY --recfm F,B --dirblk 5");

        std::string alias_name = get_random_ds() + ".ALIAS";
        std::string makeAliasOutput;
        std::string makeAliasError;

        int rc = zds_idcams(" DEFINE ALIAS (NAME(" + alias_name + ") -\n RELATE(" + target + "))\n", makeAliasOutput,
                            makeAliasError);

        ExpectWithContext(rc, "Failed to create alias with rc " + std::to_string(rc) +
                                  ". Idcams output/error:" + makeAliasOutput + "\n" + makeAliasError)
            .ToBe(0);

        created_aliases.push_back(alias_name);
        std::string response;
        std::string command = zo_command + " data-set resolve-alias " + alias_name;
        rc = execute_command_with_output(command, response);
        ExpectWithContext(rc, response).ToBe(0);
        Expect(response).ToContain("'" + target + "'");
      });
    });

    afterAll([&]() -> void
    {
      std::string cleanup_idcams;
      std::string cleanup_err;
      std::string cleanup_output;
      for (const auto &alias : created_aliases)
      {
        cleanup_idcams += "  DELETE -\n " + alias + " -\n ALIAS\n";
      }
      cleanup_idcams += "  SET MAXCC = 0\n";
      int rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
      TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
      if (rc != 0)
      {
        throw std::runtime_error("Output and error from failed IDCAMS cleanup:" + cleanup_err + "\n" + cleanup_output +
                                 "\n");
      }
    });
  });
}
