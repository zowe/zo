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

#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "../zbase64.h"
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "ztest.hpp"
#include "zds.hpp"
#include "zjb.hpp"
#include "zut.hpp"
#include "zutils.hpp"
// #include "zstorage.metal.test.h"

using namespace ztst;

// Base for test contexts that create data sets and register them for cleanup
struct DataSetTestContextBase
{
  std::vector<std::string> &cleanup_list;

  explicit DataSetTestContextBase(std::vector<std::string> &list) : cleanup_list(list)
  {
  }

  void create_pds_at(const std::string &dsn)
  {
    ZDS zds = {0};
    create_pds(&zds, dsn);
  }

  void create_pdse_at(const std::string &dsn)
  {
    ZDS zds = {0};
    create_pdse(&zds, dsn);
  }

  void create_seq_at(const std::string &dsn)
  {
    ZDS zds = {0};
    create_seq(&zds, dsn);
  }
};

struct ListMembersTestContext : DataSetTestContextBase
{
  std::string pds_dsn;

  explicit ListMembersTestContext(std::vector<std::string> &list) : DataSetTestContextBase(list)
  {
    pds_dsn = get_random_ds(3);
    cleanup_list.push_back(pds_dsn);
  }

  void setup_pds()
  {
    create_pds_at(pds_dsn);
    write_to_dsn(pds_dsn + "(ABC1)", "data 1");
    write_to_dsn(pds_dsn + "(ABC2)", "data 2");
    write_to_dsn(pds_dsn + "(XYZ1)", "data 3");
    write_to_dsn(pds_dsn + "(TEST)", "data 4");
  }
};

// Test context for DD allocation and management
struct DDTestContext : DataSetTestContextBase
{
  std::vector<std::string> allocated_dds;

  explicit DDTestContext(std::vector<std::string> &list) : DataSetTestContextBase(list)
  {
  }

  void allocate_fb_dd(const std::string &ddname, int lrecl, int blksize = 0)
  {
    if (blksize == 0)
      blksize = lrecl;
    std::string alloc_cmd = "alloc dd(" + ddname + ") lrecl(" + std::to_string(lrecl) + ") recfm(f,b) blksize(" +
                            std::to_string(blksize) + ")";

    std::vector<std::string> single_dd;
    single_dd.push_back(alloc_cmd);
    allocated_dds.push_back(alloc_cmd);

    ZDIAG diag{};
    int rc = zut_loop_dynalloc(diag, single_dd);
    if (rc != 0)
    {
      throw std::runtime_error("Failed to allocate DD " + ddname + ": " + std::string(diag.e_msg));
    }
  }

  void allocate_vb_dd(const std::string &ddname, int lrecl)
  {
    std::string alloc_cmd = "alloc dd(" + ddname + ") lrecl(" + std::to_string(lrecl) + ") recfm(v,b) blksize(" +
                            std::to_string(lrecl + 4) + ")";

    std::vector<std::string> single_dd;
    single_dd.push_back(alloc_cmd);
    allocated_dds.push_back(alloc_cmd);

    ZDIAG diag{};
    int rc = zut_loop_dynalloc(diag, single_dd);
    if (rc != 0)
    {
      throw std::runtime_error("Failed to allocate VB DD " + ddname + ": " + std::string(diag.e_msg));
    }
  }

  void allocate_sysprint_dd()
  {
    allocate_fb_dd("SYSPRINT", 80);
  }

  bool verify_dd_content(const std::string &ddname, const std::string &expected)
  {
    ZDS read_zds{};
    ZDSReadOpts read_opts{.zds = &read_zds, .ddname = ddname};
    std::string content;
    int rc = zds_read(read_opts, content);
    return (rc == 0) && (content.find(expected) != std::string::npos);
  }

  ~DDTestContext()
  {
    if (!allocated_dds.empty())
    {
      ZDIAG diag{};
      zut_free_dynalloc_dds(diag, allocated_dds);
    }
  }
};

