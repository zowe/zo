---
name: backend-testing
description: Use when writing, debugging, or maintaining tests for the native C/C++ and Metal C components in native/c/test/. Covers the custom ztest framework, test patterns, build system integration, and debugging test failures.
---

# Zowe Remote SSH Native Testing

Testing native C/C++ and Metal C components using the custom `ztest` framework (Jest-like C++ testing, not Google Test).

## Framework Overview

**File Structure:**
- `native/c/test/*.test.cpp` - C++ unit tests  
- `native/c/test/*.metal.test.c` - Metal C tests
- `ztest.hpp` - Custom Jest-like framework
- `ztest_runner.cpp` - Main test runner

**Key Features:** `describe()`, `it()`, `Expect().ToBe()`, signal handling, timeouts, XML output.

## Basic Test Pattern

```cpp
#include "ztest.hpp"
#include "../my_module.h"
using namespace ztst;

void my_module_tests() {
    describe("module", []() {
        it("should work", []() {
            Expect(my_function(5)).ToBe(10);
        });
        
        it("should handle errors", []() {
            Expect([]() { bad_function(); }).ToAbend();
        });
    });
}
```

**Add to runner:** Include header in `ztest_runner.cpp`, call `my_module_tests()` in main callback, update makefile with object file.

## Key Assertions

```cpp
Expect(result).ToBe(expected);
Expect(result).Not().ToBe(unwanted);
Expect(str).ToContain("substring");
Expect(num).ToBeGreaterThan(5);
Expect(ptr).ToBeNull();
Expect(lambda).ToAbend();  // Test crashes
ExpectWithContext(val, "debug info").ToBe(expected);
```

## Test Options

```cpp
TEST_OPTIONS opts = {
    .remove_signal_handling = true,  // Disable crash detection
    .timeout_sec = 30               // Custom timeout
};
it("test name", []() { /* test */ }, opts);

// Conditional skip
#ifdef RELEASE_BUILD
it("test", []() { /* runs */ });
#else
xit("test", []() { /* skipped */ });
#endif
```

## Running Tests

```bash
# Local
cd native/c/test && make test
./build-out/ztest_runner "pattern"     # Filter tests

# Remote z/OS
npm run z:test
npm run z:test "pattern"

# Debug
export ZNP_TEST_LOG=ON BuildType=DEBUG
make clean && make test
```

## Debugging

**Crash investigation:**
```cpp
TEST_OPTIONS opts = { .remove_signal_handling = true };
// Shows actual crash dump instead of catching
```

**Timeout issues:**
```cpp
TEST_OPTIONS opts = { .timeout_sec = 60 };
```

**Debug logging:**
```cpp
TestLog("Debug message");  // Only shows if ZNP_TEST_LOG=ON
```

**Command testing:**
```cpp
int rc; std::string output;
rc = execute_command_with_output("zo version", output);
ExpectWithContext(rc, output).ToBe(0);
```

## Metal C Integration

**Metal C test file:** `module.metal.test.c`
```c
extern "C" int test_metal_function() {
    // Metal C test logic
    return 0; // success
}
```

**Call from C++ test:**
```cpp
it("metal test", []() {
    Expect(test_metal_function()).ToBe(0);
});
```

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `ZNP_TEST_LOG=ON` | Enable debug logging |
| `BuildType=DEBUG` | Debug build |
| `FORCE_COLOR=1` | Force colors |

## Common Issues

- **Signal handling:** Framework catches crashes by default - disable for debugging
- **Makefile whitespace:** Tabs vs spaces matter
- **Test independence:** Don't rely on execution order
- **Metal C memory:** Different allocation rules than standard C++

Exit codes: 0 = pass, 1 = failures. Generates `test-results.xml` for CI.