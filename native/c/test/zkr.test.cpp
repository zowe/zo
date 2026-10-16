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

// Unit tests for the `system cert` / `system keyring` command group (zkr service
// layer + CLI). Certificate and key ring operations require ESM authority that
// not every build user has, so the mutating tests are gated behind a runtime
// capability probe (see `can_mutate` below): where authority is missing they are
// SKIPPED, not failed, so the suite passes across a variety of user contexts.
// The command-validation and diagnostic tests need no special authority.
//
// R_datalib background and the SAF return-code conventions used here are
// documented inline in zkr.cpp/zkrtype.h (with references to z/OS Security
// Server RACF Callable Services, SA23-2293); keep assertions in sync with them.

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "ztest.hpp"
#include "zutils.hpp"
#include "../zds.hpp"
#include "../zkr.hpp"
#include "../zkrtype.h"
#include "../zut.hpp"

using namespace ztst;

namespace
{

// A collision-resistant suffix for scratch ring/label names, capped at 8 chars.
// Random() is fine in a normal test binary (unlike the workflow sandbox); combine
// it with the pid so concurrent test runs on the same LPAR do not clash.
std::string zkr_unique()
{
  std::string pid = std::to_string(getpid());
  if (pid.size() > 4)
  {
    pid = pid.substr(pid.size() - 4);
  }
  return pid + get_random_string(4);
}

// Delete a scratch ring, swallowing any error (best-effort cleanup).
void zkr_try_del_ring(const std::string &owner, const std::string &ring)
{
  ZKR z{};
  zkr_del_ring(&z, owner, ring);
}

// Purge a scratch certificate from the ESM database by label, swallowing errors.
void zkr_try_purge_cert(const std::string &owner, const std::string &label)
{
  ZKR z{};
  zkr_del_cert(&z, owner, "*", label, /*skip_refresh*/ true);
}

// Build a synthetic list entry for the zkr_filter_certs unit tests.
ZKRCertInfo zkr_mk_cert(const std::string &label, const std::string &usage)
{
  ZKRCertInfo c;
  c.label = label;
  c.owner = "TESTUSER";
  c.usage = usage;
  c.status = "TRUST";
  return c;
}

// Read an env var that may arrive either under its own name or with the ZNP_
// prefix (buildTools.ts forwards local ZNP_* variables to the remote runner
// verbatim, prefix included).
std::string zkr_env(const char *name, const char *znp_name)
{
  const char *val = std::getenv(name);
  if (val == nullptr)
    val = std::getenv(znp_name);
  return val != nullptr ? val : "";
}

// Self-provision a throwaway PKCS#12 fixture via RACDCERT (see
// doc/certificates-test-plan.md, item 2): GENCERT a scratch certificate,
// EXPORT it as PKCS12DER to a temporary dataset, DELETE the ESM record (so a
// later import sees a brand-new certificate), then binary-copy the dataset to
// a USS temp file. Returns the USS path, or "" with `note` set when any step
// fails (typically missing IRR.DIGTCERT.GENCERT/EXPORT authority) -- callers
// then skip, exactly as with a missing ZKR_TEST_P12.
std::string zkr_generate_p12_fixture(const std::string &owner, const std::string &password, std::string &note)
{
  // Dataset-qualifier-safe uniqueness (max 8 chars, must start alphabetic).
  const std::string uniq = "Z" + get_random_string(6);
  const std::string label = "ZKRUTFIX" + uniq;
  const std::string dsn = owner + ".ZKRUT." + uniq + ".P12";
  const std::string uss = "/tmp/zkrut-" + uniq + ".p12";

  std::string out;
  int rc = execute_command_with_output(
      "tsocmd \"RACDCERT GENCERT SUBJECTSDN(CN('ZKRUT FIXTURE')) WITHLABEL('" + label + "') SIZE(2048)\"", out);
  if (rc != 0)
  {
    note = "RACDCERT GENCERT failed: " + out;
    return "";
  }

  rc = execute_command_with_output("tsocmd \"RACDCERT EXPORT (LABEL('" + label + "')) DSN('" + dsn +
                                       "') FORMAT(PKCS12DER) PASSWORD('" + password + "')\"",
                                   out);

  // The generated record is no longer needed regardless of the export result.
  std::string ignored;
  execute_command_with_output("tsocmd \"RACDCERT DELETE (LABEL('" + label + "'))\"", ignored);

  if (rc != 0)
  {
    note = "RACDCERT EXPORT failed: " + out;
    return "";
  }

  rc = execute_command_with_output("cp -B \"//'" + dsn + "'\" " + uss, out);
  execute_command_with_output("tsocmd \"DELETE '" + dsn + "'\"", ignored);
  if (rc != 0)
  {
    note = "binary copy of the exported dataset failed: " + out;
    return "";
  }
  return uss;
}

} // namespace

