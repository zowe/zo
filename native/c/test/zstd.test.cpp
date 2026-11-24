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
#include "../zstd.hpp"
#include <stdint.h>
#include <string>

using namespace ztst;
using namespace zstd;

struct MyObject
{
  MyObject(int v) : value(v)
  {
  }
  int value;
};

struct AlignedObject
{
  char c;
  double d;
};

namespace
{
struct DestructorTester
{
  static int instance_count;

  DestructorTester()
  {
    ++instance_count;
  }

  DestructorTester(const DestructorTester &other)
  {
    ++instance_count;
  }

  DestructorTester &operator=(const DestructorTester &other)
  {
    return *this;
  }

  ~DestructorTester()
  {
    --instance_count;
  }
};

int DestructorTester::instance_count = 0;
} // namespace

void zstd_tests()
{
  describe("zstd::unexpected", []() -> void
  {
    it("should store an error value", []() -> void
    {
      unexpected<std::string> unexp("test error");
      Expect(unexp.error()).ToBe(std::string("test error"));
    });

    it("should handle move construction", []() -> void
    {
      std::string error_msg = "moved error";
      unexpected<std::string> unexp(std::move(error_msg));
      Expect(unexp.error()).ToBe(std::string("moved error"));
    });
  });

  describe("zstd::make_unexpected", []() -> void
  {
    it("should create unexpected from value", []() -> void
    {
      auto unexp = make_unexpected(42);
      Expect(unexp.error()).ToBe(42);
    });

    it("should create unexpected from moved value", []() -> void
    {
      std::string error_msg = "test error";
      auto unexp = make_unexpected(std::move(error_msg));
      Expect(unexp.error()).ToBe(std::string("test error"));
    });
  });

  describe("zstd::expected", []() -> void
  {
    it("should be successful on default construction", []() -> void
    {
      expected<int, std::string> exp;
      Expect(exp.has_value()).ToBe(true);
      Expect(static_cast<bool>(exp)).ToBe(true);
      Expect(exp.value()).ToBe(0);
    });

    it("should hold a value when constructed with one", []() -> void
    {
      expected<int, std::string> exp(42);
      Expect(exp.has_value()).ToBe(true);
      Expect(exp.value()).ToBe(42);
      Expect(*exp).ToBe(42);
    });

    it("should hold an error when constructed with unexpected", []() -> void
    {
      expected<int, std::string> exp = make_unexpected(std::string("error"));
      Expect(exp.has_value()).ToBe(false);
      Expect(static_cast<bool>(exp)).ToBe(false);
      Expect(exp.error()).ToBe(std::string("error"));
    });

    it("should handle copy construction of successful expected", []() -> void
    {
      expected<int, std::string> exp1(42);
      expected<int, std::string> exp2(exp1);
      Expect(exp2.has_value()).ToBe(true);
      Expect(exp2.value()).ToBe(42);
    });

    it("should handle copy construction of error expected", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error"));
      expected<int, std::string> exp2(exp1);
      Expect(exp2.has_value()).ToBe(false);
      Expect(exp2.error()).ToBe(std::string("error"));
    });

    it("should handle move construction of successful expected", []() -> void
    {
      expected<int, std::string> exp1(42);
      expected<int, std::string> exp2(std::move(exp1));
      Expect(exp2.has_value()).ToBe(true);
      Expect(exp2.value()).ToBe(42);
    });

    it("should handle move construction of error expected", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error"));
      expected<int, std::string> exp2(std::move(exp1));
      Expect(exp2.has_value()).ToBe(false);
      Expect(exp2.error()).ToBe(std::string("error"));
    });

    it("should handle copy assignment from value to value", []() -> void
    {
      expected<int, std::string> exp1(10);
      expected<int, std::string> exp2(20);
      exp1 = exp2;
      Expect(exp1.has_value()).ToBe(true);
      Expect(exp1.value()).ToBe(20);
    });

    it("should handle copy assignment from error to error", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error1"));
      expected<int, std::string> exp2 = make_unexpected(std::string("error2"));
      exp1 = exp2;
      Expect(exp1.has_value()).ToBe(false);
      Expect(exp1.error()).ToBe(std::string("error2"));
    });

    it("should handle copy assignment from value to error", []() -> void
    {
      expected<int, std::string> exp1(42);
      expected<int, std::string> exp2 = make_unexpected(std::string("error"));
      exp1 = exp2;
      Expect(exp1.has_value()).ToBe(false);
      Expect(exp1.error()).ToBe(std::string("error"));
    });

    it("should handle copy assignment from error to value", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error"));
      expected<int, std::string> exp2(42);
      exp1 = exp2;
      Expect(exp1.has_value()).ToBe(true);
      Expect(exp1.value()).ToBe(42);
    });

    it("should handle value assignment", []() -> void
    {
      expected<int, std::string> exp = make_unexpected(std::string("error"));
      exp = 42;
      Expect(exp.has_value()).ToBe(true);
      Expect(exp.value()).ToBe(42);
    });

    it("should handle unexpected assignment", []() -> void
    {
      expected<int, std::string> exp(42);
      exp = make_unexpected(std::string("error"));
      Expect(exp.has_value()).ToBe(false);
      Expect(exp.error()).ToBe(std::string("error"));
    });

    it("should throw when accessing value of error expected", []() -> void
    {
      expected<int, std::string> exp = make_unexpected(std::string("error"));
      bool thrown = false;
      try
      {
        exp.value();
      }
      catch (const std::logic_error &)
      {
        thrown = true;
      }
      Expect(thrown).ToBe(true);

      thrown = false;
      try
      {
        *exp;
      }
      catch (const std::logic_error &)
      {
        thrown = true;
      }
      Expect(thrown).ToBe(true);
    });

    it("should throw when accessing error of successful expected", []() -> void
    {
      expected<int, std::string> exp(42);
      bool thrown = false;
      try
      {
        exp.error();
      }
      catch (const std::logic_error &)
      {
        thrown = true;
      }
      Expect(thrown).ToBe(true);
    });

    it("should allow modification through operator*", []() -> void
    {
      expected<int, std::string> exp(10);
      *exp = 20;
      Expect(*exp).ToBe(20);
    });

    it("should allow modification through value()", []() -> void
    {
      expected<int, std::string> exp(10);
      exp.value() = 20;
      Expect(exp.value()).ToBe(20);
    });

    it("should allow access through operator->", []() -> void
    {
      expected<MyObject, std::string> exp(MyObject(55));
      Expect(exp->value).ToBe(55);
    });

    it("should provide value_or functionality", []() -> void
    {
      expected<int, std::string> success(42);
      expected<int, std::string> failure = make_unexpected(std::string("error"));

      Expect(success.value_or(0)).ToBe(42);
      Expect(failure.value_or(99)).ToBe(99);
    });

    it("should swap successful expectations", []() -> void
    {
      expected<int, std::string> exp1(10);
      expected<int, std::string> exp2(20);
      exp1.swap(exp2);
      Expect(exp1.value()).ToBe(20);
      Expect(exp2.value()).ToBe(10);
    });

    it("should swap error expectations", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error1"));
      expected<int, std::string> exp2 = make_unexpected(std::string("error2"));
      exp1.swap(exp2);
      Expect(exp1.error()).ToBe(std::string("error2"));
      Expect(exp2.error()).ToBe(std::string("error1"));
    });

    it("should swap success with error", []() -> void
    {
      expected<int, std::string> exp1(42);
      expected<int, std::string> exp2 = make_unexpected(std::string("error"));
      exp1.swap(exp2);
      Expect(exp1.has_value()).ToBe(false);
      Expect(exp1.error()).ToBe(std::string("error"));
      Expect(exp2.has_value()).ToBe(true);
      Expect(exp2.value()).ToBe(42);
    });

    it("should ensure proper memory alignment", []() -> void
    {
      expected<AlignedObject, std::string> exp;
      exp = AlignedObject();

      const AlignedObject *ptr = &(*exp);
      uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
      size_t alignment = __alignof__(AlignedObject);

      Expect(address % alignment).ToBe(0);
    });

    it("should properly destruct contained objects", []() -> void
    {
      DestructorTester::instance_count = 0;

      {
        expected<DestructorTester, std::string> exp{DestructorTester()};
        Expect(DestructorTester::instance_count).ToBe(1);
      }
      Expect(DestructorTester::instance_count).ToBe(0);

      DestructorTester::instance_count = 0;
      {
        expected<int, DestructorTester> exp = make_unexpected(DestructorTester());
        Expect(DestructorTester::instance_count).ToBe(1);
      }
      Expect(DestructorTester::instance_count).ToBe(0);
    });
  });

  describe("zstd::expected comparisons", []() -> void
  {
    it("should compare two successful expectations", []() -> void
    {
      expected<int, std::string> exp1(42);
      expected<int, std::string> exp2(42);
      expected<int, std::string> exp3(43);

      Expect(exp1 == exp2).ToBe(true);
      Expect(exp1 != exp3).ToBe(true);
    });

    it("should compare two error expectations", []() -> void
    {
      expected<int, std::string> exp1 = make_unexpected(std::string("error"));
      expected<int, std::string> exp2 = make_unexpected(std::string("error"));
      expected<int, std::string> exp3 = make_unexpected(std::string("different"));

      Expect(exp1 == exp2).ToBe(true);
      Expect(exp1 != exp3).ToBe(true);
    });

    it("should compare success and error expectations", []() -> void
    {
      expected<int, std::string> success(42);
      expected<int, std::string> error = make_unexpected(std::string("error"));

      Expect(success != error).ToBe(true);
      Expect(error != success).ToBe(true);
    });

    it("should compare expected with value", []() -> void
    {
      expected<int, std::string> exp(42);
      expected<int, std::string> error = make_unexpected(std::string("error"));

      Expect(exp == 42).ToBe(true);
      Expect(42 == exp).ToBe(true);
      Expect(exp != 43).ToBe(true);
      Expect(43 != exp).ToBe(true);
      Expect(error != 42).ToBe(true);
      Expect(42 != error).ToBe(true);
    });

    it("should compare expected with unexpected", []() -> void
    {
      expected<int, std::string> exp(42);
      expected<int, std::string> error = make_unexpected(std::string("error"));
      auto unexp = make_unexpected(std::string("error"));
      auto different_unexp = make_unexpected(std::string("different"));

      Expect(error == unexp).ToBe(true);
      Expect(unexp == error).ToBe(true);
      Expect(error != different_unexp).ToBe(true);
      Expect(different_unexp != error).ToBe(true);
      Expect(exp != unexp).ToBe(true);
      Expect(unexp != exp).ToBe(true);
    });
  });
}
