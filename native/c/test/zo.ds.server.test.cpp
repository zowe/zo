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
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <sys/stat.h>
#include <fcntl.h>
#include "zutils.hpp"
#include "../zbase64.h"
#include "zo.server.test.hpp"
#include "zo.ds.server.test.hpp"

using namespace ztst;

// Helper function to create unique data set names for testing
static std::string get_test_ds()
{
  return get_random_ds(3);
}

void zo_ds_server_tests()
{
  ServerHandle server;
  describe("data set server tests", [&]() -> void
  {
    beforeAll([&]() -> void
    {
      server = start_server(zo_server_command, true);
    });

    afterAll([&]() -> void
    {
      stop_server(server);
    });

    xdescribe("compress", [&]() -> void
    {
      // Skipped: compressDataset RPC method not implemented in server
    });

    // TODO: Enable once RPC passing is fixed
    xdescribe("copy", [&]() -> void
    {
      std::string src_ds;
      std::string dest_ds;

      beforeEach([&]() -> void
      {
        src_ds = get_test_ds() + ".SRC";
        dest_ds = get_test_ds() + ".DEST";

        // Create source data set
        std::string response;
        execute_command_with_output(zo_command + " ds create " + src_ds +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);

        // Add some content to the source dataset
        execute_command_with_output("echo 'Test data for copy' | " + zo_command + " ds write '" + src_ds + "(TESTMEM)'",
                                    response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + src_ds, response);
        execute_command_with_output(zo_command + " ds delete " + dest_ds, response);
      });

      it("should properly copy a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "copyDatasetOrMember", "{\"source\":\"" + src_ds + "\",\"target\":\"" + dest_ds + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify destination data set exists
        std::string ls_response;
        int rc = execute_command_with_output(zo_command + " ds list " + dest_ds, ls_response);
        Expect(rc).ToBe(0);

        // Verify the copied member exists
        int rc2 = execute_command_with_output(zo_command + " ds list-members '" + dest_ds + "'", ls_response);
        Expect(rc2).ToBe(0);
        Expect(ls_response).ToContain("TESTMEM");

        // Verify content was copied correctly
        std::string content_response;
        int rc3 = execute_command_with_output(zo_command + " ds view '" + dest_ds + "(TESTMEM)'", content_response);
        Expect(rc3).ToBe(0);
        Expect(content_response).ToContain("Test data for copy");
      });
    });

    describe("create", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "createDataset",
            "{\"dsname\":\"" + ds_name +
                "\",\"attributes\":{\"dsorg\":\"PO\",\"recfm\":\"FB\",\"lrecl\":80,\"primary\":5,\"dirblk\":25}}",
            req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify data set was created
        std::string ls_response;
        int rc = execute_command_with_output(zo_command + " ds list " + ds_name, ls_response);
        Expect(rc).ToBe(0);
      });
    });

    describe("create-adata", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create VB data set with defaults via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "createDataset",
            "{\"dsname\":\"" + ds_name +
                "\",\"attributes\":{\"dsorg\":\"PO\",\"recfm\":\"VB\",\"lrecl\":32756,\"primary\":5,\"dirblk\":25}}",
            req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
      });
    });

    describe("create-fb", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create FB data set with defaults via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "createDataset",
            "{\"dsname\":\"" + ds_name +
                "\",\"attributes\":{\"dsorg\":\"PO\",\"recfm\":\"FB\",\"lrecl\":80,\"primary\":5,\"dirblk\":25}}",
            req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
      });
    });

    describe("create-loadlib", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create loadlib data set with defaults via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "createDataset",
            "{\"dsname\":\"" + ds_name +
                "\",\"attributes\":{\"dsorg\":\"PO\",\"recfm\":\"U\",\"lrecl\":0,\"primary\":5,\"dirblk\":25}}",
            req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
      });
    });

    describe("create-member", [&]() -> void
    {
      std::string ds_name;
      std::string member_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
        member_name = "MEMBER1";

        // Create parent PDS first
        std::string response;
        execute_command_with_output(zo_command + " ds create " + ds_name +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create a member via RPC", [&]() -> void
      {
        std::string full_dsname = ds_name + "(" + member_name + ")";
        int req_id;
        std::string request = make_rpc_request("createMember", "{\"dsname\":\"" + full_dsname + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify member exists
        std::string ls_response;
        int rc = execute_command_with_output(zo_command + " ds list-members '" + ds_name + "'", ls_response);
        Expect(rc).ToBe(0);
        Expect(ls_response).ToContain(member_name);
      });
    });

    describe("create-vb", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly create VB data set with defaults via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "createDataset",
            "{\"dsname\":\"" + ds_name +
                "\",\"attributes\":{\"dsorg\":\"PO\",\"recfm\":\"VB\",\"lrecl\":255,\"primary\":5,\"dirblk\":25}}",
            req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
      });
    });

    describe("delete", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();

        // Create data set to delete
        std::string response;
        execute_command_with_output(zo_command + " ds create " + ds_name +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);
      });

      it("should properly delete a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("deleteDataset", "{\"dsname\":\"" + ds_name + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify data set no longer exists
        std::string ls_response;
        int rc = execute_command_with_output(zo_command + " ds list " + ds_name, ls_response);
        Expect(rc).Not().ToBe(0);
      });
    });

    describe("delete VSAM", [&]() -> void
    {
      it("should delete a VSAM cluster via RPC with default options", [&]() -> void
      {
        TestVsamGuard cluster;
        int req_id;
        const std::string request = make_rpc_request("deleteVsam", "{\"dsname\":\"" + cluster.name() + "\"}", req_id);
        write_to_server(server, request);
        const std::string response = read_rpc_response(server);
        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        std::string list_response;
        const int rc = execute_command_with_output(zo_command + " ds list '" + cluster.name() + "' --exact-match", list_response);
        ExpectWithContext(rc, list_response).Not().ToBe(0);
      });

      it("should return IDCAMS diagnostics when deletion fails", [&]() -> void
      {
        const std::string request = make_rpc_request("deleteVsam", "{\"dsname\":\"" + get_test_ds() + ".VSAM\"}");
        write_to_server(server, request);
        const std::string response = read_rpc_response(server);
        Expect(response).ToContain("\"error\"");
        Expect(response).ToContain("IDCAMS returned nonzero RC");
        Expect(response).ToContain("IDC3012I");
      });

      it("should reject missing names and non-boolean options", [&]() -> void
      {
        for (const std::string params : {"{}", "{\"dsname\":\"USER.VSAM\",\"erase\":\"true\"}", "{\"dsname\":\"USER.VSAM\",\"purge\":1}"})
        {
          write_to_server(server, make_rpc_request("deleteVsam", params));
          const std::string response = read_rpc_response(server);
          Expect(response).ToContain("\"error\"");
          Expect(response).ToContain("-32602");
        }
      });

      it("should reject empty names, wildcards, members and control statement injection", [&]() -> void
      {
        for (const std::string name : {"", "USER.*", "USER.VSAM(MEMBER)", "USER.VSAM'", "USER.VSAM\\n DELETE USER.OTHER CLUSTER PURGE"})
        {
          write_to_server(server, make_rpc_request("deleteVsam", "{\"dsname\":\"" + name + "\"}"));
          const std::string response = read_rpc_response(server);
          Expect(response).ToContain("\"error\"");
          Expect(response).ToContain("invalid VSAM cluster name");
        }
      });
    });

    describe("list", [&]() -> void
    {
      std::string ds_pattern;

      beforeEach([&]() -> void
      {
        ds_pattern = get_user() + ".TEST.*";
      });

      it("should properly list data sets via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("listDatasets", "{\"pattern\":\"" + ds_pattern + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"items\"");
      });

      it("should honor exactMatch via RPC", [&]() -> void
      {
        const std::string ds_name = get_test_ds();
        const std::string child_ds = ds_name + ".T00";

        std::string setup_response;
        execute_command_with_output(zo_command + " ds create " + ds_name, setup_response);
        execute_command_with_output(zo_command + " ds create " + child_ds, setup_response);

        int inclusive_id;
        std::string inclusive_request =
            make_rpc_request("listDatasets", "{\"pattern\":\"" + ds_name + "\"}", inclusive_id);
        write_to_server(server, inclusive_request);
        const std::string inclusive_response = read_rpc_response(server);

        int exact_id;
        std::string exact_request =
            make_rpc_request("listDatasets", "{\"pattern\":\"" + ds_name + "\",\"exactMatch\":true}", exact_id);
        write_to_server(server, exact_request);
        const std::string exact_response = read_rpc_response(server);

        // Clean up before asserting so a failure cannot leak data sets
        execute_command_with_output(zo_command + " ds delete " + child_ds, setup_response);
        execute_command_with_output(zo_command + " ds delete " + ds_name, setup_response);

        // Default: trailing ".**" is appended, so the descendant matches too
        Expect(inclusive_response).ToContain("\"success\":true");
        Expect(inclusive_response).ToContain(ds_name);
        Expect(inclusive_response).ToContain(child_ds);

        // exactMatch: the pattern is used verbatim
        Expect(exact_response).ToContain("\"success\":true");
        Expect(exact_response).ToContain("\"id\":" + std::to_string(exact_id));
        Expect(exact_response).ToContain(ds_name);
        Expect(exact_response).Not().ToContain(child_ds);
      });
    });

    describe("list-members", [&]() -> void
    {
      std::string ds_name;
      std::string member_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
        member_name = "TESTMEM";

        // Create PDS with a member
        std::string response;
        execute_command_with_output(zo_command + " ds create " + ds_name +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);
        execute_command_with_output(zo_command + " ds create-member '" + ds_name + "(" + member_name + ")'", response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly list data set members via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("listDsMembers", "{\"dsname\":\"" + ds_name + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"items\"");
        Expect(response).ToContain(member_name);
      });
    });

    describe("rename", [&]() -> void
    {
      std::string ds_before;
      std::string ds_after;

      beforeEach([&]() -> void
      {
        ds_before = get_test_ds() + ".OLD";
        ds_after = get_test_ds() + ".NEW";

        // Create data set to rename
        std::string response;
        execute_command_with_output(
            zo_command + " ds create " + ds_before + " --dsorg PO --recfm FB --lrecl 80 --primary 5", response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_before, response);
        execute_command_with_output(zo_command + " ds delete " + ds_after, response);
      });

      it("should properly rename a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "renameDataset", "{\"dsnameBefore\":\"" + ds_before + "\",\"dsnameAfter\":\"" + ds_after + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify old name doesn't exist, new name does
        std::string ls_response;
        int rc1 = execute_command_with_output(zo_command + " ds list " + ds_before, ls_response);
        Expect(rc1).Not().ToBe(0);
        int rc2 = execute_command_with_output(zo_command + " ds list " + ds_after, ls_response);
        Expect(rc2).ToBe(0);
      });
    });

    describe("rename-member", [&]() -> void
    {
      std::string ds_name;
      std::string member_before;
      std::string member_after;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();
        member_before = "OLDMEM";
        member_after = "NEWMEM";

        // Create PDS with a member
        std::string response;
        execute_command_with_output(zo_command + " ds create " + ds_name +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);
        execute_command_with_output(zo_command + " ds create-member '" + ds_name + "(" + member_before + ")'",
                                    response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly rename a member via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("renameMember",
                                               "{\"dsname\":\"" + ds_name + "\",\"memberBefore\":\"" + member_before +
                                                   "\",\"memberAfter\":\"" + member_after + "\"}",
                                               req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify old member doesn't exist, new member does
        std::string ls_response;
        execute_command_with_output(zo_command + " ds list-members '" + ds_name + "'", ls_response);
        Expect(ls_response).Not().ToContain(member_before);
        Expect(ls_response).ToContain(member_after);
      });
    });

    describe("restore", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();

        // Create and delete data set to test restore (assuming it gets migrated/recalled)
        std::string response;
        execute_command_with_output(zo_command + " ds create " + ds_name +
                                        " --dsorg PO --recfm FB --lrecl 80 --primary 5 --dirblk 25",
                                    response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly restore/recall a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("restoreDataset", "{\"dsname\":\"" + ds_name + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
      });
    });

    describe("view", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();

        // Create data set and write some content
        std::string response;
        execute_command_with_output(
            zo_command + " ds create " + ds_name + " --dsorg PS --recfm FB --lrecl 80 --primary 5", response);
        execute_command_with_output("echo 'Test content' | " + zo_command + " ds write " + ds_name, response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly view/read a data set via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("readDataset", "{\"dsname\":\"" + ds_name + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"data\"");
      });
    });

    describe("write and view", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();

        // Create data set for writing
        std::string response;
        execute_command_with_output(
            zo_command + " ds create " + ds_name + " --dsorg PS --recfm FB --lrecl 80 --primary 5", response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should properly write and read a data set via RPC", [&]() -> void
      {
        // Write operation: base64 encoded "Hello World!" is "SGVsbG8gV29ybGQh"
        int write_id;
        std::string write_req = make_rpc_request(
            "writeDataset", "{\"dsname\":\"" + ds_name + "\",\"data\":\"SGVsbG8gV29ybGQh\"}", write_id);

        write_to_server(server, write_req);
        std::string write_resp = read_rpc_response(server);

        Expect(write_resp).ToContain("\"success\":true");
        Expect(write_resp).ToContain("\"id\":" + std::to_string(write_id));

        // Read operation: verify we get back the same base64 content
        int read_id;
        std::string read_req = make_rpc_request("readDataset", "{\"dsname\":\"" + ds_name + "\"}", read_id);

        write_to_server(server, read_req);
        std::string read_resp = read_rpc_response(server);

        Expect(read_resp).ToContain("\"success\":true");
        Expect(read_resp).ToContain("\"id\":" + std::to_string(read_id));
        Expect(read_resp).ToContain("\"data\":");
        // Note: Data set may add trailing newline, so we check that data is returned
        // The returned base64 may be "SGVsbG8gV29ybGQhCg==" (with newline) instead of "SGVsbG8gV29ybGQh"
        ExpectWithContext(read_resp, "Should contain our written data").ToContain("SGVsbG8gV29ybGQ");
      });

      it("should reject an oversized etag via RPC", [&]() -> void
      {
        int request_id;
        std::string request = make_rpc_request(
            "writeDataset", "{\"dsname\":\"" + ds_name + "\",\"data\":\"\",\"etag\":\"123456789\"}", request_id);

        write_to_server(server, request);
        const std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"id\":" + std::to_string(request_id));
        Expect(response).ToContain("etag exceeds 8 character length limit");
        Expect(response).Not().ToContain("\"success\":true");
      });
    });

    describe("streaming", [&]() -> void
    {
      std::string ds_name;

      beforeEach([&]() -> void
      {
        ds_name = get_test_ds();

        // Create PS dataset for streaming test
        std::string response;
        execute_command_with_output(
            zo_command + " ds create " + ds_name + " --dsorg PS --recfm FB --lrecl 80 --primary 5", response);
      });

      afterEach([&]() -> void
      {
        std::string response;
        execute_command_with_output(zo_command + " ds delete " + ds_name, response);
      });

      it("should write via stream", [&]() -> void
      {
        int stream_id = 100;

        // Test data to stream - base64 encoded
        const std::string payload = "Hello Stream\nLine 2\n";
        const auto encoded = zbase64::encode(payload.c_str(), payload.size());
        const std::string encoded_payload(encoded.begin(), encoded.end());

        std::string pipe_path;
        std::thread writer;

        // RPC request with stream ID - set encoding to binary for simplicity
        int req_id;
        std::string request = make_rpc_request(
            "writeDataset",
            "{\"dsname\":\"" + ds_name + "\",\"stream\":" + std::to_string(stream_id) + ",\"encoding\":\"binary\"}",
            req_id);

        write_to_server(server, request);

        // Read the sendStream notification to get the actual pipe path
        std::string notification = read_rpc_response(server);

        ExpectWithContext(notification, "Should be sendStream notification").ToContain("\"method\":\"sendStream\"");
        ExpectWithContext(notification, "Should have correct stream ID")
            .ToContain("\"id\":" + std::to_string(stream_id));

        // Extract pipe path from notification
        size_t pipe_path_start = notification.find("\"pipePath\":\"") + 12;
        size_t pipe_path_end = notification.find("\"", pipe_path_start);
        pipe_path = notification.substr(pipe_path_start, pipe_path_end - pipe_path_start);

        // Start writer thread immediately
        writer = start_pipe_writer_thread(pipe_path, encoded_payload);

        // Read the actual RPC response
        std::string response = read_rpc_response(server);

        if (writer.joinable())
        {
          writer.join();
        }

        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"success\":true");

        // Verify the streamed data was written correctly
        int read_id;
        std::string read_request = make_rpc_request("readDataset", "{\"dsname\":\"" + ds_name + "\"}", read_id);

        write_to_server(server, read_request);
        std::string read_response = read_rpc_response(server);

        Expect(read_response).ToContain("\"success\":true");
        ExpectWithContext(read_response, "Should contain streamed text").ToContain("SGVsbG8");
      });

      it("should read via stream", [&]() -> void
      {
        std::string test_content = "U3RyZWFtIHJlYWQgdGVzdAo=";
        std::string write_request =
            make_rpc_request("writeDataset", "{\"dsname\":\"" + ds_name + "\",\"data\":\"" + test_content + "\"}");

        write_to_server(server, write_request);
        std::string write_response = read_rpc_response(server);

        Expect(write_response).ToContain("\"success\":true");

        int stream_id = 200;

        int read_id;
        std::string read_request = make_rpc_request(
            "readDataset", "{\"dsname\":\"" + ds_name + "\",\"stream\":" + std::to_string(stream_id) + "}", read_id);

        write_to_server(server, read_request);

        // Read the receiveStream notification to get the actual pipe path
        std::string notification = read_rpc_response(server);
        Expect(notification).ToContain("\"method\":\"receiveStream\"");
        ExpectWithContext(notification, "Should have correct stream ID")
            .ToContain("\"id\":" + std::to_string(stream_id));

        // Extract pipe path from notification
        size_t pipe_path_start = notification.find("\"pipePath\":\"") + 12;
        size_t pipe_path_end = notification.find("\"", pipe_path_start);
        std::string output_pipe = notification.substr(pipe_path_start, pipe_path_end - pipe_path_start);

        // Start reader thread immediately to prevent server from blocking
        std::string file_content;
        std::thread reader = start_pipe_reader_thread(output_pipe, &file_content);

        std::string read_response = read_rpc_response(server);

        // Wait for reader to complete
        reader.join();

        Expect(read_response).ToContain("\"id\":" + std::to_string(read_id));

        ExpectWithContext(file_content, "Streamed file should contain data").ToContain(test_content);

        ExpectWithContext(read_response, "Should be valid JSON-RPC response").ToContain("jsonrpc");

        ExpectWithContext(read_response, "Read streaming RPC interface should be supported")
            .ToContain("\"id\":" + std::to_string(read_id));
      });
    });
  });
}