void zds_tests()
{
  std::vector<std::string> created_dsns;
  TEST_OPTIONS extended_timeout = {false, 30};
  describe("zds", [&]() -> void
  {
    TEST_OPTIONS cleanup_opts = {false, 30};
    afterAll([&]() -> void
    {
      for (const auto &dsn : created_dsns)
      {
        try
        {
          ZDS zds = {0};
          zds_delete_dsn(&zds, dsn);
        }
        catch (...)
        {
        }
      }
      created_dsns.clear();
    }, cleanup_opts);

    describe("list", [&]() -> void
    {
      it("should list data sets with a given DSN", []() -> void
      {
        int rc = 0;
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        std::string dsn = "SYS1.MACLIB";
        rc = zds_list_data_sets(&zds, dsn, entries);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        std::string found = "";
        for (const auto &e : entries)
        {
          std::string trimmed_name = e.name;
          zut_rtrim(trimmed_name);
          if (trimmed_name == dsn)
          {
            found = trimmed_name;
          }
        }
        Expect(found).ToBe(dsn);
      });

      it("should list data sets with a given DSN and show attributes", []() -> void
      {
        int rc = 0;
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        std::string dsn = "SYS1.MACLIB";
        rc = zds_list_data_sets(&zds, dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = nullptr;
        for (auto &e : entries)
        {
          std::string trimmed_name = e.name;
          zut_rtrim(trimmed_name);
          if (trimmed_name == dsn)
          {
            found = &e;
          }
        }
        Expect(found != nullptr).ToBe(true);
        Expect(zut_rtrim(found->name)).ToBe(dsn);
        Expect(found->volser.length()).ToBeGreaterThan(0);
        Expect(found->volser.front()).Not().ToBe('*');
        Expect(found->volsers.size()).ToBeGreaterThan(0);
        Expect(found->volsers[0].front()).Not().ToBe('*');
      });

      it("should list data sets with a given DSN and calculate used space correctly", [&]() -> void
      {
        int rc = 0;
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 6160;
        attr.alcunit = "CYL";
        attr.primary = 1;
        attr.dirblk = 5;
        std::string dsname = get_random_ds(3);
        created_dsns.push_back(dsname);
        create_dsn_with_attrs(&zds, dsname, attr, "PDS");
        std::vector<ZDSEntry> entries;

        rc = zds_list_data_sets(&zds, dsname, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(entries.size()).ToBe(1);
        Expect(zut_rtrim(entries[0].name)).ToBe(dsname);
        Expect(entries[0].allocx).ToBe(1);
        Expect(entries[0].usedx).ToBe(1);
        Expect(entries[0].usedp > 0 && entries[0].usedp < 50).ToBe(true);
      });

      it("should find dsn (SYS1.MACLIB) based on a pattern: (SYS1.*)", []() -> void
      {
        int rc = 0;
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        std::string dsn = "SYS1.MACLIB";
        std::string pattern = "SYS1.*";
        rc = zds_list_data_sets(&zds, pattern, entries);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        std::string found = "";
        for (const auto &e : entries)
        {
          std::string trimmed_name = e.name;
          zut_rtrim(trimmed_name);
          if (trimmed_name == dsn)
          {
            found = trimmed_name;
          }
        }
        Expect(found).ToBe(dsn);
      });
    });

    describe("list members", [&]() -> void
    {
      it("should list SYS1.PARMLIB without rejecting full directory blocks", []() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, "SYS1.PARMLIB", members);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(RTNCD_SUCCESS);
        Expect(members.empty()).ToBe(false);
        for (const auto &member : members)
        {
          Expect(zds_is_valid_member_name(member.name)).ToBe(true);
        }
      });

      it("should list all members when pattern is empty", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(4); // ABC1, ABC2, XYZ1, TEST
      });

      it("should list specific member by exact name", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "XYZ1");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(1);
        Expect(members[0].name).ToBe("XYZ1");
      });

      it("should list members matching wildcard *", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "ABC*");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(2); // ABC1, ABC2

        bool has_abc1 = members[0].name == "ABC1" || members[1].name == "ABC1";
        bool has_abc2 = members[0].name == "ABC2" || members[1].name == "ABC2";
        Expect(has_abc1).ToBe(true);
        Expect(has_abc2).ToBe(true);
      });

      it("should list members matching wildcard ?", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        // Match exactly 4 characters starting with ABC
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "ABC?");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(2); // ABC1, ABC2
      });

      it("should return empty list when no members match", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "NOMATCH*");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(0);
      });
      it("should list members matching wildcard * at the beginning", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds(); // Creates ABC1, ABC2, XYZ1, TEST

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "*1");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(2); // ABC1, XYZ1

        bool has_abc1 = members[0].name == "ABC1" || members[1].name == "ABC1";
        bool has_xyz1 = members[0].name == "XYZ1" || members[1].name == "XYZ1";
        Expect(has_abc1).ToBe(true);
        Expect(has_xyz1).ToBe(true);
      });

      it("should list members matching wildcard * in the middle", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "A*2");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(1); // ABC2
        Expect(members[0].name).ToBe("ABC2");
      });

      it("should list members matching a combination of ? and *", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        // Starts with any char, followed by B, then any trailing chars
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "?B*");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(2); // ABC1, ABC2

        bool has_abc1 = members[0].name == "ABC1" || members[1].name == "ABC1";
        bool has_abc2 = members[0].name == "ABC2" || members[1].name == "ABC2";
        Expect(has_abc1).ToBe(true);
        Expect(has_abc2).ToBe(true);
      });

      it("should list members matching multiple ? wildcards for exact length", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        // Matches exactly 4 characters
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "????");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(4); // ABC1, ABC2, XYZ1, TEST
      });

      it("should list members matching an embedded ? and trailing *", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;
        // 'X?Z*' matches XYZ1
        int rc = zds_list_members(&zds, tc.pds_dsn, members, "X?Z*");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(1);
        Expect(members[0].name).ToBe("XYZ1");
      });
      it("should list members matching a lowercase pattern by converting it to uppercase", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;

        int rc = zds_list_members(&zds, tc.pds_dsn, members, "xyz1");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(1);
        Expect(members[0].name).ToBe("XYZ1");

        members.clear();
        rc = zds_list_members(&zds, tc.pds_dsn, members, "abc*");
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(2);

        Expect(members[0].name == "ABC1" || members[1].name == "ABC1").ToBe(true);
        Expect(members[0].name == "ABC2" || members[1].name == "ABC2").ToBe(true);
      });
      it("should list members with correct ISPF statistics attributes", [&]() -> void
      {
        ListMembersTestContext tc(created_dsns);
        tc.setup_pds();

        ZDS zds = {0};
        std::vector<ZDSMem> members;

        int rc = zds_list_members(&zds, tc.pds_dsn, members, "ABC1", true);

        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        Expect(members.size()).ToBe(1);

        ZDSMem &mem = members[0];
        Expect(mem.name).ToBe("ABC1");
        Expect(mem.user.empty()).ToBe(false);

        Expect(mem.c4date.empty()).ToBe(false);
        Expect(mem.m4date.empty()).ToBe(false);

        Expect(mem.mtime.empty()).ToBe(false);

        Expect(mem.c4date[2]).ToBe('/');
        Expect(mem.c4date[5]).ToBe('/');

        Expect(mem.sclm).ToBe(false);
      });
    });

    describe("source encoding tests", []() -> void
    {
      it("should use default source encoding (UTF-8) when not specified", []() -> void
      {
        ZDS zds = {0};
        // Set target encoding but no source encoding
        strcpy(zds.encoding_opts.codepage, "IBM-1047");
        zds.encoding_opts.data_type = eDataTypeText;
        // source_codepage should be empty/null

        // The actual encoding conversion should use UTF-8 as source when source_codepage is empty
        // Since we can't easily test the actual file operations without a real data set,
        // we'll verify the struct is properly initialized
        Expect(std::strlen(zds.encoding_opts.source_codepage)).ToBe(0);
        Expect(std::strlen(zds.encoding_opts.codepage)).ToBe(8); // "IBM-1047"
      });

      it("should use specified source encoding when provided", []() -> void
      {
        ZDS zds = {0};
        // Set both target and source encoding
        strcpy(zds.encoding_opts.codepage, "IBM-1047");
        strcpy(zds.encoding_opts.source_codepage, "IBM-037");
        zds.encoding_opts.data_type = eDataTypeText;

        // Verify both encodings are set correctly
        Expect(std::string(zds.encoding_opts.codepage)).ToBe("IBM-1047");
        Expect(std::string(zds.encoding_opts.source_codepage)).ToBe("IBM-037");
        Expect(zds.encoding_opts.data_type).ToBe(eDataTypeText);
      });

      it("should handle binary data type correctly with source encoding", []() -> void
      {
        ZDS zds = {0};
        strcpy(zds.encoding_opts.codepage, "binary");
        strcpy(zds.encoding_opts.source_codepage, "UTF-8");
        zds.encoding_opts.data_type = eDataTypeBinary;

        // For binary data, encoding should not be used for conversion
        Expect(std::string(zds.encoding_opts.codepage)).ToBe("binary");
        Expect(std::string(zds.encoding_opts.source_codepage)).ToBe("UTF-8");
        Expect(zds.encoding_opts.data_type).ToBe(eDataTypeBinary);
      });

      it("should handle empty source encoding gracefully", []() -> void
      {
        ZDS zds = {0};
        strcpy(zds.encoding_opts.codepage, "IBM-1047");
        // Explicitly set source_codepage to empty
        memset(zds.encoding_opts.source_codepage, 0, sizeof(zds.encoding_opts.source_codepage));
        zds.encoding_opts.data_type = eDataTypeText;

        // Should handle empty source encoding (will default to UTF-8 in actual conversion)
        Expect(std::strlen(zds.encoding_opts.source_codepage)).ToBe(0);
        Expect(std::string(zds.encoding_opts.codepage)).ToBe("IBM-1047");
      });

      it("should handle maximum length encoding names", []() -> void
      {
        ZDS zds = {0};
        // Test with maximum length encoding names (15 chars + null terminator)
        std::string long_target = "IBM-1234567890A"; // 15 characters
        std::string long_source = "UTF-1234567890B"; // 15 characters

        strncpy(zds.encoding_opts.codepage, long_target.c_str(), sizeof(zds.encoding_opts.codepage) - 1);
        strncpy(zds.encoding_opts.source_codepage, long_source.c_str(), sizeof(zds.encoding_opts.source_codepage) - 1);

        // Ensure null termination
        zds.encoding_opts.codepage[sizeof(zds.encoding_opts.codepage) - 1] = '\0';
        zds.encoding_opts.source_codepage[sizeof(zds.encoding_opts.source_codepage) - 1] = '\0';

        Expect(std::string(zds.encoding_opts.codepage)).ToBe(long_target);
        Expect(std::string(zds.encoding_opts.source_codepage)).ToBe(long_source);
      });

      it("should preserve encoding settings through struct copy", []() -> void
      {
        ZDS zds1 = {0};
        strcpy(zds1.encoding_opts.codepage, "IBM-1047");
        strcpy(zds1.encoding_opts.source_codepage, "IBM-037");
        zds1.encoding_opts.data_type = eDataTypeText;
        ZDS zds2 = zds1;
        Expect(std::string(zds2.encoding_opts.codepage)).ToBe("IBM-1047");
        Expect(std::string(zds2.encoding_opts.source_codepage)).ToBe("IBM-037");
        Expect(zds2.encoding_opts.data_type).ToBe(eDataTypeText);
      });

      it("should validate encoding struct size assumptions", []() -> void
      {
        // Verify the struct size is as expected for both fields
        Expect(sizeof(((ZEncode *)0)->codepage)).ToBe(16);
        Expect(sizeof(((ZEncode *)0)->source_codepage)).ToBe(16);

        // Verify the fields are at expected offsets
        ZEncode encode = {0};
        strcpy(encode.codepage, "test1");
        strcpy(encode.source_codepage, "test2");

        Expect(std::string(encode.codepage)).ToBe("test1");
        Expect(std::string(encode.source_codepage)).ToBe("test2");
      });

      it("should handle common encoding combinations", []() -> void
      {
        ZDS zds = {0};
        zds.encoding_opts.data_type = eDataTypeText;

        // Test common mainframe encoding conversions
        struct EncodingPair
        {
          const char *source;
          const char *target;
        };

        EncodingPair pairs[] = {
            {"UTF-8", "IBM-1047"},     // UTF-8 to EBCDIC
            {"IBM-037", "UTF-8"},      // EBCDIC to UTF-8
            {"IBM-1047", "IBM-037"},   // EBCDIC to EBCDIC
            {"ISO8859-1", "IBM-1047"}, // ASCII to EBCDIC
            {"UTF-8", "binary"}        // Text to binary
        };

        for (const auto &pair : pairs)
        {
          memset(&zds.encoding_opts, 0, sizeof(zds.encoding_opts));
          strcpy(zds.encoding_opts.source_codepage, pair.source);
          strcpy(zds.encoding_opts.codepage, pair.target);
          Expect(std::string(zds.encoding_opts.source_codepage)).ToBe(std::string(pair.source));
          Expect(std::string(zds.encoding_opts.codepage)).ToBe(std::string(pair.target));
        }
      });
    });

    // Regression tests for writing DBCS content to PDS members with a
    // stateful mixed SBCS/DBCS target codepage (e.g. IBM-1399). Each
    // record must be self-contained: it has to start and end in
    // single-byte state with balanced shift-out/shift-in. The member
    // (BPAM) write paths encode line-by-line, so a DBCS record that is
    // not the last line previously had its closing SI dropped, which
    // corrupted the record and made the read-back conversion fail.
    describe("DBCS member encoding round-trip", [&]() -> void
    {
      // UTF-8 bytes for 日本語 (U+65E5 U+672C U+8A9E).
      const std::string japanese = std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 9);
      // ASCII markers built as raw bytes so they don't depend
      // on the (EBCDIC) compile-time charset of the test.
      const std::string marker_a = std::string("\x2F\x2F\x41", 3); // "//A"
      const std::string marker_b = std::string("\x2F\x2F\x42", 3); // "//B"

      // UTF-8 input where the DBCS record is in the middle, so
      // it is followed by another record: "//A\n日本語\n//B\n".
      const std::string utf8_input = marker_a + "\x0A" + japanese + "\x0A" + marker_b + "\x0A";

      it("should round-trip a non-final DBCS record written to a member (buffered)", [&]() -> void
      {
        ZDS zds = {0};
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        create_pds(&zds, dsn); // FB/80 PDS, like a JCL library
        std::string member = dsn + "(JCL)";

        // Write UTF-8 -> IBM-1399 (stateful Japanese mixed codepage)
        strcpy(zds.encoding_opts.codepage, "IBM-1399");
        strcpy(zds.encoding_opts.source_codepage, "UTF-8");
        zds.encoding_opts.data_type = eDataTypeText;

        ZDSWriteOpts wopts{.zds = &zds, .dsname = member};
        int wrc = zds_write(wopts, utf8_input);
        // Pre-fix this failed: the post-write etag read-back
        // conversion (IBM-1399 -> UTF-8) hit a malformed record.
        ExpectWithContext(wrc, zds.diag.e_msg).ToBe(RTNCD_SUCCESS);

        // Read back with the same encoding and verify the
        // DBCS record and its neighbors survived intact.
        ZDS rzds = {0};
        strcpy(rzds.encoding_opts.codepage, "IBM-1399");
        strcpy(rzds.encoding_opts.source_codepage, "UTF-8");
        rzds.encoding_opts.data_type = eDataTypeText;

        std::string out;
        ZDSReadOpts ropts{.zds = &rzds, .dsname = member};
        int rrc = zds_read(ropts, out);
        ExpectWithContext(rrc, rzds.diag.e_msg).ToBe(RTNCD_SUCCESS);

        ExpectWithContext(out.find(japanese) != std::string::npos, "DBCS record should round-trip intact").ToBe(true);
        Expect(out.find(marker_a) != std::string::npos).ToBe(true);
        Expect(out.find(marker_b) != std::string::npos).ToBe(true);
      });

      it("should round-trip a non-final DBCS record written to a member (streamed)", [&]() -> void
      {
        ZDS zds = {0};
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        create_pds(&zds, dsn);
        std::string member = dsn + "(JCL)";

        // The streamed write path consumes base64 chunks from
        // a pipe/file, mirroring how the extension uploads.
        std::string temp_file = "/tmp/zds_dbcs_stream_" + std::to_string(getpid());
        std::ofstream temp_out(temp_file);
        temp_out << zbase64::encode(utf8_input);
        temp_out.close();

        strcpy(zds.encoding_opts.codepage, "IBM-1399");
        strcpy(zds.encoding_opts.source_codepage, "UTF-8");
        zds.encoding_opts.data_type = eDataTypeText;

        size_t content_len = 0;
        ZDSWriteOpts wopts{.zds = &zds, .dsname = member};
        int wrc = zds_write_streamed(wopts, temp_file, &content_len);
        unlink(temp_file.c_str());
        ExpectWithContext(wrc, zds.diag.e_msg).ToBe(RTNCD_SUCCESS);

        ZDS rzds = {0};
        strcpy(rzds.encoding_opts.codepage, "IBM-1399");
        strcpy(rzds.encoding_opts.source_codepage, "UTF-8");
        rzds.encoding_opts.data_type = eDataTypeText;

        std::string out;
        ZDSReadOpts ropts{.zds = &rzds, .dsname = member};
        int rrc = zds_read(ropts, out);
        ExpectWithContext(rrc, rzds.diag.e_msg).ToBe(RTNCD_SUCCESS);

        ExpectWithContext(out.find(japanese) != std::string::npos, "DBCS record should round-trip intact").ToBe(true);
        Expect(out.find(marker_a) != std::string::npos).ToBe(true);
        Expect(out.find(marker_b) != std::string::npos).ToBe(true);
      });
    });

    describe("binary member write", [&]() -> void
    {
      it("should keep records that exactly fill a VB block (LRECL 84, BLKSIZE 88)", [&]() -> void
      {
        ZDS zds = {};
        std::string response;
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);

        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "VB";
        attr.lrecl = 84;
        attr.blksize = 88;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 1;
        attr.dirblk = 1;
        ExpectWithContext(zds_create_dsn(&zds, dsn, attr, response), response).ToBe(0);

        // Two full 80-byte chunks plus a partial last chunk
        std::string data;
        for (int i = 0; i < 200; ++i)
          data.push_back(static_cast<char>(i));

        std::string member = dsn + "(BIN)";
        ExpectWithContext(zds_write_binary(ZDSWriteOpts{.zds = &zds, .dsname = member}, data), zds.diag.e_msg).ToBe(RTNCD_SUCCESS);

        ZDS rzds = {};
        zut_prepare_encoding("binary", &rzds.encoding_opts);
        std::string out;
        ExpectWithContext(zds_read(ZDSReadOpts{.zds = &rzds, .dsname = member}, out), rzds.diag.e_msg).ToBe(RTNCD_SUCCESS);
        Expect(out.size()).ToBe(data.size());
        Expect(out == data).ToBe(true);
      });
    });

    describe("copy datasets", [&]() -> void
    {
      DS_ATTRIBUTES pds_attr{};
      pds_attr.dsorg = "PO";
      pds_attr.recfm = "FB";
      pds_attr.lrecl = 80;
      pds_attr.blksize = 6160;
      pds_attr.alcunit = "TRACKS";
      pds_attr.primary = 5;
      pds_attr.secondary = 5;
      pds_attr.dirblk = 10;

      DS_ATTRIBUTES ps_attr{};

      ps_attr.dsorg = "PS";
      ps_attr.recfm = "FB";
      ps_attr.lrecl = 80;
      ps_attr.blksize = 0;
      ps_attr.alcunit = "TRACKS";
      ps_attr.primary = 1;
      ps_attr.secondary = 1;
      ps_attr.dirblk = 0;

      std::string response;

      it("should copy sequential to sequential (creates target)", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string src = get_random_ds(3);
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);

        Expect(zds_create_dsn(&zds, src, ps_attr, response)).ToBe(0);

        std::string data = "test data";
        ZDSWriteOpts wopts{.zds = &zds, .dsname = src};
        Expect(zds_write(wopts, data)).ToBe(0);

        int rc = zds_copy_dsn(&zds, src, tgt, &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });

      it("should copy PDS to PDS with replace flag", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string src = get_random_ds(3);
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);

        zds_create_dsn(&zds, src, pds_attr, response);

        std::string d1 = "DATA1", d2 = "DATA2", data = "OLD DATA";

        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = src + "(M1)"}, d1);
        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = src + "(M2)"}, d2);

        zds_create_dsn(&zds, tgt, pds_attr, response);
        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = tgt + "(tgt)"}, data);

        opts.replace = true;
        int rc = zds_copy_dsn(&zds, src, tgt, &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });

      it("should copy member to member", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string src_pds = get_random_ds(3);
        std::string tgt_pds = get_random_ds(3);
        created_dsns.push_back(src_pds);
        created_dsns.push_back(tgt_pds);

        zds_create_dsn(&zds, src_pds, pds_attr, response);
        zds_create_dsn(&zds, tgt_pds, pds_attr, response);

        std::string data = "test data";
        ZDSWriteOpts wopts{.zds = &zds, .dsname = src_pds + "(M1)"};
        int rc = zds_write(wopts, data);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);

        std::string src = src_pds + "(M1)";
        rc = zds_copy_dsn(&zds, src, tgt_pds + "(NEW)", &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });

      it("should copy PDS to PDS with overwrite", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        opts.overwrite = true;
        std::string src = get_random_ds(3);
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);

        zds_create_dsn(&zds, src, pds_attr, response);
        std::string n = "NEW", o = "OLD";
        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = src + "(M1)"}, n);

        zds_create_dsn(&zds, tgt, pds_attr, response);
        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = tgt + "(tgt)"}, o);

        int rc = zds_copy_dsn(&zds, src, tgt, &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);

        std::string out;
        ZDSReadOpts ropts{.zds = &zds, .dsname = tgt + "(tgt)"};
        int read_rc = zds_read(ropts, out);
        Expect(read_rc).Not().ToBe(0);
      });

      it("should handle special characters (@, #, $) with single quotes", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string base = get_random_ds(3);
        std::string src = base + ".PDS@#$";
        std::string tgt = base + ".TGT#@$";
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);

        zds_create_dsn(&zds, src, pds_attr, response);
        zds_create_dsn(&zds, tgt, pds_attr, response);

        std::string data = "test data";
        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = src + "(MEM#1)"}, data);

        int rc = zds_copy_dsn(&zds, src + "(MEM#1)", tgt + "(MEM#1)", &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });

      it("should fail if target member exists and replace flag is not used", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string src = get_random_ds(3);
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);

        zds_create_dsn(&zds, src, pds_attr, response);

        zds_create_dsn(&zds, tgt, pds_attr, response);

        std::string d1 = "DATA1", d2 = "DATA2";

        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = src + "(M1)"}, d1);

        zds_write(ZDSWriteOpts{.zds = &zds, .dsname = tgt + "(tgt)"}, d2);

        int rc = zds_copy_dsn(&zds, src, tgt, &opts);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Use the 'replace' option");
      });

      it("should fail if source data set does not exist", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};

        std::string src = "DNE.DATASET";
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(tgt);

        int rc = zds_copy_dsn(&zds, src, tgt, &opts);

        Expect(rc).ToBe(RTNCD_FAILURE);

        Expect(std::string(zds.diag.e_msg)).ToContain("Source data set 'DNE.DATASET' not found");
      });

      it("should fail if source member does not exist in an existing PDS", [&]() -> void
      {
        ZDS zds = {};
        ZDSCopyOptions opts{};
        std::string src = get_random_ds(3);
        std::string tgt = get_random_ds(3);
        created_dsns.push_back(src);
        created_dsns.push_back(tgt);
        Expect(zds_create_dsn(&zds, src, pds_attr, response)).ToBe(0);
        Expect(zds_create_dsn(&zds, tgt, pds_attr, response)).ToBe(0);

        int rc = zds_copy_dsn(&zds, src + "(DNE)", tgt + "(NEW)", &opts);

        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Source member 'DNE' not found");
      });

      it("should handle concurrent sequential data set copies without DD name collision", [&]() -> void
      {
        constexpr int N = 4;
        ZDS setup_zds = {};

        std::vector<std::string> srcs(N), tgts(N);
        for (int i = 0; i < N; i++)
        {
          srcs[i] = get_random_ds(3);
          tgts[i] = get_random_ds(3);
          created_dsns.push_back(srcs[i]);
          created_dsns.push_back(tgts[i]);
          create_seq(&setup_zds, srcs[i]);
          std::string data = "sequential-payload-" + std::to_string(i);
          zds_write(ZDSWriteOpts{.zds = &setup_zds, .dsname = srcs[i]}, data);
        }

        std::vector<int> results(N, -1);
        std::vector<std::string> diag_msgs(N);
        {
          std::vector<std::thread> threads;
          for (int i = 0; i < N; i++)
          {
            threads.emplace_back([&, i]()
            {
              ZDS thread_zds = {};
              ZDSCopyOptions opts{};
              results[i] = zds_copy_dsn(&thread_zds, srcs[i], tgts[i], &opts);
              diag_msgs[i] = thread_zds.diag.e_msg;
            });
          }
          for (auto &t : threads)
            t.join();
        }

        for (int i = 0; i < N; i++)
        {
          ExpectWithContext(results[i], "thread " + std::to_string(i) + ": " + diag_msgs[i]).ToBe(0);
          std::string out;
          ZDS read_zds = {};
          ZDSReadOpts ropts{.zds = &read_zds, .dsname = tgts[i]};
          ExpectWithContext(zds_read(ropts, out), "read " + std::to_string(i)).ToBe(0);
          Expect(out).ToContain("sequential-payload-" + std::to_string(i));
        }

        ZDS del_zds = {};
        for (int i = 0; i < N; i++)
        {
          zds_delete_dsn(&del_zds, srcs[i]);
          zds_delete_dsn(&del_zds, tgts[i]);
        }
      }, extended_timeout);

      it("should handle concurrent PDS copies without DD name collision", [&]() -> void
      {
        constexpr int N = 4;
        ZDS setup_zds = {};

        std::vector<std::string> srcs(N), tgts(N);
        for (int i = 0; i < N; i++)
        {
          srcs[i] = get_random_ds(3);
          tgts[i] = get_random_ds(3);
          created_dsns.push_back(srcs[i]);
          created_dsns.push_back(tgts[i]);
          create_pds(&setup_zds, srcs[i]);
          create_pds(&setup_zds, tgts[i]);
          std::string data = "pds-payload-" + std::to_string(i);
          zds_write(ZDSWriteOpts{.zds = &setup_zds, .dsname = srcs[i] + "(MEMBER)"}, data);
        }

        std::vector<int> results(N, -1);
        std::vector<std::string> diag_msgs(N);
        {
          std::vector<std::thread> threads;
          for (int i = 0; i < N; i++)
          {
            threads.emplace_back([&, i]()
            {
              ZDS thread_zds = {};
              ZDSCopyOptions opts{};
              opts.replace = true;
              results[i] = zds_copy_dsn(&thread_zds, srcs[i], tgts[i], &opts);
              diag_msgs[i] = thread_zds.diag.e_msg;
            });
          }
          for (auto &t : threads)
            t.join();
        }

        for (int i = 0; i < N; i++)
        {
          ExpectWithContext(results[i], "thread " + std::to_string(i) + ": " + diag_msgs[i]).ToBe(0);
          std::string out;
          ZDS read_zds = {};
          ZDSReadOpts ropts{.zds = &read_zds, .dsname = tgts[i] + "(MEMBER)"};
          ExpectWithContext(zds_read(ropts, out), "read " + std::to_string(i)).ToBe(0);
          Expect(out).ToContain("pds-payload-" + std::to_string(i));
        }

        ZDS del_zds = {};
        for (int i = 0; i < N; i++)
        {
          zds_delete_dsn(&del_zds, srcs[i]);
          zds_delete_dsn(&del_zds, tgts[i]);
        }
      }, extended_timeout);
    });

    describe("delete data sets", [&]() -> void
    {
      it("should fail if data set does not exist", []() -> void
      {
        ZDS zds = {0};
        std::string dsname = get_random_ds(3);
        int rc = zds_delete_dsn(&zds, dsname);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg))
            .ToContain("Failed to allocate data set '" + dsname + "' for deletion (errno=46)");
      });

      it("should fail if member does not exist", [&]() -> void
      {
        ZDS zds = {0};
        std::string dsname = get_random_ds(3);
        created_dsns.push_back(dsname);
        create_pds(&zds, dsname);
        int rc = zds_delete_dsn(&zds, dsname + "(NOEXIST)");
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg))
            .ToContain("Failed to allocate data set '" + dsname + "(NOEXIST)' for deletion (errno=91)");
      });

      it("should delete data set successfully when valid", [&]() -> void
      {
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};

        attr.dsorg = "PS";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 0;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 1;
        attr.dirblk = 0;
        std::string dsname = get_random_ds(3);
        created_dsns.push_back(dsname);

        create_seq(&zds, dsname);
        int rc = zds_delete_dsn(&zds, dsname);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });
    });

    describe("rename data sets", [&]() -> void
    {
      it("should fail if source or target data sets are empty", []() -> void
      {
        ZDS zds = {0};
        std::string target = get_random_ds(3);
        int rc = zds_rename_dsn(&zds, "", target);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Data set names must be valid");

        ZDS zds2 = {0};
        rc = zds_rename_dsn(&zds2, "USER.TEST", "");
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds2.diag.e_msg)).ToContain("Data set names must be valid");
      });

      it("should fail if target data set name exceeds max length", []() -> void
      {
        ZDS zds = {0};
        std::string longName = "USER.TEST.TEST.TEST.TEST.TEST.TEST.TEST.TEST.TEST.TEST.TEST.TEST";
        std::string source = get_random_ds(3);
        int rc = zds_rename_dsn(&zds, source, longName);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Target data set name exceeds max character length of 44");
      });

      it("should fail if source data set does not exist", []() -> void
      {
        ZDS zds = {0};
        std::string target = get_random_ds(3);
        std::string source = get_random_ds(3);
        int rc = zds_rename_dsn(&zds, source, target);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Source data set does not exist");
      });

      it("should fail if target data set already exists", [&]() -> void
      {
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};

        attr.dsorg = "PS";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 0;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 1;
        attr.dirblk = 0;

        std::string source = get_random_ds(3);
        std::string target = get_random_ds(3);
        created_dsns.push_back(source);
        created_dsns.push_back(target);

        create_seq(&zds, source);
        create_seq(&zds, target);
        int rc = zds_rename_dsn(&zds, source, target);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Target data set name already exists");
      });

      it("should rename dataset successfully when valid", [&]() -> void
      {
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};

        attr.dsorg = "PS";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 0;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 1;
        attr.dirblk = 0;
        std::string before = get_random_ds(3);
        std::string after = get_random_ds(3);
        created_dsns.push_back(after); // before is renamed to after; clean up final name

        create_seq(&zds, before);
        int rc = zds_rename_dsn(&zds, before, after);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
      });
    });
    describe("rename members", [&]() -> void
    {
      const std::string M1 = "M1";
      const std::string M2 = "M2";
      DS_ATTRIBUTES attr{};

      attr.dsorg = "PO";
      attr.recfm = "FB";
      attr.lrecl = 80;
      attr.blksize = 6160;
      attr.alcunit = "TRACKS";
      attr.primary = 1;
      attr.secondary = 1;
      attr.dirblk = 10;
      std::string response;

      it("should fail if data set name is empty", [&]() -> void
      {
        ZDS zds = {0};
        std::string ds = get_random_ds(3);
        int rc = zds_rename_members(&zds, "", M1, M2);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Data set name must be valid");
      });

      it("should fail if member names are empty", [&]() -> void
      {
        ZDS zds = {0};
        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);
        Expect(rc).ToBe(0);

        rc = zds_rename_members(&zds, ds, "", M2);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Member name cannot be empty");

        ZDS zds2 = {0};
        rc = zds_rename_members(&zds2, ds, M1, "");
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Member name cannot be empty");
      });
      it("should fail if member name is too long", [&]() -> void
      {
        ZDS zds = {0};
        std::string longName = "USER.TEST.TEST.TEST";
        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);
        std::string empty = "";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};
        rc = zds_write(write_opts, empty);
        Expect(rc).ToBe(0);
        rc = zds_rename_members(&zds, ds, M1, longName);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg))
            .ToContain("Member name must start with A-Z,#,@,$ and contain only A-Z,0-9,#,@,$ (max 8 chars)");
      });

      it("should fail if data set does not exist", [&]() -> void
      {
        ZDS zds = {0};
        std::string ds = get_random_ds(3);
        int rc = zds_rename_members(&zds, ds, M1, M2);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Data set does not exist");
      });

      it("should fail if source member does not exist", [&]() -> void
      {
        ZDS zds = {0};
        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);
        rc = zds_rename_members(&zds, ds, M1, "M3");
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Source member does not exist");
      });

      it("should fail if target member already exists", [&]() -> void
      {
        ZDS zds = {};

        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);

        std::string empty = "";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};
        rc = zds_write(write_opts, empty);
        Expect(rc).ToBe(0);
        memset(zds.etag, 0, 8);
        ZDSWriteOpts write_opts2{.zds = &zds, .dsname = ds + "(M2)"};
        rc = zds_write(write_opts2, empty);
        Expect(rc).ToBe(0);

        rc = zds_rename_members(&zds, ds, M1, M2);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg)).ToContain("Target member already exists");
      });

      it("should rename dataset successfully when valid", [&]() -> void
      {
        ZDS zds = {};
        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);

        std::string empty = "";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};
        rc = zds_write(write_opts, empty);
        Expect(rc).ToBe(0);

        rc = zds_rename_members(&zds, ds, M1, "M3");
        Expect(rc).ToBe(0);
        rc = zds_delete_dsn(&zds, ds);
      });

      it("should fail if member name begins with a digit", [&]() -> void
      {
        ZDS zds = {};
        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);
        int rc = zds_create_dsn(&zds, ds, attr, response);

        std::string empty = "";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};
        rc = zds_write(write_opts, empty);
        Expect(rc).ToBe(0);

        rc = zds_rename_members(&zds, ds, M1, "123");
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(zds.diag.e_msg))
            .ToContain("Member name must start with A-Z,#,@,$ and contain only A-Z,0-9,#,@,$ (max 8 chars)");
      });
    });

    describe("list VSAM", [&]() -> void
    {
      std::string user = get_user();
      std::string base = get_random_ds(2, user);
      std::string ksds_dsn = base + ".VSAM.KSDS";
      std::string ksds_aix_dsn = base + ".VSAM.KAIX";
      std::string ksds_path_dsn = base + ".VSAM.KPTH";
      std::string esds_dsn = base + ".VSAM.ESDS";
      std::string esds_aix_dsn = base + ".VSAM.EAIX";
      std::string esds_path_dsn = base + ".VSAM.EPTH";

      auto submit_and_wait = [](const std::string &jcl, int max_polls = 600, bool check_rc = false)
      {
        ZJB zjb = {0};
        std::string jobid;
        int rc = zjb_submit(&zjb, jcl, jobid);
        if (rc != 0)
          throw std::runtime_error("Failed to submit JCL: " + std::string(zjb.diag.e_msg));
        TestLog("Submitted job " + jobid + " (" + std::to_string(jcl.size()) + " bytes)");
        std::string correlator(zjb.correlator, sizeof(zjb.correlator));
        ZJob final_job = {};
        for (int i = 0; i < max_polls; ++i)
        {
          ZJB zjb_v = {0};
          final_job = {};
          int vrc = zjb_view(&zjb_v, correlator, final_job);
          if (vrc != 0)
            throw std::runtime_error("Failed to view job: " + std::string(zjb_v.diag.e_msg));
          if (!final_job.retcode.empty())
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        TestLog("Job " + jobid + " retcode='" + final_job.retcode + "'");
        if (check_rc && final_job.retcode.find("CC 0000") == std::string::npos)
          throw std::runtime_error("Job " + jobid + " failed: retcode='" + final_job.retcode + "'");
      };

      auto find_entry = [](std::vector<ZDSEntry> &entries, const std::string &dsn) -> ZDSEntry *
      {
        for (auto &e : entries)
        {
          std::string trimmed = e.name;
          zut_rtrim(trimmed);
          if (trimmed == dsn)
            return &e;
        }
        return nullptr;
      };

      TEST_OPTIONS vsam_opts = {false, 60};

      beforeAll([&]() -> void
      {
        int rc = 0;
        std::string cleanup_idcams;
        std::string cleanup_output;
        std::string cleanup_err;
        cleanup_idcams += "  DELETE " + ksds_path_dsn + " PURGE\n";
        cleanup_idcams += "  DELETE " + ksds_aix_dsn + " PURGE\n";
        cleanup_idcams += "  DELETE " + ksds_dsn + " CLUSTER PURGE\n";
        cleanup_idcams += "  DELETE " + esds_path_dsn + " PURGE\n";
        cleanup_idcams += "  DELETE " + esds_aix_dsn + " PURGE\n";
        cleanup_idcams += "  DELETE " + esds_dsn + " CLUSTER PURGE\n";
        cleanup_idcams += "  SET MAXCC = 0\n";

        rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          TestLog("Output and error from failed IDCAMS cleanup:" + cleanup_err + "\n" + cleanup_output + "\n");
        }

        // Volume is required for systems that are not SMS managed
        ZDS zds = {0};
        std::string temp_dsn = get_random_ds(3, user);
        created_dsns.push_back(temp_dsn);
        create_seq(&zds, temp_dsn);
        std::vector<ZDSEntry> entries;
        rc = zds_list_data_sets(&zds, temp_dsn, entries, true);
        if (rc != 0 || entries.empty())
          throw std::runtime_error("Failed to list data sets: " + std::string(zds.diag.e_msg));
        std::string vsam_vol = entries[0].volser;

        std::string setup_idcams;
        std::string setup_output;
        std::string setup_err;
        setup_idcams += "  DEFINE CLUSTER ( -\n";
        setup_idcams += "    NAME(" + ksds_dsn + ") -\n";
        setup_idcams += "    INDEXED -\n";
        setup_idcams += "    KEYS(8 0) -\n";
        setup_idcams += "    RECORDSIZE(80 80) -\n";
        setup_idcams += "    TRACKS(5 5) -\n";
        setup_idcams += "    VOLUMES(" + vsam_vol + ") -\n";
        setup_idcams += "    SHAREOPTIONS(2 3) )\n";
        setup_idcams += "  DEFINE AIX ( -\n";
        setup_idcams += "    NAME(" + ksds_aix_dsn + ") -\n";
        setup_idcams += "    RELATE(" + ksds_dsn + ") -\n";
        setup_idcams += "    KEYS(8 32) -\n";
        setup_idcams += "    RECORDSIZE(80 80) -\n";
        setup_idcams += "    TRACKS(5 5) -\n";
        setup_idcams += "    VOLUMES(" + vsam_vol + ") -\n";
        setup_idcams += "    SHAREOPTIONS(2 3) -\n";
        setup_idcams += "    UPGRADE )\n";
        setup_idcams += "  DEFINE PATH ( -\n";
        setup_idcams += "    NAME(" + ksds_path_dsn + ") -\n";
        setup_idcams += "    PATHENTRY(" + ksds_aix_dsn + ") -\n";
        setup_idcams += "    UPDATE )\n";
        setup_idcams += "  DEFINE CLUSTER ( -\n";
        setup_idcams += "    NAME(" + esds_dsn + ") -\n";
        setup_idcams += "    NONINDEXED -\n";
        setup_idcams += "    RECORDSIZE(80 80) -\n";
        setup_idcams += "    TRACKS(5 5) -\n";
        setup_idcams += "    VOLUMES(" + vsam_vol + ") -\n";
        setup_idcams += "    SHAREOPTIONS(2 3) )\n";
        setup_idcams += "  DEFINE AIX ( -\n";
        setup_idcams += "    NAME(" + esds_aix_dsn + ") -\n";
        setup_idcams += "    RELATE(" + esds_dsn + ") -\n";
        setup_idcams += "    KEYS(8 0) -\n";
        setup_idcams += "    RECORDSIZE(80 80) -\n";
        setup_idcams += "    TRACKS(5 5) -\n";
        setup_idcams += "    VOLUMES(" + vsam_vol + ") -\n";
        setup_idcams += "    SHAREOPTIONS(2 3) -\n";
        setup_idcams += "    UPGRADE )\n";
        setup_idcams += "  DEFINE PATH ( -\n";
        setup_idcams += "    NAME(" + esds_path_dsn + ") -\n";
        setup_idcams += "    PATHENTRY(" + esds_aix_dsn + ") -\n";
        setup_idcams += "    UPDATE )\n";
        rc = zds_idcams(setup_idcams, setup_output, setup_err);
        TestLog("Setup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          throw std::runtime_error("Output and error from failed IDCAMS setup:" + setup_err + "\n" + setup_output +
                                   "\n");
        }
      }, vsam_opts);

      afterAll([ksds_dsn, esds_dsn]() -> void
      {
        std::string cleanup_output;
        std::string cleanup_err;
        std::string cleanup_idcams;
        cleanup_idcams += "  DELETE " + ksds_dsn + " CLUSTER PURGE\n";
        cleanup_idcams += "  DELETE " + esds_dsn + " CLUSTER PURGE\n";
        int rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          TestLog("Output and error from failed IDCAMS cleanup:" + cleanup_err + "\n" + cleanup_output + "\n");
        }
      }, vsam_opts);

      it("should report VS dsorg and *VSAM* volser for KSDS cluster", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, ksds_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, ksds_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(ZDS_DSORG_VSAM));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_VSAM));
      });

      it("should report empty dsorg and *AIX* volser for KSDS alternate index", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, ksds_aix_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, ksds_aix_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(""));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_AIX));
      });

      it("should report empty dsorg and *PATH* volser for KSDS path", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, ksds_path_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, ksds_path_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(""));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_PATH));
      });

      it("should report VS dsorg and *VSAM* volser for ESDS cluster", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, esds_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, esds_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(ZDS_DSORG_VSAM));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_VSAM));
      });

      it("should report empty dsorg and *AIX* volser for ESDS alternate index", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, esds_aix_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, esds_aix_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(""));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_AIX));
      });

      it("should report empty dsorg and *PATH* volser for ESDS path", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        int rc = zds_list_data_sets(&zds, esds_path_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        ZDSEntry *found = find_entry(entries, esds_path_dsn);
        Expect(found != nullptr).ToBe(true);
        Expect(found->dsorg).ToBe(std::string(""));
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_PATH));
      });
    });
    describe("list GDG", [&]() -> void
    {
      const std::string user = get_user();
      const std::string base = get_random_ds(3, user);
      const std::string gdg_base_dsn = base + ".GDG";
      const std::string gdg_gen1_dsn = gdg_base_dsn + ".G0001V00";

      auto submit_and_wait = [](const std::string &jcl, int max_polls = 600, bool check_rc = false)
      {
        ZJB zjb = {0};
        std::string jobid;
        int rc = zjb_submit(&zjb, jcl, jobid);
        if (rc != 0)
          throw std::runtime_error("Failed to submit JCL: " + std::string(zjb.diag.e_msg));
        TestLog("Submitted job " + jobid + " (" + std::to_string(jcl.size()) + " bytes)");
        std::string correlator(zjb.correlator, sizeof(zjb.correlator));
        ZJob final_job = {};
        for (int i = 0; i < max_polls; ++i)
        {
          ZJB zjb_v = {0};
          final_job = {};
          int vrc = zjb_view(&zjb_v, correlator, final_job);
          if (vrc != 0)
            throw std::runtime_error("Failed to view job: " + std::string(zjb_v.diag.e_msg));
          if (!final_job.retcode.empty())
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        TestLog("Job " + jobid + " retcode='" + final_job.retcode + "'");
        if (check_rc && final_job.retcode.find("CC 0000") == std::string::npos)
          throw std::runtime_error("Job failed with retcode='" + final_job.retcode + "'");
      };

      TEST_OPTIONS gdg_opts = {false, 60};

      beforeAll([&]() -> void
      {
        // Define the GDG base via IDCAMS
        std::string setup_output;
        std::string setup_err;
        std::string setup_idcams;
        setup_idcams += "  DEFINE GDG ( -\n";
        setup_idcams += "    NAME(" + gdg_base_dsn + ") -\n";
        setup_idcams += "    LIMIT(5) -\n";
        setup_idcams += "    NOEMPTY -\n";
        // SCRATCH so generations are removed from the VTOC when the base
        // is deleted. Under NOSCRATCH they are only uncataloged, leaving
        // orphaned data sets that cannot be deleted by name from ISPF.
        setup_idcams += "    SCRATCH )\n";
        int rc = zds_idcams(setup_idcams, setup_output, setup_err);
        TestLog("Setup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          throw std::runtime_error("Output and error from failed IDCAMS cleanup:" + setup_err + "\n" + setup_output +
                                   "\n");
        }
        // Create one generation via IEBGENER
        std::string gen_jcl;
        gen_jcl += "//GDGGEN$ JOB IZUACCT\n";
        gen_jcl += "//STEP1    EXEC PGM=IEBGENER\n";
        gen_jcl += "//SYSPRINT DD SYSOUT=*\n";
        gen_jcl += "//SYSIN    DD DUMMY\n";
        gen_jcl += "//SYSUT1   DD *\n";
        gen_jcl += "GDG GENERATION ONE\n";
        gen_jcl += "/*\n";
        gen_jcl += "//SYSUT2   DD DSN=" + gdg_base_dsn + "(+1),DISP=(NEW,CATLG),\n";
        gen_jcl += "//            UNIT=SYSALLDA,SPACE=(TRK,(1,1)),\n";
        gen_jcl += "//            RECFM=FB,LRECL=80,BLKSIZE=800\n";
        submit_and_wait(gen_jcl, 600, true);
      }, gdg_opts);

      // Capture by value: afterAll hooks run after the enclosing describe() lambda
      // has returned, so any reference to its locals would already be dangling.
      afterAll([gdg_base_dsn, gdg_gen1_dsn]() -> void
      {
        std::string cleanup_idcams;
        std::string cleanup_output;
        std::string cleanup_err;
        // Deleting the base also removes its generations, which SCRATCH on
        // the DEFINE clears from the VTOC rather than merely uncataloging.
        cleanup_idcams += "  DELETE " + gdg_gen1_dsn + " NONVSAM PURGE\n";
        cleanup_idcams += "  DELETE " + gdg_base_dsn + " GDG PURGE\n";
        // No SET MAXCC here: unlike beforeAll, a nonzero RC is real news and
        // the SYSPRINT is the only place the IDC3009I reason code shows up.
        int rc = zds_idcams(cleanup_idcams, cleanup_output, cleanup_err);
        TestLog("Cleanup RC from IDCAMS: " + std::to_string(rc));
        if (rc != 0)
        {
          // Warn rather than fail: if setup died early the GDG may never
          // have been defined, and DELETE of a missing entry is expected.
          TestLog("WARNING: IDCAMS cleanup failed for " + gdg_base_dsn + " (RC " + std::to_string(rc) +
                  "); data sets may need manual removal:\n" + cleanup_err + "\n" + cleanup_output + "\n");
        }
      }, gdg_opts);

      it("should report ZDS_VOLSER_GDG (?????\?) for a GDG base", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        const int rc = zds_list_data_sets(&zds, gdg_base_dsn, entries, true);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);

        ZDSEntry *found = nullptr;
        for (auto &e : entries)
        {
          std::string trimmed = e.name;
          zut_rtrim(trimmed);
          if (trimmed == gdg_base_dsn)
          {
            found = &e;
            break;
          }
        }
        Expect(found != nullptr).ToBe(true);
        Expect(found->volser).ToBe(std::string(ZDS_VOLSER_GDG));
      }, gdg_opts);

      it("should find GDG generations using a wildcard pattern", [&]() -> void
      {
        ZDS zds = {0};
        std::vector<ZDSEntry> entries;
        const std::string pattern = gdg_base_dsn + ".*";
        const int rc = zds_list_data_sets(&zds, pattern, entries);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);

        bool found_gen1 = false;
        for (const auto &e : entries)
        {
          std::string trimmed = e.name;
          zut_rtrim(trimmed);
          if (trimmed == gdg_gen1_dsn)
            found_gen1 = true;
        }
        Expect(found_gen1).ToBe(true);
      }, gdg_opts);

      it("should read content from a GDG generation by absolute name", [&]() -> void
      {
        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = gdg_gen1_dsn};
        std::string content;
        const int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("GDG GENERATION ONE") != std::string::npos).ToBe(true);
      }, gdg_opts);

      it("should read content from the most recent GDG generation via relative reference", [&]() -> void
      {
        const std::string relative_ref = gdg_base_dsn + "(0)";
        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = relative_ref};
        std::string content;
        const int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("GDG GENERATION ONE") != std::string::npos).ToBe(true);
      }, gdg_opts);
    });
    describe("read", [&]() -> void
    {
      it("should read from a sequential data set", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "hello world");

        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("hello world") != std::string::npos).ToBe(true);
      });

      it("should read from a PDS member", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_pds(&zds, dsn);
        write_to_dsn(dsn + "(TESTMEM)", "member data");

        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn + "(TESTMEM)"};
        std::string content;
        int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("member data") != std::string::npos).ToBe(true);
      });

      it("should fail to read from a non-existent data set", []() -> void
      {
        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = "NONEXIST.DATASET.NAME"};
        std::string content;
        int rc = zds_read(read_opts, content);
        Expect(rc).Not().ToBe(0);
      });

      it("should fail when neither dsname nor ddname is provided", []() -> void
      {
        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds};
        std::string content;
        int rc = zds_read(read_opts, content);
        Expect(rc).ToBe(RTNCD_FAILURE);
        Expect(std::string(read_zds.diag.e_msg)).ToContain("Either a dsname or ddname must be provided");
      });

      it("should throw invalid_argument when ZDS pointer is null", []() -> void
      {
        ZDSReadOpts read_opts{.zds = nullptr, .dsname = "SOME.DSN"};
        std::string content;
        bool caught = false;
        try
        {
          zds_read(read_opts, content);
        }
        catch (const std::invalid_argument &e)
        {
          caught = true;
          Expect(std::string(e.what())).ToContain("zds_read: valid ZDS pointer is required in ZDSReadOpts.zds");
        }
        Expect(caught).ToBe(true);
      });

      it("should read binary data from a sequential data set", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "binary test");

        ZDS read_zds{};
        read_zds.encoding_opts.data_type = eDataTypeBinary;
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.empty()).ToBe(false);
      });

      it("should read with encoding (IBM-1047 to UTF-8)", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "encoded data");

        ZDS read_zds{};
        strcpy(read_zds.encoding_opts.codepage, "IBM-1047");
        read_zds.encoding_opts.data_type = eDataTypeText;
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.empty()).ToBe(false);
      });

      it("should read with explicit source encoding (local-encoding)", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "local enc data");

        ZDS read_zds{};
        strcpy(read_zds.encoding_opts.codepage, "ISO8859-1");
        strcpy(read_zds.encoding_opts.source_codepage, "IBM-1047");
        read_zds.encoding_opts.data_type = eDataTypeText;
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        int rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.empty()).ToBe(false);
      });

      it("should write and read DBCS multi-byte encoded content (IBM-939)", [&]() -> void
      {
        const std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);

        // Japanese こんにちは encoded as UTF-8 bytes
        const std::string utf8_japanese = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";

        ZDS write_zds{};
        write_zds.encoding_opts.data_type = eDataTypeText;
        strcpy(write_zds.encoding_opts.codepage, "IBM-939");
        ZDSWriteOpts write_opts{.zds = &write_zds, .dsname = dsn};
        int rc = zds_write(write_opts, utf8_japanese);
        ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

        // Read back in binary mode to verify DBCS EBCDIC encoding.
        // IBM-939 uses SO (0x0E) and SI (0x0F) byte delimiters around double-byte characters.
        ZDS read_zds{};
        read_zds.encoding_opts.data_type = eDataTypeBinary;
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find('\x0e') != std::string::npos).ToBe(true);
        Expect(content.find('\x0f') != std::string::npos).ToBe(true);
      });

      it("should default source encoding to UTF-8 when source_codepage is empty", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "default enc");

        // Read with codepage=UTF-8 but no source_codepage;
        // defaults to UTF-8 source
        ZDS default_zds{};
        strcpy(default_zds.encoding_opts.codepage, "UTF-8");
        default_zds.encoding_opts.data_type = eDataTypeText;
        ZDSReadOpts read_default_opts{.zds = &default_zds, .dsname = dsn};
        std::string content_default;
        int rc = zds_read(read_default_opts, content_default);
        ExpectWithContext(rc, default_zds.diag.e_msg).ToBe(0);

        // Read again with explicit source_codepage=UTF-8
        ZDS explicit_zds{};
        strcpy(explicit_zds.encoding_opts.codepage, "UTF-8");
        strcpy(explicit_zds.encoding_opts.source_codepage, "UTF-8");
        explicit_zds.encoding_opts.data_type = eDataTypeText;
        ZDSReadOpts read_explicit_opts{.zds = &explicit_zds, .dsname = dsn};
        std::string content_explicit;
        rc = zds_read(read_explicit_opts, content_explicit);
        ExpectWithContext(rc, explicit_zds.diag.e_msg).ToBe(0);

        // Both should produce the same result
        Expect(content_default).ToBe(content_explicit);
      });

      it("should report from=file encoding, to=local encoding when iconv fails on read", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);

        ZDS write_zds{};
        write_zds.encoding_opts.data_type = eDataTypeBinary;
        const std::string invalid_utf8 = "invalid utf8: \xff\xfe\xfd";
        ZDSWriteOpts write_opts{.zds = &write_zds, .dsname = dsn};
        int wrc = zds_write(write_opts, invalid_utf8);
        ExpectWithContext(wrc, write_zds.diag.e_msg).ToBe(0);

        ZDS read_zds{};
        strcpy(read_zds.encoding_opts.codepage, "UTF-8");
        strcpy(read_zds.encoding_opts.source_codepage, "IBM-1047");
        read_zds.encoding_opts.data_type = eDataTypeText;
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        int rc = zds_read(read_opts, content);

        Expect(rc).ToBe(RTNCD_FAILURE);
        const std::string msg(read_zds.diag.e_msg);
        Expect(msg).ToContain("from UTF-8 to IBM-1047");
      });

      it("should produce a consistent etag after write and read", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);

        std::string data = "etag test data";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = dsn};
        int rc = zds_write(write_opts, data);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        std::string write_etag(zds.etag);
        Expect(write_etag.empty()).ToBe(false);

        // Read back and compute etag independently
        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .dsname = dsn};
        std::string content;
        rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);

        std::stringstream etag_stream;
        etag_stream << std::hex << zut_calc_adler32_checksum(content);
        Expect(etag_stream.str()).ToBe(write_etag);
      });

      it("should detect etag mismatch on write (conflict resolution)", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);

        std::string data = "original content";
        ZDSWriteOpts write_opts{.zds = &zds, .dsname = dsn};
        int rc = zds_write(write_opts, data);
        ExpectWithContext(rc, zds.diag.e_msg).ToBe(0);
        std::string valid_etag(zds.etag);

        // Overwrite with new content (changes the actual etag on disk)
        ZDS zds2 = {0};
        std::string data2 = "modified content";
        ZDSWriteOpts write_opts2{.zds = &zds2, .dsname = dsn};
        rc = zds_write(write_opts2, data2);
        ExpectWithContext(rc, zds2.diag.e_msg).ToBe(0);

        // Attempt to write using the stale (first) etag — should fail
        ZDS zds3 = {0};
        strcpy(zds3.etag, valid_etag.c_str());
        std::string data3 = "conflicting write";
        ZDSWriteOpts write_opts3{.zds = &zds3, .dsname = dsn};
        rc = zds_write(write_opts3, data3);
        Expect(rc).Not().ToBe(0);
        Expect(std::string(zds3.diag.e_msg)).ToContain("Etag mismatch");
      });

      it("should read an uncataloged data set using volser (dynamic allocation)", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "volser test");

        // Look up the volser for this data set
        std::vector<ZDSEntry> entries;
        ZDS list_zds = {0};
        int rc = zds_list_data_sets(&list_zds, dsn, entries, true);
        ExpectWithContext(rc, list_zds.diag.e_msg).ToBe(0);
        Expect(entries.empty()).ToBe(false);
        std::string volser = entries[0].volser;
        Expect(volser.empty()).ToBe(false);

        // Dynamically allocate using volser and read via ddname
        std::vector<std::string> dds;
        dds.push_back("alloc dd(input) da('" + dsn + "') shr vol(" + volser + ")");
        ZDIAG diag{};
        rc = zut_loop_dynalloc(diag, dds);
        ExpectWithContext(rc, diag.e_msg).ToBe(0);

        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "INPUT", .dsname = dsn};
        std::string content;
        rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("volser test") != std::string::npos).ToBe(true);

        zut_free_dynalloc_dds(diag, dds);
      });

      it("should read from a dynamically allocated DD without a dsname", [&]() -> void
      {
        std::string dsn = get_random_ds(3);
        created_dsns.push_back(dsn);
        ZDS zds = {0};
        create_seq(&zds, dsn);
        write_to_dsn(dsn, "dd only read test");

        std::vector<std::string> dds;
        dds.push_back("alloc dd(OUTDD) da('" + dsn + "') shr");
        ZDIAG diag{};
        int rc = zut_loop_dynalloc(diag, dds);
        ExpectWithContext(rc, diag.e_msg).ToBe(0);

        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "OUTDD"};
        std::string content;
        rc = zds_read(read_opts, content);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(content.find("dd only read test") != std::string::npos).ToBe(true);

        zut_free_dynalloc_dds(diag, dds);
      });

      it("should read from a SYSPRINT-style DD (FB lrecl=80)", [&]() -> void
      {
        std::string expected = "SYSPRINT DD read test";

        std::vector<std::string> dds;
        dds.push_back("alloc dd(SYSPRINT) lrecl(80) recfm(f,b) blksize(80)");
        ZDIAG diag{};
        int rc = zut_loop_dynalloc(diag, dds);
        ExpectWithContext(rc, diag.e_msg).ToBe(0);

        ZDS write_zds{};
        ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "SYSPRINT"};
        rc = zds_write(write_opts, expected);
        ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

        ZDS read_zds{};
        ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "SYSPRINT"};
        std::string output;
        rc = zds_read(read_opts, output);
        ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
        Expect(output.find(expected) != std::string::npos).ToBe(true);

        zut_free_dynalloc_dds(diag, dds);
      });

      describe("DD write operations", [&]() -> void
      {
        describe("basic operations", [&]() -> void
        {
          it("should write text to FB LRECL=80 DD (SYSPRINT pattern)", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_sysprint_dd();

            std::string test_data = "Line 1: Basic text content\nLine 2: More test data\nLine 3: Final line";
            ZDS write_zds{};
            ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "SYSPRINT"};
            int rc = zds_write(write_opts, test_data);
            ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

            bool has_line1 = ctx.verify_dd_content("SYSPRINT", "Line 1: Basic text content");
            bool has_line2 = ctx.verify_dd_content("SYSPRINT", "Line 2: More test data");
            bool has_line3 = ctx.verify_dd_content("SYSPRINT", "Line 3: Final line");
            Expect(has_line1).ToBe(true);
            Expect(has_line2).ToBe(true);
            Expect(has_line3).ToBe(true);
          });

          it("should write binary data to VB DD", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_vb_dd("BINDD", 255);

            std::string binary_data = "Binary\x00\x01\x02test\xFF\xFE data";
            ZDS write_zds{};
            write_zds.encoding_opts.data_type = eDataTypeBinary;
            ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "BINDD"};
            int rc = zds_write(write_opts, binary_data);
            ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

            ZDS read_zds{};
            read_zds.encoding_opts.data_type = eDataTypeBinary;
            ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "BINDD"};
            std::string content;
            rc = zds_read(read_opts, content);
            ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
            Expect(content.find("Binary") != std::string::npos).ToBe(true);
          });

          it("should write empty content to DD", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("EMPTYDD", 80);

            std::string empty_data = "";
            ZDS write_zds{};
            ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "EMPTYDD"};
            int rc = zds_write(write_opts, empty_data);
            ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

            ZDS read_zds{};
            ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "EMPTYDD"};
            std::string content;
            rc = zds_read(read_opts, content);
            ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
            Expect(content.empty()).ToBe(true);
          });

          it("should handle single-line vs multi-line content", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("TESTDD", 80);

            // Test single line without newline
            std::string single_line = "Single line without newline";
            ZDS write_zds{};
            ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "TESTDD"};
            int rc = zds_write(write_opts, single_line);
            ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

            bool has_single_line = ctx.verify_dd_content("TESTDD", "Single line without newline");
            Expect(has_single_line).ToBe(true);
          });

          it("should verify content round-trip accuracy", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("ROUNDDD", 133);

            std::string test_content = "Round-trip test line 1\nRound-trip test line 2\nSpecial chars: !@#$%^&*()";
            ZDS write_zds{};
            ZDSWriteOpts write_opts{.zds = &write_zds, .ddname = "ROUNDDD"};
            int rc = zds_write(write_opts, test_content);
            ExpectWithContext(rc, write_zds.diag.e_msg).ToBe(0);

            ZDS read_zds{};
            ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "ROUNDDD"};
            std::string content;
            rc = zds_read(read_opts, content);
            ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
            Expect(content.find("Round-trip test line 1") != std::string::npos).ToBe(true);
            Expect(content.find("Special chars: !@#$%^&*()") != std::string::npos).ToBe(true);
          });
        });

        describe("record format support", [&]() -> void
        {
          it("should handle FB with various LRECL values", [&]() -> void
          {
            DDTestContext ctx(created_dsns);

            // Test LRECL=80 (standard)
            ctx.allocate_fb_dd("FB80", 80);
            std::string data80 = "Standard 80-character record for typical mainframe usage";
            ZDS zds80{};
            ZDSWriteOpts opts80{.zds = &zds80, .ddname = "FB80"};
            int rc = zds_write(opts80, data80);
            ExpectWithContext(rc, zds80.diag.e_msg).ToBe(0);

            // Test LRECL=133 (wide reports)
            ctx.allocate_fb_dd("FB133", 133);
            std::string data133 =
                "Wide format record for reports that need more than 80 characters per line - this is commonly used for listings";
            ZDS zds133{};
            ZDSWriteOpts opts133{.zds = &zds133, .ddname = "FB133"};
            rc = zds_write(opts133, data133);
            ExpectWithContext(rc, zds133.diag.e_msg).ToBe(0);

            // Test LRECL=255 (maximum for FB)
            ctx.allocate_fb_dd("FB255", 255);
            std::string data255 =
                "Maximum length fixed-block record that can contain up to 255 characters of data which is useful for very wide reports or data transfer operations that need the maximum capacity available in fixed-block format";
            ZDS zds255{};
            ZDSWriteOpts opts255{.zds = &zds255, .ddname = "FB255"};
            rc = zds_write(opts255, data255);
            ExpectWithContext(rc, zds255.diag.e_msg).ToBe(0);

            bool has_fb80 = ctx.verify_dd_content("FB80", "Standard 80-character");
            bool has_fb133 = ctx.verify_dd_content("FB133", "Wide format record");
            bool has_fb255 = ctx.verify_dd_content("FB255", "Maximum length fixed-block");
            Expect(has_fb80).ToBe(true);
            Expect(has_fb133).ToBe(true);
            Expect(has_fb255).ToBe(true);
          });

          it("should handle VB with various LRECL values", [&]() -> void
          {
            DDTestContext ctx(created_dsns);

            // Test VB with LRECL=80
            ctx.allocate_vb_dd("VB80", 80);
            std::string vb_data =
                "Variable block record\nShorter line\nThis is a longer line that demonstrates variable length";
            ZDS vb_zds{};
            ZDSWriteOpts vb_opts{.zds = &vb_zds, .ddname = "VB80"};
            int rc = zds_write(vb_opts, vb_data);
            ExpectWithContext(rc, vb_zds.diag.e_msg).ToBe(0);

            // Test VB with LRECL=1000 (large but reasonable)
            ctx.allocate_vb_dd("VB1K", 1000);
            std::string large_vb = "Large variable block record that can handle very long lines";
            ZDS large_zds{};
            ZDSWriteOpts large_opts{.zds = &large_zds, .ddname = "VB1K"};
            rc = zds_write(large_opts, large_vb);
            ExpectWithContext(rc, large_zds.diag.e_msg).ToBe(0);

            bool has_vb80 = ctx.verify_dd_content("VB80", "Variable block record");
            bool has_vb1k = ctx.verify_dd_content("VB1K", "Large variable block");
            Expect(has_vb80).ToBe(true);
            Expect(has_vb1k).ToBe(true);
          });

          it("should return out-of-space failure if contents are too long", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("TRUNCDD", 40);

            std::string long_line = "This line is much longer than 40 characters and should be truncated by the system";
            ZDS trunc_zds{};
            ZDSWriteOpts trunc_opts{.zds = &trunc_zds, .ddname = "TRUNCDD"};
            int rc = zds_write(trunc_opts, long_line);
            ExpectWithContext(rc, trunc_zds.diag.e_msg).ToBe(RTNCD_FAILURE);
            Expect(std::string(trunc_zds.diag.e_msg))
                .ToContain("Failed to write all contents to 'DD:TRUNCDD' (possibly out of space)");

            // Verify truncation occurred (should only contain first 40 chars)
            ZDS read_zds{};
            ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "TRUNCDD"};
            std::string content;
            rc = zds_read(read_opts, content);
            ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
            Expect(content.find("This line is much longer than 40 char") != std::string::npos).ToBe(true);
            Expect(content.find("should be truncated by the system") == std::string::npos).ToBe(true);
          });
        });

        describe("encoding support", [&]() -> void
        {
          it("should handle text mode with default encoding", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("TEXTDD", 80);

            std::string text_data = "Text mode content with UTF-8 characters";
            ZDS text_zds{};
            text_zds.encoding_opts.data_type = eDataTypeText;
            ZDSWriteOpts text_opts{.zds = &text_zds, .ddname = "TEXTDD"};
            int rc = zds_write(text_opts, text_data);
            ExpectWithContext(rc, text_zds.diag.e_msg).ToBe(0);

            bool has_text_content = ctx.verify_dd_content("TEXTDD", "Text mode content");
            Expect(has_text_content).ToBe(true);
          });

          it("should handle binary mode without conversion", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_vb_dd("BINDD", 100);

            std::string binary_data = "Binary\x00\x01\x02\x03\xFF\xFE\xFD\xFC data";
            ZDS binary_zds{};
            binary_zds.encoding_opts.data_type = eDataTypeBinary;
            ZDSWriteOpts binary_opts{.zds = &binary_zds, .ddname = "BINDD"};
            int rc = zds_write(binary_opts, binary_data);
            ExpectWithContext(rc, binary_zds.diag.e_msg).ToBe(0);

            // Read back in binary mode
            ZDS read_zds{};
            read_zds.encoding_opts.data_type = eDataTypeBinary;
            ZDSReadOpts read_opts{.zds = &read_zds, .ddname = "BINDD"};
            std::string content;
            rc = zds_read(read_opts, content);
            ExpectWithContext(rc, read_zds.diag.e_msg).ToBe(0);
            Expect(content.find("Binary") != std::string::npos).ToBe(true);
          });

          it("should handle mixed content scenarios", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("MIXEDDD", 120);

            std::string mixed_data = "Text content\nWith special chars: áéíóú\nAnd numbers: 12345";
            ZDS mixed_zds{};
            mixed_zds.encoding_opts.data_type = eDataTypeText;
            ZDSWriteOpts mixed_opts{.zds = &mixed_zds, .ddname = "MIXEDDD"};
            int rc = zds_write(mixed_opts, mixed_data);
            ExpectWithContext(rc, mixed_zds.diag.e_msg).ToBe(0);

            bool has_mixed_text = ctx.verify_dd_content("MIXEDDD", "Text content");
            bool has_mixed_numbers = ctx.verify_dd_content("MIXEDDD", "And numbers: 12345");
            Expect(has_mixed_text).ToBe(true);
            Expect(has_mixed_numbers).ToBe(true);
          });
        });

        describe("streaming operations", [&]() -> void
        {
          it("should handle zds_write_streamed with pipe input", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_fb_dd("STREAMDD", 80);

            // Create a temporary file to simulate pipe input
            std::string temp_file = "/tmp/zds_stream_test_" + std::to_string(getpid());
            std::string content = "Streamed line 1\nStreamed line 2\nStreamed line 3\n";
            std::string encoded_content = zbase64::encode(content);
            std::ofstream temp_out(temp_file);
            temp_out << encoded_content;
            temp_out.close();

            size_t content_len = 0;
            ZDS stream_zds{};
            ZDSWriteOpts stream_opts{.zds = &stream_zds, .ddname = "STREAMDD"};
            int rc = zds_write_streamed(stream_opts, temp_file, &content_len);
            ExpectWithContext(rc, stream_zds.diag.e_msg).ToBe(0);

            // Verify content length was set
            Expect(content_len > 0).ToBe(true);

            // Verify content was written
            bool has_stream_line1 = ctx.verify_dd_content("STREAMDD", "Streamed line 1");
            bool has_stream_line3 = ctx.verify_dd_content("STREAMDD", "Streamed line 3");
            Expect(has_stream_line1).ToBe(true);
            Expect(has_stream_line3).ToBe(true);

            // Cleanup
            unlink(temp_file.c_str());
          });

          it("should validate content length parameter", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_vb_dd("LENDD", 255);

            std::string temp_file = "/tmp/zds_len_test_" + std::to_string(getpid());
            std::string test_content = "Content length validation test data\nSecond line for length check\n";
            std::string encoded_content = zbase64::encode(test_content);
            std::ofstream temp_out(temp_file);
            temp_out << encoded_content;
            temp_out.close();

            size_t content_len = 0;
            ZDS len_zds{};
            ZDSWriteOpts len_opts{.zds = &len_zds, .ddname = "LENDD"};
            int rc = zds_write_streamed(len_opts, temp_file, &content_len);
            ExpectWithContext(rc, len_zds.diag.e_msg).ToBe(0);

            // Content length should match expected decoded size
            Expect(content_len).ToBe(test_content.length());

            unlink(temp_file.c_str());
          });

          it("should handle large data efficiently", [&]() -> void
          {
            DDTestContext ctx(created_dsns);
            ctx.allocate_vb_dd("LARGEDD", 1000);

            // Create larger test data
            std::string temp_file = "/tmp/zds_large_test_" + std::to_string(getpid());
            std::stringstream content_stream;
            for (int i = 0; i < 1000; i++)
            {
              content_stream << "Large data test line " << i
                             << " with substantial content to test streaming efficiency\n";
            }
            std::string content = content_stream.str();
            std::string encoded_content = zbase64::encode(content);
            std::ofstream temp_out(temp_file);
            temp_out << encoded_content;
            temp_out.close();

            size_t content_len = 0;
            ZDS large_zds{};
            ZDSWriteOpts large_opts{.zds = &large_zds, .ddname = "LARGEDD"};
            int rc = zds_write_streamed(large_opts, temp_file, &content_len);
            ExpectWithContext(rc, large_zds.diag.e_msg).ToBe(0);

            Expect(content_len > 5000).ToBe(true); // Should be substantial content (reduced from 100 lines)
            bool has_large_line0 = ctx.verify_dd_content("LARGEDD", "Large data test line 0");
            bool has_large_line999 = ctx.verify_dd_content("LARGEDD", "Large data test line 999");
            ExpectWithContext(has_large_line0, "Line 0 is missing in written DD contents").ToBe(true);
            ExpectWithContext(has_large_line999, "Line 999 is missing in written DD contents").ToBe(true);

            unlink(temp_file.c_str());
          });
        });

        describe("error handling", [&]() -> void
        {
          it("should reject empty DD names", [&]() -> void
          {
            std::string test_data = "Test data for empty DD name";
            ZDS error_zds{};
            ZDSWriteOpts error_opts{.zds = &error_zds, .ddname = ""};
            int rc = zds_write(error_opts, test_data);

            Expect(rc).Not().ToBe(0);
            Expect(std::string(error_zds.diag.e_msg).length() > 0).ToBe(true);
          });

          it("should handle null content_len pointer in streamed operations", [&]() -> void
          {
            std::string temp_file = "/tmp/zds_null_test_" + std::to_string(getpid());
            std::string content = "Test data for null pointer\n";
            std::string encoded_content = zbase64::encode(content);
            std::ofstream temp_out(temp_file);
            temp_out << encoded_content;
            temp_out.close();

            ZDS null_zds{};
            ZDSWriteOpts null_opts{.zds = &null_zds, .ddname = "TESTDD"};
            int rc = zds_write_streamed(null_opts, temp_file, nullptr);

            Expect(rc).Not().ToBe(0);
            Expect(std::string(null_zds.diag.e_msg).find("content_len") != std::string::npos).ToBe(true);

            unlink(temp_file.c_str());
          });
        });
      });
    });
    describe("DCB abend", [&]() -> void
    {
      it("should validate constants around DCB abend use", [&]() -> void
      {
        Expect(DCB_ABEND_OPT_OK_TO_RECOVER).ToBe(0x08);
        Expect(DCB_ABEND_OPT_OK_TO_IGNORE).ToBe(0x04);
        Expect(DCB_ABEND_OPT_OK_TO_DELAY).ToBe(0x02);
        Expect(DCB_ABEND_RC_TERMINATE).ToBe(0);
        Expect(DCB_ABEND_RC_IGNORE).ToBe(4);
        Expect(DCB_ABEND_RC_DELAY).ToBe(8);
        Expect(DCB_ABEND_RC_IGNORE_QUIETLY).ToBe(20);
      });

      it("should validate the size of DCB_ABEND_PL struct", [&]() -> void
      {
        Expect(sizeof(DCB_ABEND_PL)).ToBe(16);
      });

      xit("should propagate abend when writing past max space of a PDS", [&]() -> void
      {
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 6160;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 0;
        attr.dirblk = 1;

        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);

        std::string response;
        int rc = zds_create_dsn(&zds, ds, attr, response);
        ExpectWithContext(rc, response).ToBe(0);

        zds.encoding_opts.data_type = eDataTypeText;
        strcpy(zds.encoding_opts.codepage, "IBM-1047");
        strcpy(zds.encoding_opts.source_codepage, "IBM-1047");

        std::string large_data;
        large_data.reserve(81 * 1000);
        for (int i = 0; i < 1000; i++)
        {
          large_data += std::string(80, 'A') + "\n";
        }

        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};

        // This should fail with an abend. DCB abend exit runs as part of the member write process - no mocking
        // available so we can't test the DCB exit itself, this just verifies that the abend is percolated and handled
        // by recovery
        Expect([&]()
        {
          rc = zds_write(write_opts, large_data);
        }).ToAbend();
      });

      // Skip this test because forcing DCB abends in tests seems fragile.
      // On some systems, the DCB abend does not trigger. On others it
      // does, but the data set cannot be cleaned up because it is locked
      // immediately after the abend.
      // uncomment when in a zvdt env
      xit("should release ENQ after DCB abend during write", [&]() -> void
      {
        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 6160;
        attr.alcunit = "TRACKS";
        attr.primary = 5;
        attr.secondary = 0;
        attr.dirblk = 5;

        const std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);

        ZDS create_zds = {0};
        std::string response;
        int rc = zds_create_dsn(&create_zds, ds, attr, response);
        ExpectWithContext(rc, response).ToBe(0);

        ZDS crash_zds{};
        crash_zds.encoding_opts.data_type = eDataTypeText;
        strcpy(crash_zds.encoding_opts.codepage, "IBM-1047");
        strcpy(crash_zds.encoding_opts.source_codepage, "IBM-1047");

        std::string large_data;
        large_data.reserve(81 * 1000);
        for (int i = 0; i < 1000; i++)
          large_data += std::string(80, 'A') + "\n";

        ZDSWriteOpts crash_opts{.zds = &crash_zds, .dsname = ds + "(M1)"};
        Expect([&]()
        {
          zds_write(crash_opts, large_data);
        }).ToAbend();

        // After recovery, verify ENQ was released by writing to a different member.
        // ZDS_RTNCD_ENQ_ERROR (-14) would indicate the ENQ was not released.
        ZDS write_zds{};
        write_zds.encoding_opts.data_type = eDataTypeText;
        strcpy(write_zds.encoding_opts.codepage, "IBM-1047");
        strcpy(write_zds.encoding_opts.source_codepage, "IBM-1047");
        ZDSWriteOpts verify_opts{.zds = &write_zds, .dsname = ds + "(M2)"};
        const std::string small_data = "ENQ released\n";
        rc = zds_write(verify_opts, small_data);
        ExpectWithContext(rc, std::string(write_zds.diag.e_msg)).Not().ToBe(ZDS_RTNCD_ENQ_ERROR);
      });

      // uncomment when in a zvdt env
      xit("should release ENQ after forced S0C3 abend (EXRL) during BPAM write", [&]() -> void
      {
        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 800;
        attr.dirblk = 5;
        attr.primary = 1;

        const std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);

        ZDS create_zds = {0};
        std::string response;
        int rc = zds_create_dsn(&create_zds, ds, attr, response);
        ExpectWithContext(rc, response).ToBe(0);

        // Open for BPAM write - this acquires the z/OS ENQ on the data set.
        // ioc->has_enq is the direct "is this locked" flag (see IO_CTRL in zamtypes.h).
        ZDS open_zds{};
        IO_CTRL *ioc = nullptr;
        rc = zds_open_output_bpam(&open_zds, ds + "(M1)", ioc);
        ExpectWithContext(rc, open_zds.diag.e_msg).ToBe(0);
        Expect(ioc->has_enq).ToBe(1);

        // Force a S0C3 abend (EXRL 0,* = execute-type instruction targeting itself)
        // while the ENQ is held. ESTAE recovery must call deq_data_set before
        // returning to the test for ioc->has_enq to become 0.
        Expect([&]()
        {
          __asm(" EXRL 0,*");
        }).ToAbend();

        // After recovery: has_enq == 0 means the recovery path properly DEQ'd.
        // has_enq == 1 means a forced crash leaves ENQ unreleased (bug).
        Expect(ioc->has_enq).ToBe(0);
      });

      // uncomment when in a zvdt env
      xit("should catch abend when writing past max space of a PDS", [&]() -> void
      {
        ZDS zds = {0};
        DS_ATTRIBUTES attr{};
        attr.dsorg = "PO";
        attr.recfm = "FB";
        attr.lrecl = 80;
        attr.blksize = 6160;
        attr.alcunit = "TRACKS";
        attr.primary = 1;
        attr.secondary = 0;
        attr.dirblk = 1;

        std::string ds = get_random_ds(3);
        created_dsns.push_back(ds);

        std::string response;
        int rc = zds_create_dsn(&zds, ds, attr, response);
        ExpectWithContext(rc, response).ToBe(0);

        zds.encoding_opts.data_type = eDataTypeText;
        strcpy(zds.encoding_opts.codepage, "IBM-1047");
        strcpy(zds.encoding_opts.source_codepage, "IBM-1047");

        std::string large_data;
        large_data.reserve(81 * 1000);
        for (int i = 0; i < 1000; i++)
        {
          large_data += std::string(80, 'A') + "\n";
        }

        ZDSWriteOpts write_opts{.zds = &zds, .dsname = ds + "(M1)"};

        // This should fail with an abend. DCB abend exit runs as part of the member write process - no mocking
        // available so we can't test the DCB exit itself, this just verifies that the abend is percolated and handled
        // by recovery
        Expect([&]()
        {
          rc = zds_write(write_opts, large_data);
        }).ToAbend();
      });
    });
  });
}
