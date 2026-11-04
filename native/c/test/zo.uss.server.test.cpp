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
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include "../zbase64.h"
#include "zutils.hpp"
#include "zo.server.test.hpp"
#include "zo.uss.server.test.hpp"

using namespace ztst;

// Create unique test directory to avoid conflicts between test runs
static const std::string ussTestDir = "/tmp/zowex_uss_srv_" + get_random_string(10);

void zo_uss_server_tests()
{
  ServerHandle server;
  describe("uss server tests", [&]() -> void
  {
    beforeAll([&]() -> void
    {
      server = start_server(zo_server_command, true);

      // Create test directory with full permissions for all test operations
      std::string response;
      execute_command_with_output(zo_command + " uss create-dir " + ussTestDir + " --mode 777", response);
    });

    afterAll([&]() -> void
    {
      stop_server(server);

      std::string response;
      execute_command_with_output(zo_command + " uss delete " + ussTestDir + " --recursive", response);
    });

    describe("chmod", [&]() -> void
    {
      std::string uss_path;

      beforeEach([&]() -> void
      {
        uss_path = get_random_uss(ussTestDir);
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + uss_path, response);
      });

      it("should properly chmod a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request =
            make_rpc_request("chmodFile", "{\"fspath\":\"" + uss_path + "\",\"mode\":\"777\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify permissions were actually set by checking ls output
        std::string ls_response;
        execute_command_with_output("ls -l " + uss_path, ls_response);
        Expect(ls_response).ToContain("-rwxrwxrwx");
      });
    });

    describe("chown", [&]() -> void
    {
      std::string uss_path;

      beforeEach([&]() -> void
      {
        uss_path = get_random_uss(ussTestDir);
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + uss_path, response);
      });

      it("should properly chown a file via RPC", [&]() -> void
      {
        // Get current user ID for chown operation
        std::string resp;
        execute_command_with_output("id -u", resp);
        resp.erase(resp.find_last_not_of(" \t\r\n") + 1); // trim whitespace

        int req_id;
        std::string request =
            make_rpc_request("chownFile", "{\"fspath\":\"" + uss_path + "\",\"owner\":\"" + resp + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);
        if (response.find("errno 139") != std::string::npos) // EPERM = 139
        {
          TestLog("WARNING: Chown operation failed with insufficient permissions");
          return;
        }

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify ownership was actually set by checking ls -l output
        std::string ls_response;
        execute_command_with_output("ls -l " + uss_path, ls_response);

        // ls -l shows the username, so we need to get it via id -un to compare
        std::string username_resp;
        execute_command_with_output("id -un", username_resp);
        username_resp.erase(username_resp.find_last_not_of(" \t\r\n") + 1); // trim whitespace

        ExpectWithContext(ls_response, "File should be owned by the user").ToContain(username_resp);
      });
    });

    describe("chtag", [&]() -> void
    {
      std::string uss_path;

      beforeEach([&]() -> void
      {
        uss_path = get_random_uss(ussTestDir);
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + uss_path, response);
      });

      it("should properly chtag a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request =
            make_rpc_request("chtagFile", "{\"fspath\":\"" + uss_path + "\",\"tag\":\"IBM-1047\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify tag was applied using ls -alT (shows encoding tags)
        std::string ls_response;
        execute_command_with_output("ls -alT " + uss_path, ls_response);
        Expect(ls_response).ToContain("IBM-1047");
      });
    });

    describe("copy", [&]() -> void
    {
      std::string src_path;
      std::string dest_path;

      beforeEach([&]() -> void
      {
        src_path = get_random_uss(ussTestDir) + "_src";
        dest_path = get_random_uss(ussTestDir) + "_dest";
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + src_path, response);

        // Populate the source file with some sample content via RPC writeFile
        std::string write_req =
            make_rpc_request("writeFile", "{\"fspath\":\"" + src_path + "\",\"data\":\"SGVsbG8gY29weSE=\"}");
        write_to_server(server, write_req);
        read_rpc_response(server); // consume response
      });

      it("should properly copy a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request(
            "copyUss", "{\"srcFsPath\":\"" + src_path + "\",\"dstFsPath\":\"" + dest_path + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify destination file exists after copy
        std::string ls_response;
        int rc = execute_command_with_output("ls " + dest_path, ls_response);
        Expect(rc).ToBe(0);

        // Verify the destination file contains the identical content via RPC readFile
        int read_id;
        std::string read_req = make_rpc_request("readFile", "{\"fspath\":\"" + dest_path + "\"}", read_id);
        write_to_server(server, read_req);
        std::string read_resp = read_rpc_response(server);

        Expect(read_resp).ToContain("\"success\":true");
        Expect(read_resp).ToContain("\"id\":" + std::to_string(read_id));
        Expect(read_resp).ToContain("\"SGVsbG8gY29weSE=\""); // same base64 data
      });
    });

    describe("create-dir", [&]() -> void
    {
      std::string dir_path;

      beforeEach([&]() -> void
      {
        dir_path = get_random_uss(ussTestDir) + "_newdir";
      });

      it("should properly create a directory via RPC", [&]() -> void
      {
        int req_id;
        std::string request =
            make_rpc_request("createFile", "{\"fspath\":\"" + dir_path + "\",\"isDir\":true}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify directory was created and has directory permissions
        std::string ls_response;
        int rc = execute_command_with_output("ls -ld " + dir_path, ls_response);
        Expect(rc).ToBe(0);
        Expect(ls_response).ToContain("drwx"); // directory prefix in permissions
      });
    });

    describe("create-file", [&]() -> void
    {
      std::string file_path;

      beforeEach([&]() -> void
      {
        file_path = get_random_uss(ussTestDir) + "_newfile";
      });

      it("should properly create a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request =
            make_rpc_request("createFile", "{\"fspath\":\"" + file_path + "\",\"isDir\":false}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        std::string ls_response;
        int rc = execute_command_with_output("ls -l " + file_path, ls_response);
        Expect(rc).ToBe(0);
      });
    });

    describe("delete", [&]() -> void
    {
      std::string file_path;

      beforeEach([&]() -> void
      {
        file_path = get_random_uss(ussTestDir) + "_delfile";
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + file_path, response);
      });

      it("should properly delete a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("deleteFile", "{\"fspath\":\"" + file_path + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify file no longer exists (ls should fail)
        std::string ls_response;
        int rc = execute_command_with_output("ls " + file_path, ls_response);
        Expect(rc).Not().ToBe(0);
      });
    });

    describe("issue", [&]() -> void
    {
      it("should properly issue a UNIX command via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("unixCommand", "{\"commandText\":\"echo hello\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"data\":\"hello\\n\"");
      });

      TEST_OPTIONS large_output_options = {.timeout_sec = 120};
      it("should JSON-escape large text output via RPC", [&]() -> void
      {
        const std::string big_path = get_random_uss(ussTestDir) + "_large_out";
        std::string out;
        int rc = execute_command_with_output(
            "awk 'BEGIN{for(i=0;i<3000000;i++)print \"a\\\"b\\\\c\"}' > '" + big_path + "'", out);
        ExpectWithContext(rc, out).ToBe(0);

        int req_id;
        std::string request = make_rpc_request("unixCommand", "{\"commandText\":\"cat '" + big_path + "'\"}", req_id);
        write_to_server(server, request);
        const std::string response = read_rpc_response(server, 60000);

        Expect(response.size()).ToBeGreaterThan(18000000);
        Expect(response.compare(response.size() - 2, 2, "}\n")).ToBe(0);
        Expect(response.find("\"id\":" + std::to_string(req_id))).Not().ToBe(std::string::npos);
        Expect(response.find("\"data\":\"a\\\"b\\\\c\\na\\\"b\\\\c\\n")).Not().ToBe(std::string::npos);
        Expect(response.find("a\"b\\c\n")).ToBe(std::string::npos);
      }, large_output_options);
    });

    describe("list", [&]() -> void
    {
      std::string dir_path;

      beforeEach([&]() -> void
      {
        dir_path = get_random_uss(ussTestDir) + "_listdir";
        std::string response;
        // Create test directory with a file inside for listing
        execute_command_with_output(zo_command + " uss create-dir " + dir_path, response);
        execute_command_with_output(zo_command + " uss create-file " + dir_path + "/testfile", response);
      });

      it("should properly list files via RPC", [&]() -> void
      {
        int req_id;
        std::string request = make_rpc_request("listFiles", "{\"fspath\":\"" + dir_path + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"testfile\"");
      });
    });

    describe("move", [&]() -> void
    {
      std::string src_path;
      std::string dest_path;

      beforeEach([&]() -> void
      {
        src_path = get_random_uss(ussTestDir) + "_mvsrc";
        dest_path = get_random_uss(ussTestDir) + "_mvdest";
        std::string response;
        execute_command_with_output(zo_command + " uss create-file " + src_path, response);

        // Populate the source file with some sample content via RPC writeFile
        std::string write_req =
            make_rpc_request("writeFile", "{\"fspath\":\"" + src_path + "\",\"data\":\"SGVsbG8gbW92ZSE=\"}");
        write_to_server(server, write_req);
        read_rpc_response(server); // consume response
      });

      it("should properly move a file via RPC", [&]() -> void
      {
        int req_id;
        std::string request =
            make_rpc_request("moveFile", "{\"source\":\"" + src_path + "\",\"target\":\"" + dest_path + "\"}", req_id);

        write_to_server(server, request);
        std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"success\":true");
        Expect(response).ToContain("\"id\":" + std::to_string(req_id));

        // Verify move: source should not exist, destination should exist
        std::string ls_response;
        int rc1 = execute_command_with_output("ls " + src_path, ls_response);
        Expect(rc1).Not().ToBe(0); // source file gone
        int rc2 = execute_command_with_output("ls " + dest_path, ls_response);
        Expect(rc2).ToBe(0); // destination file exists

        // Verify the destination file contains the identical content via RPC readFile
        int read_id;
        std::string read_req = make_rpc_request("readFile", "{\"fspath\":\"" + dest_path + "\"}", read_id);
        write_to_server(server, read_req);
        std::string read_resp = read_rpc_response(server);

        Expect(read_resp).ToContain("\"success\":true");
        Expect(read_resp).ToContain("\"id\":" + std::to_string(read_id));
        Expect(read_resp).ToContain("\"SGVsbG8gbW92ZSE=\""); // same base64 data
      });
    });

    describe("write and view", [&]() -> void
    {
      std::string file_path;

      beforeEach([&]() -> void
      {
        file_path = get_random_uss(ussTestDir) + "_wvfile";
      });

      it("should properly write and view a file via RPC", [&]() -> void
      {
        // Write operation: base64 encoded "Hello World!" is "SGVsbG8gV29ybGQh"
        int write_id;
        std::string write_req =
            make_rpc_request("writeFile", "{\"fspath\":\"" + file_path + "\",\"data\":\"SGVsbG8gV29ybGQh\"}", write_id);

        write_to_server(server, write_req);
        std::string write_resp = read_rpc_response(server);

        Expect(write_resp).ToContain("\"success\":true");
        Expect(write_resp).ToContain("\"id\":" + std::to_string(write_id));

        // Read operation: verify we get back the same base64 content
        int read_id;
        std::string read_req = make_rpc_request("readFile", "{\"fspath\":\"" + file_path + "\"}", read_id);

        write_to_server(server, read_req);
        std::string read_resp = read_rpc_response(server);

        Expect(read_resp).ToContain("\"success\":true");
        Expect(read_resp).ToContain("\"id\":" + std::to_string(read_id));
        Expect(read_resp).ToContain("\"SGVsbG8gV29ybGQh\""); // same base64 data
      });

      it("should reject an oversized etag via RPC", [&]() -> void
      {
        int request_id;
        const std::string oversized_etag(34, 'a');
        std::string request = make_rpc_request(
            "writeFile", "{\"fspath\":\"" + file_path + "\",\"data\":\"\",\"etag\":\"" + oversized_etag + "\"}",
            request_id);

        write_to_server(server, request);
        const std::string response = read_rpc_response(server);

        Expect(response).ToContain("\"id\":" + std::to_string(request_id));
        Expect(response).ToContain("etag exceeds 33 character length limit");
        Expect(response).Not().ToContain("\"success\":true");
      });
    });

    describe("streaming", [&]() -> void
    {
      std::string file_path;

      beforeEach([&]() -> void
      {
        file_path = get_random_uss(ussTestDir) + "_stream_file";
      });

      it("should write via stream", [&]() -> void
      {
        int stream_id = 300;

        const std::string payload = "Hello USS Stream\nLine 2 of USS\n";
        const auto encoded = zbase64::encode(payload.c_str(), payload.size());
        const std::string encoded_payload(encoded.begin(), encoded.end());

        std::string pipe_path;
        std::thread writer;

        int req_id;
        std::string request = make_rpc_request(
            "writeFile",
            "{\"fspath\":\"" + file_path + "\",\"stream\":" + std::to_string(stream_id) + ",\"encoding\":\"binary\"}",
            req_id);

        write_to_server(server, request);

        std::string notification = read_rpc_response(server);

        ExpectWithContext(notification, "Should be sendStream notification").ToContain("\"method\":\"sendStream\"");
        ExpectWithContext(notification, "Should have correct stream ID")
            .ToContain("\"id\":" + std::to_string(stream_id));

        size_t pipe_path_start = notification.find("\"pipePath\":\"") + 12;
        size_t pipe_path_end = notification.find("\"", pipe_path_start);
        pipe_path = notification.substr(pipe_path_start, pipe_path_end - pipe_path_start);

        writer = start_pipe_writer_thread(pipe_path, encoded_payload);

        std::string response = read_rpc_response(server);

        if (writer.joinable())
        {
          writer.join();
        }

        Expect(response).ToContain("\"id\":" + std::to_string(req_id));
        Expect(response).ToContain("\"success\":true");

        int read_id;
        std::string read_request = make_rpc_request("readFile", "{\"fspath\":\"" + file_path + "\"}", read_id);

        write_to_server(server, read_request);
        std::string read_response = read_rpc_response(server);

        Expect(read_response).ToContain("\"success\":true");
        ExpectWithContext(read_response, "Should contain streamed text").ToContain("SGVsbG8gVVNTIFN0cmVhbQ");
      });

      it("should read via stream", [&]() -> void
      {
        std::string test_content = "VVNTIFN0cmVhbSByZWFkIHRlc3QK"; // "USS Stream read test\n" base64
        std::string write_request =
            make_rpc_request("writeFile", "{\"fspath\":\"" + file_path + "\",\"data\":\"" + test_content + "\"}");

        write_to_server(server, write_request);
        std::string write_response = read_rpc_response(server);

        Expect(write_response).ToContain("\"success\":true");

        int stream_id = 400;

        int read_id;
        std::string read_request = make_rpc_request(
            "readFile", "{\"fspath\":\"" + file_path + "\",\"stream\":" + std::to_string(stream_id) + "}", read_id);

        write_to_server(server, read_request);

        std::string notification = read_rpc_response(server);
        Expect(notification).ToContain("\"method\":\"receiveStream\"");
        ExpectWithContext(notification, "Should have correct stream ID")
            .ToContain("\"id\":" + std::to_string(stream_id));

        size_t pipe_path_start = notification.find("\"pipePath\":\"") + 12;
        size_t pipe_path_end = notification.find("\"", pipe_path_start);
        std::string output_pipe = notification.substr(pipe_path_start, pipe_path_end - pipe_path_start);

        std::string file_content;
        std::thread reader = start_pipe_reader_thread(output_pipe, &file_content);

        std::string read_response = read_rpc_response(server);

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
