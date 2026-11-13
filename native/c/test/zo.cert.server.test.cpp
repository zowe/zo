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

// JSON-RPC layer tests for the certificate/key ring methods (test plan item 4,
// doc/certificates-test-plan.md). The service layer is covered by zkr.test.cpp;
// these assert what only the RPC path can break: request schema validation,
// camelCase->kebab-case parameter mapping, response schema validation (the
// server 500s a success response that violates its schema), and the error
// shape. Read-only calls are gated on a CLI probe of the caller's authority so
// the suite passes for users without ESM certificate access.

#include "ztest.hpp"
#include <string>
#include "zutils.hpp"
#include "zo.server.test.hpp"
#include "zo.cert.server.test.hpp"

using namespace ztst;

void zo_cert_server_tests()
{
  ServerHandle server;
  describe("certificate server tests", [&]() -> void
  {
    beforeAll([&]() -> void
    {
      server = start_server(zo_server_command, true);
    });

    afterAll([&]() -> void
    {
      stop_server(server);
    });

    describe("request validation (no authority required)", [&]() -> void
    {
      it("rejects createKeyring without the required keyring field", [&]() -> void
      {
        write_to_server(server, make_rpc_request("createKeyring", "{\"owner\":\"TESTUSER\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).ToContain("Request validation failed");
        Expect(response).Not().ToContain("\"success\":true");
      });

      it("rejects deleteCertificate without the required label field", [&]() -> void
      {
        write_to_server(server,
                        make_rpc_request("deleteCertificate", "{\"owner\":\"TESTUSER\",\"keyring\":\"RING01\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).ToContain("Request validation failed");
      });

      it("rejects trustCertificate without the required status field", [&]() -> void
      {
        write_to_server(server, make_rpc_request("trustCertificate", "{\"owner\":\"TESTUSER\",\"label\":\"LBL\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).ToContain("Request validation failed");
      });

      it("accepts unknown request fields for forward compatibility", [&]() -> void
      {
        // Validation must not reject the extra field; execution may still fail
        // on authority, so only assert the failure is NOT a validation error.
        write_to_server(server,
                        make_rpc_request("listRings", "{\"owner\":\"" + get_user() + "\",\"futureField\":true}"));
        std::string response = read_rpc_response(server);
        Expect(response).Not().ToContain("Request validation failed");
      });

      it("rejects exportCertificate p12 without a password at the handler level", [&]() -> void
      {
        // The request schema allows an omitted password (pem exports don't need
        // one), so this is caught by handle_cert_export, not schema validation.
        write_to_server(server, make_rpc_request("exportCertificate",
                                                 "{\"owner\":\"TESTUSER\",\"keyring\":\"RING01\",\"label\":\"LBL\","
                                                 "\"format\":\"p12\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).ToContain("password");
        Expect(response).Not().ToContain("\"success\":true");
        Expect(response).Not().ToContain("Request validation failed");
      });

      it("accepts importCertificate with dsn and no file at the schema layer", [&]() -> void {
        // `file` became FIELD_OPTIONAL so a dsn-only import is not rejected by
        // schema validation; it is expected to fail later (handler/ESM), but not
        // with "Request validation failed".
        write_to_server(server, make_rpc_request("importCertificate",
                                                 "{\"owner\":\"TESTUSER\",\"keyring\":\"RING01\",\"label\":\"LBL\","
                                                 "\"usage\":\"PERSONAL\",\"dsn\":\"TESTUSER.NOPE.P12\","
                                                 "\"password\":\"secret\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).Not().ToContain("Request validation failed");
      });
    });

    describe("read-only methods (gated on ESM authority)", [&]() -> void
    {
      // Probe with the CLI, matching each RPC's underlying service so the gate
      // tracks AUTHORITY, not data: GetRingInfo (list-rings) fails for a user
      // who simply has no rings even with full authority, while the
      // virtual-ring enumeration (list/count on "*") succeeds empty -- so the
      // two families need separate probes.
      const std::string user = get_user();
      std::string probe_out;
      const bool can_read_rings =
          execute_command_with_output(zo_command + " system keyring list-rings " + user, probe_out) == 0;
      const bool can_read_certs =
          execute_command_with_output(zo_command + " system keyring list " + user + " '*' --max-entries 1",
                                      probe_out) == 0;

      it("listRings responds with the documented shape", [&]() -> void
      {
        write_to_server(server, make_rpc_request("listRings", "{\"owner\":\"" + user + "\"}"));
        std::string response = read_rpc_response(server);
        if (can_read_rings)
        {
          // A success response has also passed server-side response validation.
          Expect(response).ToContain("\"success\":true");
          Expect(response).ToContain("\"returnedRows\"");
          Expect(response).ToContain("\"items\"");
        }
        else
        {
          Expect(response).ToContain("\"error\"");
        }
      });

      it("listCertificates on the virtual ring maps camelCase params", [&]() -> void
      {
        // maxEntries/labelOnly arrive camelCase and must reach the handler as
        // max-entries/label-only via the dispatcher's key conversion.
        write_to_server(server, make_rpc_request("listCertificates",
                                                 "{\"owner\":\"" + user +
                                                     "\",\"keyring\":\"*\",\"maxEntries\":1,\"labelOnly\":true}"));
        std::string response = read_rpc_response(server);
        if (can_read_certs)
        {
          Expect(response).ToContain("\"success\":true");
          Expect(response).ToContain("\"moreAvailable\"");
        }
        else
        {
          Expect(response).ToContain("\"error\"");
        }
      });

      it("countRing returns a numeric count for the virtual ring", [&]() -> void
      {
        write_to_server(server, make_rpc_request("countRing", "{\"owner\":\"" + user + "\",\"keyring\":\"*\"}"));
        std::string response = read_rpc_response(server);
        if (can_read_certs)
        {
          Expect(response).ToContain("\"success\":true");
          Expect(response).ToContain("\"count\"");
        }
        else
        {
          Expect(response).ToContain("\"error\"");
        }
      });
    });

    describe("structured error payload (no authority required)", [&]() -> void
    {
      it("deleteCertificate surfaces safReturns in the JSON-RPC error data", [&]() -> void
      {
        // R_datalib rejects a nonexistent owner/ring/label combination for any
        // caller, regardless of the caller's own ESM authority, so this SAF
        // failure -- and the structured error object the handler attaches via
        // set_error_object()/report_error() -- is guaranteed without a gate.
        write_to_server(server,
                        make_rpc_request("deleteCertificate",
                                         "{\"owner\":\"ZZNOSUCH\",\"keyring\":\"NORING\",\"label\":\"NOLABEL\"}"));
        std::string response = read_rpc_response(server);
        Expect(response).ToContain("\"error\"");
        Expect(response).ToContain("\"safReturns\"");
        Expect(response).Not().ToContain("\"success\":true");
      });
    });
  });
}