void zkr_tests()
{
  describe("certificates (system cert / keyring)", [&]() -> void
  {
    const std::string owner = get_user();

    // ---------------------------------------------------------------------
    // CLI input validation. These exercise the argument/virtual-ring policy
    // in commands/certificates.cpp, which rejects bad input BEFORE any ESM
    // call, so they need no key ring authority and are fully deterministic.
    // ---------------------------------------------------------------------
    describe(
        "CLI input validation (no authority required)",
        [&]() -> void
    {
      it("`system cert --help` lists the certificate subcommands",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(zo_command + " system cert --help", out);
        ExpectWithContext(rc, out).ToBe(0);
        Expect(out).ToContain("import");
        Expect(out).ToContain("connect");
        Expect(out).ToContain("set-default");
        Expect(out).ToContain("rename");
      });

      it("`system keyring --help` lists the key ring subcommands",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(zo_command + " system keyring --help", out);
        ExpectWithContext(rc, out).ToBe(0);
        Expect(out).ToContain("create");
        Expect(out).ToContain("list-rings");
        Expect(out).ToContain("count");
      });

      it("`cert delete` rejects the virtual ring '*' and points to --database",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert delete TESTUSER '*' -l LBL", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--database");
      });

      it("`cert delete` requires a key ring or --database",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert delete TESTUSER -l LBL", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--database");
      });

      it("`cert delete` rejects a key ring and --database together",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert delete TESTUSER RING01 -l LBL --database", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("not both");
      });

      it("`cert connect` rejects '*' for --from-ring and points to --from-database",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert connect TESTUSER RING02 -l LBL --from-ring '*'", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--from-database");
      });

      it("`cert connect` requires --from-ring or --from-database",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert connect TESTUSER RING02 -l LBL", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--from-ring");
      });

      it("`cert export -F p12` requires --password",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert export TESTUSER RING01 -l LBL -F p12 -f /tmp/ignored.p12",
            out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--password is required");
      });

      it("`cert export -F p12` with neither --file nor --dsn requires one of them",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert export TESTUSER RING01 -l LBL -F p12 -p secret", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--dsn");
      });

      it("`cert export` rejects --file and --dsn together",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert export TESTUSER RING01 -l LBL -f /tmp/a.pem --dsn A.B.C", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("not both");
      });

      it("`cert import` rejects --file and --dsn together",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert import TESTUSER RING01 -l LBL -u PERSONAL -p secret "
                         "-f /tmp/a.p12 --dsn A.B.C",
            out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("not both");
      });

      it("`cert import` requires --file or --dsn",
         []() -> void
      {
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert import TESTUSER RING01 -l LBL -u PERSONAL -p secret", out);
        Expect(rc).Not().ToBe(0);
        Expect(out).ToContain("--dsn");
      });
    });

    // ---------------------------------------------------------------------
    // List filter semantics: `keyring list --label/--usage` follows the
    // RACDCERT LABEL keyword -- exact, case-sensitive matches, no wildcards
    // -- and --max-entries caps MATCHING rows, so a filtered list must scan
    // past the page size. Pure functions over synthetic data; no ESM.
    // ---------------------------------------------------------------------
    describe(
        "list filter semantics (RACDCERT LABEL parity, no authority required)",
        [&]() -> void
    {
      it("finds an exact label match beyond the default page size",
         []() -> void
      {
        // 12 certs; the target sits at position 11 -- past the default
        // --max-entries page of 10 that used to truncate before filtering.
        std::vector<ZKRCertInfo> certs;
        for (int i = 0; i < 12; ++i)
          certs.push_back(zkr_mk_cert("CERT" + std::to_string(i), "PERSONAL"));

        bool more = true;
        const std::vector<ZKRCertInfo> out = zkr_filter_certs(certs, "CERT10", "", 10, &more);
        Expect(out.size()).ToBe(static_cast<size_t>(1));
        Expect(out[0].label).ToBe("CERT10");
        Expect(more).ToBe(false);
      });

      it("label matching is case-sensitive",
         []() -> void
      {
        std::vector<ZKRCertInfo> certs;
        certs.push_back(zkr_mk_cert("MYCERT", "PERSONAL"));

        bool more = false;
        Expect(zkr_filter_certs(certs, "MyCert", "", 0, &more).size()).ToBe(static_cast<size_t>(0));
        Expect(zkr_filter_certs(certs, "MYCERT", "", 0, &more).size()).ToBe(static_cast<size_t>(1));
      });

      it("'*' is not a wildcard for labels",
         []() -> void
      {
        std::vector<ZKRCertInfo> certs;
        certs.push_back(zkr_mk_cert("CERT1", "PERSONAL"));
        certs.push_back(zkr_mk_cert("*", "PERSONAL"));

        const std::vector<ZKRCertInfo> out = zkr_filter_certs(certs, "*", "", 0, nullptr);
        Expect(out.size()).ToBe(static_cast<size_t>(1));
        Expect(out[0].label).ToBe("*");
      });

      it("--max-entries caps matching rows and flags more",
         []() -> void
      {
        // 30 certs alternating usage: 15 CERTAUTH among them.
        std::vector<ZKRCertInfo> certs;
        for (int i = 0; i < 30; ++i)
          certs.push_back(zkr_mk_cert("CERT" + std::to_string(i),
                                      (i % 2 == 0) ? "CERTAUTH" : "PERSONAL"));

        bool more = false;
        const std::vector<ZKRCertInfo> out = zkr_filter_certs(certs, "", "CERTAUTH", 10, &more);
        Expect(out.size()).ToBe(static_cast<size_t>(10));
        Expect(more).ToBe(true);
        for (size_t i = 0; i < out.size(); ++i)
          Expect(out[i].usage).ToBe("CERTAUTH");
      });

      it("--max-entries 0 returns all matches",
         []() -> void
      {
        std::vector<ZKRCertInfo> certs;
        for (int i = 0; i < 30; ++i)
          certs.push_back(zkr_mk_cert("CERT" + std::to_string(i),
                                      (i % 2 == 0) ? "CERTAUTH" : "PERSONAL"));

        bool more = true;
        Expect(zkr_filter_certs(certs, "", "CERTAUTH", 0, &more).size()).ToBe(static_cast<size_t>(15));
        Expect(more).ToBe(false);
      });

      it("without filters the cap applies to raw entries",
         []() -> void
      {
        std::vector<ZKRCertInfo> certs;
        for (int i = 0; i < 12; ++i)
          certs.push_back(zkr_mk_cert("CERT" + std::to_string(i), "PERSONAL"));

        bool more = false;
        Expect(zkr_filter_certs(certs, "", "", 10, &more).size()).ToBe(static_cast<size_t>(10));
        Expect(more).ToBe(true);
      });

      it("label and usage filters combine with AND",
         []() -> void
      {
        std::vector<ZKRCertInfo> certs;
        certs.push_back(zkr_mk_cert("CERT1", "PERSONAL"));
        certs.push_back(zkr_mk_cert("CERT1", "CERTAUTH"));
        certs.push_back(zkr_mk_cert("CERT2", "CERTAUTH"));

        const std::vector<ZKRCertInfo> out = zkr_filter_certs(certs, "CERT1", "CERTAUTH", 0, nullptr);
        Expect(out.size()).ToBe(static_cast<size_t>(1));
        Expect(out[0].label).ToBe("CERT1");
        Expect(out[0].usage).ToBe("CERTAUTH");
      });
    });

    // ---------------------------------------------------------------------
    // Service-layer linkage + diagnostics. The probe both proves the binary
    // links IRRSDL64/GSKCMS and determines whether this user may create key
    // rings; `can_mutate` gates the lifecycle suites below.
    // ---------------------------------------------------------------------
    bool can_mutate = false;
    std::string probe_note;

    describe(
        "service diagnostics and linkage (no authority required)",
        [&]() -> void
    {
      it("links R_datalib and probes key ring create authority",
         [&]() -> void
      {
        ZKR z{};
        const std::string ring = "ZKRUT.PROBE." + zkr_unique();
        int rc = zkr_new_ring(&z, owner, ring);
        if (rc == 0)
        {
          can_mutate = true;
          // If we can create, we must be able to clean up.
          ExpectWithContext(zkr_del_ring(&z, owner, ring), z.diag.e_msg).ToBe(0);
        }
        else
        {
          // Not authorized (or otherwise unavailable): the failure must be
          // a clean SAF diagnostic (>= 8), not a crash or empty message.
          probe_note = z.diag.e_msg;
          Expect(z.diag.e_msg.empty()).ToBe(false);
          Expect(z.diag.saf_rc).ToBeGreaterThanOrEqualTo(8);
        }
        TestLog(can_mutate ? "key ring authority: AVAILABLE"
                           : "key ring authority: DENIED (" + probe_note + ")");
      });

      it("reports a clean diagnostic for a missing key ring",
         [&]() -> void
      {
        ZKR z{};
        std::vector<ZKRCertInfo> certs;
        const std::string ring = "ZKRUT.NOPE." + zkr_unique();
        int rc = zkr_list_ring(&z, owner, ring, certs);
        Expect(rc).Not().ToBe(0);
        Expect(z.diag.e_msg.empty()).ToBe(false);
        // The failing service is the DataGetFirst (GETCERT) enumeration.
        Expect(static_cast<int>(z.diag.function_code)).ToBe(static_cast<int>(ZKR_GETCERT_CODE));
      });

      it("reads the caller's virtual key ring without crashing",
         [&]() -> void
      {
        ZKR z{};
        std::vector<ZKRCertInfo> certs;
        // "*" is the virtual ring (all of the caller's certificates). It may
        // succeed (possibly empty) or be denied; either way it must return a
        // coherent result with a populated diagnostic on failure.
        int rc = zkr_list_ring(&z, owner, "*", certs);
        if (rc != 0)
          Expect(z.diag.e_msg.empty()).ToBe(false);
      });
    });

    // ---------------------------------------------------------------------
    // Export option validation. gsk_export_key rejects an empty password
    // (z/OS System SSL Programming, SC14-7495), so a PKCS#12 export without
    // one must fail fast, before any key ring is even opened. No authority
    // needed.
    // ---------------------------------------------------------------------
    describe(
        "export option validation (no authority required)",
        [&]() -> void
    {
      it("rejects a p12 export with an empty password before touching the key ring",
         []() -> void
      {
        ZKRExportOptions ex;
        ex.owner = "TESTUSER";
        ex.ring = "RING01";
        ex.label = "ANYLABEL";
        ex.format = "p12";
        ex.password = "";

        ZKR z{};
        std::string data;
        Expect(zkr_export_cert(&z, ex, data)).Not().ToBe(0);
        Expect(z.diag.e_msg).ToContain("passphrase");
      });
    });

    // ---------------------------------------------------------------------
    // Import option validation. zkr_import_cert validates label/usage and
    // loads the PKCS#12 file from disk before ever touching the key ring, so
    // an empty source file must fail fast, before any SAF/GSK call. No
    // authority needed.
    // ---------------------------------------------------------------------
    describe(
        "import option validation (no authority required)",
        [&]() -> void
    {
      it("rejects an empty PKCS#12 file before touching the key ring",
         []() -> void
      {
        const std::string path = "/tmp/zkrut-empty-" + zkr_unique() + ".p12";
        std::ofstream f(path.c_str());
        f.close();

        ZKRImportOptions opts;
        opts.owner = "TESTUSER";
        opts.ring = "RING01";
        opts.label = "ANYLABEL";
        opts.usage = "PERSONAL";
        opts.p12_path = path;
        opts.password = "x";

        ZKR z{};
        Expect(zkr_import_cert(&z, opts)).Not().ToBe(0);
        Expect(z.diag.e_msg).ToContain("empty");

        unlink(path.c_str());
      });
    });

    // ---------------------------------------------------------------------
    // Key ring lifecycle. Requires authority to create key rings; skipped
    // (not failed) otherwise.
    // ---------------------------------------------------------------------
    describe(
        "key ring lifecycle (requires create authority)",
        [&]() -> void
    {
      itif(
          "creates a key ring, lists it empty, enumerates it, and deletes it",
          [&]() -> void
      {
        ZKR z{};
        const std::string ring = "ZKRUT.LIFE." + zkr_unique();
        ExpectWithContext(zkr_new_ring(&z, owner, ring), z.diag.e_msg).ToBe(0);

        // A brand-new ring has no certificates (empty list, not an error).
        std::vector<ZKRCertInfo> certs;
        ExpectWithContext(zkr_list_ring(&z, owner, ring, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBe(static_cast<size_t>(0));

        // GetRingInfo enumeration should succeed for the owner's rings.
        std::vector<ZKRRingEntry> rings;
        ExpectWithContext(zkr_list_rings(&z, owner, ring, rings), z.diag.e_msg).ToBe(0);

        ExpectWithContext(zkr_del_ring(&z, owner, ring), z.diag.e_msg).ToBe(0);
      },
          can_mutate);

      itif(
          "deleting a nonexistent key ring reports a clean error",
          [&]() -> void
      {
        ZKR z{};
        const std::string ring = "ZKRUT.GHOST." + zkr_unique();
        int rc = zkr_del_ring(&z, owner, ring);
        Expect(rc).Not().ToBe(0);
        Expect(z.diag.e_msg.empty()).ToBe(false);
      },
          can_mutate);
    });

    // ---------------------------------------------------------------------
    // Certificate lifecycle. Requires create authority AND a PKCS#12
    // fixture. The fixture comes from ZKR_TEST_P12 / ZKR_TEST_P12_PASS
    // when set (no key material is committed to the repo); otherwise the
    // suite self-provisions a throwaway one via RACDCERT (GENCERT ->
    // EXPORT -> DELETE) and removes it afterwards. Skipped when neither
    // path yields a fixture (e.g. no GENCERT/EXPORT authority).
    // This asserts the new service entry points wire up end-to-end; deep
    // PEM/PKCS#12 round-trip parity (byte-for-byte against keyring-util
    // output) is NOT covered by automated tests yet and must be verified
    // manually when the export/import paths change.
    // ---------------------------------------------------------------------
    describe(
        "certificate lifecycle (requires authority + a PKCS#12 fixture)",
        [&]() -> void
    {
      std::string p12 = zkr_env("ZKR_TEST_P12", "ZNP_ZKR_TEST_P12");
      std::string p12pass = zkr_env("ZKR_TEST_P12_PASS", "ZNP_ZKR_TEST_P12_PASS");
      if (p12pass.empty())
        p12pass = "password";
      bool have_fixture = !p12.empty() && access(p12.c_str(), R_OK) == 0;

      // No curated fixture: self-provision one when the user has the
      // authority for it (static so the afterAll hook, which runs after
      // this lambda's locals are gone, can still remove the USS file).
      static std::string generated_p12;
      if (!have_fixture && can_mutate)
      {
        std::string note;
        const std::string gen_pass = "ZKRUT" + get_random_string(8);
        generated_p12 = zkr_generate_p12_fixture(owner, gen_pass, note);
        if (!generated_p12.empty())
        {
          p12 = generated_p12;
          p12pass = gen_pass;
          have_fixture = true;
          TestLog("PKCS#12 fixture: self-provisioned via RACDCERT at " + generated_p12);
        }
        else
        {
          TestLog("PKCS#12 fixture: could not self-provision (" + note + "); lifecycle tests will skip");
        }
      }
      const bool run_cert = can_mutate && have_fixture;

      // Cleanup registers: populated up front so a mid-flow failure/timeout
      // still tears down the scratch rings and any stray DB certificate.
      static std::vector<std::string> cleanup_rings;
      static std::vector<std::string> cleanup_labels;
      static std::vector<std::string> cleanup_dsns;
      static std::string exported_p12;
      afterAll(
          [&]() -> void
      {
        for (const auto &l : cleanup_labels)
          zkr_try_purge_cert(owner, l);
        for (const auto &r : cleanup_rings)
          zkr_try_del_ring(owner, r);
        for (const auto &d : cleanup_dsns)
        {
          ZDS zds{};
          zds_delete_dsn(&zds, d);
        }
        cleanup_rings.clear();
        cleanup_labels.clear();
        cleanup_dsns.clear();
        if (!generated_p12.empty())
        {
          unlink(generated_p12.c_str());
          generated_p12.clear();
        }
        if (!exported_p12.empty())
        {
          unlink(exported_p12.c_str());
          exported_p12.clear();
        }
      });

      // The full sweep issues several DIGTCERT refreshes (import/delete),
      // which are slow; the 10s default is not enough.
      TEST_OPTIONS cert_opts = {false, 120};

      itif(
          "imports, shows, exports, alters, connects, and deletes a certificate",
          [&]() -> void
      {
        const std::string ring1 = "ZKRUT.CRT1." + zkr_unique();
        const std::string ring2 = "ZKRUT.CRT2." + zkr_unique();
        const std::string label = "ZKRUT" + zkr_unique();
        cleanup_rings.push_back(ring1);
        cleanup_rings.push_back(ring2);
        cleanup_labels.push_back(label);

        ZKR z{};
        ExpectWithContext(zkr_new_ring(&z, owner, ring1), z.diag.e_msg).ToBe(0);
        ExpectWithContext(zkr_new_ring(&z, owner, ring2), z.diag.e_msg).ToBe(0);

        ZKRImportOptions imp;
        imp.owner = owner;
        imp.ring = ring1;
        imp.label = label;
        imp.usage = "PERSONAL";
        imp.p12_path = p12;
        imp.password = p12pass;
        ExpectWithContext(zkr_import_cert(&z, imp), z.diag.e_msg).ToBe(0);

        // The ESM DB may already hold this cert content under an earlier
        // label; in that case its existing record/label is connected. Use
        // whatever label is actually on the ring for the rest of the flow.
        std::vector<ZKRCertInfo> certs;
        ExpectWithContext(zkr_list_ring(&z, owner, ring1, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));
        const std::string real_label = certs[0].label;

        // RACDCERT LABEL parity through the CLI: an exact --label
        // filter finds the certificate; a case-mangled one does not
        // (both succeed -- an empty result is not an error).
        std::string flt_out;
        int flt_rc = execute_command_with_output(
            zo_command + " system keyring list " + owner + " " + ring1 + " --label " + real_label,
            flt_out);
        ExpectWithContext(flt_rc, flt_out).ToBe(0);
        Expect(flt_out).ToContain(real_label);

        std::string lower_label = real_label;
        for (size_t li = 0; li < lower_label.size(); ++li)
          lower_label[li] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower_label[li])));
        if (lower_label != real_label)
        {
          std::string miss_out;
          int miss_rc = execute_command_with_output(
              zo_command + " system keyring list " + owner + " " + ring1 + " --label " + lower_label,
              miss_out);
          ExpectWithContext(miss_rc, miss_out).ToBe(0);
          Expect(miss_out).Not().ToContain("Certificate: ");
        }

        // show: R_datalib fields + GSK-decoded serial/validity.
        ZKRCertDetail detail;
        ExpectWithContext(zkr_show_cert(&z, owner, ring1, real_label, detail), z.diag.e_msg).ToBe(0);
        Expect(detail.label).ToBe(real_label);

        // export (PEM): zkr returns EBCDIC PEM text (CLI/file parity).
        ZKRExportOptions ex;
        ex.owner = owner;
        ex.ring = ring1;
        ex.label = real_label;
        ex.format = "pem";
        std::string data;
        ExpectWithContext(zkr_export_cert(&z, ex, data), z.diag.e_msg).ToBe(0);
        Expect(data).ToContain("-----BEGIN CERTIFICATE-----");

        // set-default on ring1.
        ExpectWithContext(zkr_set_default(&z, owner, ring1, real_label), z.diag.e_msg).ToBe(0);

        // connect the same certificate onto ring2, reading its bytes from ring1.
        ZKRConnectOptions conn;
        conn.owner = owner;
        conn.ring = ring2;
        conn.from_ring = ring1;
        conn.label = real_label;
        conn.usage = "PERSONAL";
        ExpectWithContext(zkr_connect_cert(&z, conn), z.diag.e_msg).ToBe(0);

        // trust: flip status via DataAlter (operates on the DB record).
        ZKRAlterOptions alt;
        alt.owner = owner;
        alt.label = real_label;
        alt.status = "NOTRUST";
        ExpectWithContext(zkr_alter_cert(&z, alt), z.diag.e_msg).ToBe(0);
        alt.status = "TRUST";
        ExpectWithContext(zkr_alter_cert(&z, alt), z.diag.e_msg).ToBe(0);

        // rename to a temporary label and back, so cleanup by real_label works.
        ZKRAlterOptions ren;
        ren.owner = owner;
        ren.label = real_label;
        ren.new_label = "ZKRUTREN" + zkr_unique();
        ExpectWithContext(zkr_alter_cert(&z, ren), z.diag.e_msg).ToBe(0);
        ZKRAlterOptions ren_back;
        ren_back.owner = owner;
        ren_back.label = ren.new_label;
        ren_back.new_label = real_label;
        ExpectWithContext(zkr_alter_cert(&z, ren_back), z.diag.e_msg).ToBe(0);

        // Record the connected label so afterAll purges it too, then
        // remove the certificate from the DB (and every ring).
        cleanup_labels.push_back(real_label);
        ExpectWithContext(zkr_del_cert(&z, owner, "*", real_label, /*skip_refresh*/ false), z.diag.e_msg)
            .ToBe(0);
      },
          cert_opts, run_cert);

      // Uncovered paths from doc/certificates-test-plan.md item 3:
      // p12 export (gsk_export_key), re-import already-exists warning,
      // ring-scoped disconnect (4/4/12 auto-refresh), connect from the
      // virtual ring ("--from-database"), count, standalone refresh.
      itif(
          "round-trips a p12 export, warns on re-import, disconnects one ring, "
          "connects from the database, counts, and refreshes",
          [&]() -> void
      {
        const std::string ring1 = "ZKRUT.RT1." + zkr_unique();
        const std::string ring2 = "ZKRUT.RT2." + zkr_unique();
        const std::string label = "ZKRUTRT" + zkr_unique();
        cleanup_rings.push_back(ring1);
        cleanup_rings.push_back(ring2);
        cleanup_labels.push_back(label);

        ZKR z{};
        ExpectWithContext(zkr_new_ring(&z, owner, ring1), z.diag.e_msg).ToBe(0);
        ExpectWithContext(zkr_new_ring(&z, owner, ring2), z.diag.e_msg).ToBe(0);

        ZKRImportOptions imp;
        imp.owner = owner;
        imp.ring = ring1;
        imp.label = label;
        imp.usage = "PERSONAL";
        imp.p12_path = p12;
        imp.password = p12pass;
        ExpectWithContext(zkr_import_cert(&z, imp), z.diag.e_msg).ToBe(0);

        std::vector<ZKRCertInfo> certs;
        ExpectWithContext(zkr_list_ring(&z, owner, ring1, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));
        const std::string real_label = certs[0].label;
        cleanup_labels.push_back(real_label);

        // keyring count through the CLI: a real ring (GetRingInfo sum)
        // and the virtual ring (DataGetFirst/GetNext enumeration).
        std::string out;
        int crc = execute_command_with_output(
            zo_command + " system keyring count " + owner + " " + ring1, out);
        ExpectWithContext(crc, out).ToBe(0);
        Expect(out).ToContain("1 certificate(s)");
        crc = execute_command_with_output(
            zo_command + " system keyring count " + owner + " '*'", out);
        ExpectWithContext(crc, out).ToBe(0);

        // p12 export with a passphrase (the gsk_export_key path; PEM
        // export is covered by the lifecycle test above).
        ZKRExportOptions ex;
        ex.owner = owner;
        ex.ring = ring1;
        ex.label = real_label;
        ex.format = "p12";
        ex.password = "ZKRUTEXP" + zkr_unique();
        std::string p12data;
        ExpectWithContext(zkr_export_cert(&z, ex, p12data), z.diag.e_msg).ToBe(0);
        Expect(p12data.empty()).ToBe(false);

        // Re-import the exported p12 under a NEW label onto ring2. The ESM
        // already holds this certificate, so the import must succeed
        // with the already-exists warning (SA23-2293 Table 79, reason
        // 8/12/16: supplied label ignored, existing record connected).
        exported_p12 = "/tmp/zkrut-rt-" + zkr_unique() + ".p12";
        {
          std::ofstream f(exported_p12.c_str(), std::ios::binary);
          f.write(p12data.data(), static_cast<std::streamsize>(p12data.size()));
        }
        ZKRImportOptions imp2;
        imp2.owner = owner;
        imp2.ring = ring2;
        imp2.label = "ZKRUTN" + zkr_unique();
        imp2.usage = "PERSONAL";
        imp2.p12_path = exported_p12;
        imp2.password = ex.password;
        ZKR z2{};
        ExpectWithContext(zkr_import_cert(&z2, imp2), z2.diag.e_msg).ToBe(0);
        Expect(z2.diag.warning.empty()).ToBe(false);

        // Ring-scoped delete: disconnect from ring1 only (exercises the
        // 4/4/12 auto-refresh); the cert must remain connected to ring2.
        ExpectWithContext(zkr_del_cert(&z, owner, ring1, real_label, false), z.diag.e_msg).ToBe(0);
        certs.clear();
        ExpectWithContext(zkr_list_ring(&z, owner, ring1, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBe(static_cast<size_t>(0));
        certs.clear();
        ExpectWithContext(zkr_list_ring(&z, owner, ring2, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));

        // Re-connect to ring1 sourcing the bytes from the virtual ring
        // -- the service path behind `cert connect --from-database`.
        // This settles empirically that DataGetFirst on "*" can feed
        // DataPut (test plan item 3.4).
        ZKRConnectOptions conn;
        conn.owner = owner;
        conn.ring = ring1;
        conn.from_ring = "*";
        conn.label = real_label;
        ExpectWithContext(zkr_connect_cert(&z, conn), z.diag.e_msg).ToBe(0);
        certs.clear();
        ExpectWithContext(zkr_list_ring(&z, owner, ring1, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));

        // Standalone DIGTCERT refresh.
        ExpectWithContext(zkr_refresh(&z), z.diag.e_msg).ToBe(0);

        // Remove the certificate from the DB and every ring (also
        // covered by afterAll if an earlier expectation aborts).
        ExpectWithContext(zkr_del_cert(&z, owner, "*", real_label, false), z.diag.e_msg).ToBe(0);
      },
          cert_opts, run_cert);

      // `--dsn` support (native/c/commands/certificates.cpp): export to a
      // data set and re-import from it, for both a sequential DSN and a
      // PDS/E member -- the BPAM path step 1 of the design exists for.
      // PKCS#12 encryption is salted, so two independent exports are never
      // byte-identical; round-trip validity (re-import succeeds) is what
      // proves the bytes survived the data set unmangled.
      itif(
          "exports a certificate to a data set and re-imports it, for a "
          "sequential DSN and a PDS/E member",
          [&]() -> void
      {
        const std::string ring1 = "ZKRUT.DS1." + zkr_unique();
        const std::string ring2 = "ZKRUT.DS2." + zkr_unique();
        const std::string ring3 = "ZKRUT.DS3." + zkr_unique();
        const std::string label = "ZKRUTDS" + zkr_unique();
        cleanup_rings.push_back(ring1);
        cleanup_rings.push_back(ring2);
        cleanup_rings.push_back(ring3);
        cleanup_labels.push_back(label);

        ZKR z{};
        ExpectWithContext(zkr_new_ring(&z, owner, ring1), z.diag.e_msg).ToBe(0);
        ExpectWithContext(zkr_new_ring(&z, owner, ring2), z.diag.e_msg).ToBe(0);
        ExpectWithContext(zkr_new_ring(&z, owner, ring3), z.diag.e_msg).ToBe(0);

        ZKRImportOptions imp;
        imp.owner = owner;
        imp.ring = ring1;
        imp.label = label;
        imp.usage = "PERSONAL";
        imp.p12_path = p12;
        imp.password = p12pass;
        ExpectWithContext(zkr_import_cert(&z, imp), z.diag.e_msg).ToBe(0);

        std::vector<ZKRCertInfo> certs;
        ExpectWithContext(zkr_list_ring(&z, owner, ring1, certs), z.diag.e_msg).ToBe(0);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));
        const std::string real_label = certs[0].label;
        cleanup_labels.push_back(real_label);

        const std::string dsn_pass = "ZKRUTDSN" + zkr_unique();
        const std::string seq_dsn = owner + ".ZKRUT.T" + zkr_unique().substr(1) + ".P12";
        const std::string lib_dsn = owner + ".ZKRUT.T" + zkr_unique().substr(1) + ".LIB";
        const std::string member_dsn = lib_dsn + "(CERT01)";
        cleanup_dsns.push_back(seq_dsn);
        cleanup_dsns.push_back(lib_dsn);

        // Sequential data set.
        std::string out;
        int rc = execute_command_with_output(
            zo_command + " system cert export " + owner + " " + ring1 + " -l '" + real_label +
                "' -F p12 -p " + dsn_pass + " --dsn " + seq_dsn,
            out);
        ExpectWithContext(rc, out).ToBe(0);

        ZDS read_zds{};
        zut_prepare_encoding("binary", &read_zds.encoding_opts);
        std::string seq_bytes;
        rc = zds_read(ZDSReadOpts{.zds = &read_zds, .dsname = seq_dsn}, seq_bytes);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(seq_bytes.empty()).ToBe(false);

        rc = execute_command_with_output(
            zo_command + " system cert import " + owner + " " + ring2 + " -l ZKRUTN" + zkr_unique() +
                " -u PERSONAL -p " + dsn_pass + " --dsn " + seq_dsn,
            out);
        ExpectWithContext(rc, out).ToBe(0);

        // PDS/E member -- the BPAM path.
        rc = execute_command_with_output(
            zo_command + " system cert export " + owner + " " + ring1 + " -l '" + real_label +
                "' -F p12 -p " + dsn_pass + " --dsn '" + member_dsn + "'",
            out);
        ExpectWithContext(rc, out).ToBe(0);

        ZDS read_member_zds{};
        zut_prepare_encoding("binary", &read_member_zds.encoding_opts);
        std::string member_bytes;
        rc = zds_read(ZDSReadOpts{.zds = &read_member_zds, .dsname = member_dsn}, member_bytes);
        ExpectWithContext(rc, read_member_zds.diag.e_msg).ToBe(0);
        Expect(member_bytes.empty()).ToBe(false);

        rc = execute_command_with_output(
            zo_command + " system cert import " + owner + " " + ring3 + " -l ZKRUTN" + zkr_unique() +
                " -u PERSONAL -p " + dsn_pass + " --dsn '" + member_dsn + "'",
            out);
        ExpectWithContext(rc, out).ToBe(0);

        // A subsequent export to the same member must succeed, not hang
        // -- proves the BPAM ENQ/RESERVE from the first export released.
        rc = execute_command_with_output(
            zo_command + " system cert export " + owner + " " + ring1 + " -l '" + real_label +
                "' -F p12 -p " + dsn_pass + " --dsn '" + member_dsn + "'",
            out);
        ExpectWithContext(rc, out).ToBe(0);
      },
          cert_opts, run_cert);

      // A binary upload to RECFM=FB pads the last record past the DER
      // SEQUENCE; import must still accept it (GSK ignores the trailing pad).
      itif(
          "imports a PKCS#12 bundle from a record-padded FB data set",
          [&]() -> void
      {
        const std::string ring = "ZKRUT.FB." + zkr_unique();
        cleanup_rings.push_back(ring);
        ZKR z{};
        ExpectWithContext(zkr_new_ring(&z, owner, ring), z.diag.e_msg).ToBe(0);

        const std::string fb_dsn = owner + ".ZKRUT.T" + zkr_unique().substr(1) + ".FB";
        cleanup_dsns.push_back(fb_dsn);
        DS_ATTRIBUTES a{};
        a.dsorg = "PS";
        a.recfm = "F,B";
        a.lrecl = 80;
        a.blksize = 27920;
        a.alcunit = "TRK";
        a.primary = 5;
        a.secondary = 5;
        std::string response;
        ExpectWithContext(zds_create_dsn(nullptr, fb_dsn, a, response), response).ToBe(0);

        std::string out;
        int rc = execute_command_with_output("cp -B " + p12 + " \"//'" + fb_dsn + "'\"", out);
        ExpectWithContext(rc, out).ToBe(0);

        rc = execute_command_with_output(
            zo_command + " system cert import " + owner + " " + ring + " -l ZKRUTN" + zkr_unique() +
                " -u PERSONAL -p " + p12pass + " --dsn " + fb_dsn,
            out);
        ExpectWithContext(rc, out).ToBe(0);

        std::vector<ZKRCertInfo> certs;
        ExpectWithContext(zkr_list_ring(&z, owner, ring, certs), z.diag.e_msg).ToBe(0);
        for (const auto &c : certs)
          cleanup_labels.push_back(c.label);
        Expect(certs.size()).ToBeGreaterThanOrEqualTo(static_cast<size_t>(1));
      },
          cert_opts, run_cert);
    });
  });
}
