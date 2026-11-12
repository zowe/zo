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
#include <stdexcept>
#include <thread>
#include <vector>

#include "ztest.hpp"
#include "zrecovery.metal.test.h"

using namespace ztst;

void zrecovery_tests()
{

  describe("zrecovery tests", []() -> void
  {
    it("should recover from an abend", []() -> void
    {
      int rc = ZRCVYEN();
      Expect(rc).ToBe(0);
    });

    it("should handle error for NULL SSOB pointer", []() -> void
    {
      int rc = ZRCVYNUL();
      Expect(rc).ToBe(RTNCD_FAILURE);
    });

    it("should verify that the entire stack/registers are saved and restored on prolog and epilog invocation",
       []() -> void
    {
      int rc = ZRCVYSTK();
      Expect(rc).ToBe(0);
    });

    it("should not handle abend if disable_recovery was called, so it propagates up to the test runner", []() -> void
    {
      Expect([]()
      {
        ZRCVYDIS();
      }).ToAbend();
    });

    it("should execute recovery twice for nested recovery", []() -> void
    {
      int rc1 = 0;
      int rc2 = 0;
      ZRCVYNST(&rc1, &rc2);
      Expect(rc1).ToBe(1);
      Expect(rc2).ToBe(1);
    });

    it("should cleanup recovery fully for nested recovery", []() -> void
    {
      Expect([]()
      {
        ZRCVYNCL();
      }).ToAbend();
    });

    it("should handle concurrent recoveries in multiple threads", []() -> void
    {
      constexpr int num_threads = 5;
      std::vector<std::thread> threads;
      std::vector<int> rcs(num_threads, -1);

      for (int i = 0; i < num_threads; ++i)
      {
        threads.push_back(std::thread([i, &rcs]()
        {
          int thread_id = i;
          ZRCVYTHR(&thread_id, &rcs[i]);
        }));
      }

      for (auto &t : threads)
      {
        if (t.joinable())
        {
          t.join();
        }
      }

      for (int i = 0; i < num_threads; ++i)
      {
        Expect(rcs[i]).ToBe(i);
      }
    });
  });
}
