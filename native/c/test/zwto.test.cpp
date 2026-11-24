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
#include <string>

#include "ztest.hpp"
#include "../zwto.h"

// zwto_debug builds a WTO_BUF and hands it to wto(). Off Metal, wto() is a no-op
// and the buffer is discarded, so we redefine wto() here to capture that buffer
// instead. zwto_debug expands at its point of use (in run_debug below), so this
// override changes only this test's expansion -- zwto.h itself is untouched. The
// sprintf/snprintf inside the macro are plain library calls in this LE process,
// so the real bounding/clamping logic runs exactly as it does in production.
static WTO_BUF g_captured;
#undef wto
#define wto(buf_ptr) (memcpy(&g_captured, (buf_ptr), sizeof(WTO_BUF)), 0)

using namespace ztst;

// Runs the real zwto_debug macro on a single "%s" argument and returns the
// resulting WTO length, copying the message text into out_msg (MAX_WTO_TEXT).
static int run_debug(const char *input, char *out_msg)
{
  memset(&g_captured, 0, sizeof(g_captured));
  zwto_debug("%s", input);
  memcpy(out_msg, g_captured.msg, sizeof(g_captured.msg));
  return g_captured.len;
}

// The "ZWEX0001I " prefix written before the caller's text.
static const int PREFIX_LEN = 10;

void zwto_tests()
{
  describe("zwto_debug message bounding", []() -> void
  {
    it("prefixes the message and preserves short text", []() -> void
    {
      char msg[MAX_WTO_TEXT] = {0};
      int len = run_debug("hello", msg);

      expect(len).ToBe(PREFIX_LEN + 5); // prefix + "hello"
      expect(std::string(msg, PREFIX_LEN)).ToBe("ZWEX0001I ");
      expect(std::string(msg + PREFIX_LEN)).ToBe("hello");
    });

    it("clamps oversized text to the buffer and keeps it NUL-terminated", []() -> void
    {
      // Larger than MAX_WTO_TEXT but well within the macro's
      // internal scratch buffer (see zwto.h) to isolate the
      // buf.msg clamp being tested here.
      std::string big(200, 'A');
      char msg[MAX_WTO_TEXT] = {0};
      int len = run_debug(big.c_str(), msg);

      // Clamped to the full buffer minus the trailing NUL.
      expect(len).ToBe(MAX_WTO_TEXT - 1);
      expect((int)msg[MAX_WTO_TEXT - 1]).ToBe(0);              // terminated in-bounds
      expect((int)msg[MAX_WTO_TEXT - 2]).ToBe((int)'A');       // last usable slot is payload
      expect(std::string(msg, PREFIX_LEN)).ToBe("ZWEX0001I "); // prefix survives
    });

    it("fills exactly to the buffer edge without clamping", []() -> void
    {
      // prefix(10) + payload == MAX_WTO_TEXT - 1 chars, + NUL == MAX_WTO_TEXT bytes.
      std::string edge(MAX_WTO_TEXT - 1 - PREFIX_LEN, 'B');
      char msg[MAX_WTO_TEXT] = {0};
      int len = run_debug(edge.c_str(), msg);

      expect(len).ToBe(MAX_WTO_TEXT - 1);
      expect((int)msg[MAX_WTO_TEXT - 1]).ToBe(0);
      expect(std::string(msg + PREFIX_LEN)).ToBe(edge);
    });
  });
}
