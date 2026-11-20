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

#include "ztest.hpp"
#include "zstorage.metal.test.h"

using namespace ztst;

void zstorage_tests()
{

  describe("zstorage tests", []() -> void
  {
    it("should obtain and free 31-bit storage", []() -> void
    {
      int size = 128;
      void *data = nullptr;
      expect(data).ToBeNull();
      data = STBT31(&size);
      expect(data).Not().ToBeNull();
      expect(STFREE(&size, data)).ToBe(0);
    });

    it("should obtain and free 64-bit storage", []() -> void
    {
      int size = 256;
      void *data = nullptr;
      expect(data).ToBeNull();
      data = STGET64(&size);
      expect(data).Not().ToBeNull();
      expect(STREL(data)).ToBe(0);
    });
  });
}
