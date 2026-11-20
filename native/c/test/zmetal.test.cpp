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
#include "zmetal.metal.test.h"
#include "../zutm.h"

using namespace ztst;

void zmetal_tests()
{

  describe("zmetal tests", []() -> void
  {
    it("should load a program", []()
    {
      std::string name = "IEFBR14";
      void *ep = ZMTLLOAD(name.c_str());
      Expect(ep).Not().ToBeNull();
      int rc = ZMTLDEL(name.c_str());
      Expect(rc).ToBe(0);
    });

    it("should not load a program that does not exist", []()
    {
      std::string name = "IEFBR15";
      void *ep = ZMTLLOAD(name.c_str());
      Expect(ep).ToBeNull();
    });

    it("should not delete a program that does not exist", []()
    {
      std::string name = "IEFBR15";
      int rc = ZMTLDEL(name.c_str());
      Expect(rc).ToBe(4);
    });
  });

  describe("auth relinquish (ZUTNOAUT) tests", []() -> void
  {
    it("should find the IEAVJAOF service (ECVTJAOF) available on this system", []()
    {
      // auth_off fails closed when ECVTJAOF is zero; this guards that
      // assumption on every system the tests run on
      Expect(ZMTLJAOF()).ToBe(1);
    });

    it("should succeed as a no-op without changing PSW state or key when not APF-authorized", []()
    {
      // premise: the test runner is not APF-authorized
      Expect(ZMTLTAUT()).Not().ToBe(0);

      int key_before = static_cast<int>(ZUTMGKEY());
      Expect(ZMTLPST()).ToBe(1); // problem state

      Expect(ZUTNOAUT()).ToBe(0);

      Expect(static_cast<int>(ZUTMGKEY())).ToBe(key_before);
      Expect(ZMTLPST()).ToBe(1); // still problem state
    });

    it("should be idempotent", []()
    {
      Expect(ZUTNOAUT()).ToBe(0);
      Expect(ZUTNOAUT()).ToBe(0);
    });

    it("should handle concurrent calls from multiple threads", []()
    {
      constexpr int num_threads = 5;
      std::vector<std::thread> threads;
      std::vector<int> rcs(num_threads, -1);

      for (int i = 0; i < num_threads; ++i)
      {
        threads.push_back(std::thread([i, &rcs]() -> void
        {
          rcs[i] = ZUTNOAUT();
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
        Expect(rcs[i]).ToBe(0);
      }
    });
  });
}
