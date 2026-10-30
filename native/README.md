# Miscellaneous Native Development Notes

## Metal C/C/C++ & HLASM

The following sections relate to code in the `native/c` folder.

### Logging

The C++ application includes a ZLogger singleton class for centralized logging. When `ZLOG_ENABLE` is defined during compilation, the logger automatically creates a `.zowex/logs/` directory in the current user's home directory and writes log output to `~/.zowex/logs/zowex.log`. Logs are written per-user (rather than to a location shared by all users of the binary, such as next to the executable) so that concurrent users of a shared `zo` installation don't contend for the same log file. The `zo server` daemon similarly writes its logs to `~/.zowex/logs/zowex_server.log`.

`ZLOG_ENABLE` is defined when building in debug mode with `make -DBuildType=DEBUG`. You can exclusively enable logging by passing it directly to the make command: `make -DZLOG_ENABLE=1`

Once enabled, you can insert log statements into your source code by making calls to the logger class:

```cpp
int main() {
  ZLogger::get_instance().trace("This is a trace message: %s", "details here");
}
```

Or, you can use the macros:

```cpp
ZLOG_TRACE("This is also a trace message");
```

Metal C has a similar set of macros in the format `ZLOGXMSG`:

```c
ZLOGDMSG("Debug message");
```

Where `X` is the desired log level for the message (outlined below in the "Log Levels" section).

#### Log Levels

The logger supports the following log levels (in order of verbosity):

- `TRACE` (T) - Most verbose, includes all debugging information
- `DEBUG` (D) - Debugging information
- `INFO` (I) - General informational messages (default)
- `WARN` (W) - Warning messages
- `ERROR` (E) - Error messages
- `FATAL` (F) - Fatal errors that may cause program termination
- `OFF` - Disable all logging

#### Configuration

Set the log level for `zo` using the `ZO_LOG_LEVEL` environment variable:

```bash
# Start zowex in interactive mode with log level "TRACE"
ZO_LOG_LEVEL=TRACE ./zo --it
```

To modify the log level programmatically in C++ code, use the `ZLogger::set_log_level` function. To change the log level in Metal C code, use the `ZLGSTLVL` function defined in `zlogger_metal.h`. Both functions take an integer from the enum `zlog_level_t` (also redefined as `LogLevel` in the C++ header file).

Override where log files are written using the `ZO_LOGS_DIR` environment variable. This is useful for shared installations where users may not have write access to the default location, or for redirecting logs elsewhere for troubleshooting:

```bash
# Write logs to a custom directory instead of ~/.zowex/logs
ZO_LOGS_DIR=/tmp/my-zowex-logs ./zo --it
```

The `zo server` daemon rolls its log file over once it exceeds 100KB, keeping up to 10 generations (`zowex_server.log`, `zowex_server.log.1`, ..., `zowex_server.log.9`) in FIFO order — the oldest generation is dropped to make room for the newest, bounding total disk usage per user to roughly 1MB.

### PKCS#12 certificate export

`zo system cert export --format p12` protects the private key and certificates with
PBES2/AES-256-CBC, PBKDF2-HMAC-SHA-256, and a SHA-256 integrity MAC. It does not
fall back to RC4 or 3DES when AES export fails.

Prerequisites for PKCS#12 export:

- System SSL with [APAR OA64899](https://www.ibm.com/support/pages/apar/OA64899) support.
  IBM introduced this support on z/OS 2.5 and 3.1 through the applicable PTFs.
  Build hosts also need the updated System SSL headers.
- Active ICSF. Where CSFSERV profiles protect the callable services, the exporting
  user needs READ access to `CSF1GSK`, `CSF1GAV`, and `CSF1TRD`.
- An 8–128-character passphrase representable in 7-bit ASCII, supplied through
  `--password`.
- An importing application that supports PBES2/AES-256 PKCS#12 bundles. System SSL
  importers need the corresponding OA64899 support and active ICSF as well.

System SSL controls the KDF work factor; `zo` does not expose an iteration-count
option. Exported USS files remain owner-read/write only (`0600`).

PEM export contains no private key and does not require these PBES2/ICSF prerequisites.
See IBM's [`gsk_export_key()` reference](https://www.ibm.com/docs/en/zos/3.2.0?topic=reference-gsk-export-key)
for the algorithm and passphrase requirements.

### Testing

Longer term, we may be able to make use of [Catch2](https://github.com/catchorg/Catch2). However, with our testing of Metal C code, we are exploring custom-written infrastructure with the ability to handle abends or other z-specific scenarios. So, in the near term, we will use custom testing infrastructure found in `native/c/test`:

- `ztest.hpp`
- `ztest.cpp`

This infrastructure requires `ibm-clang` compiler to enable "new" language features (lambdas) in order to provide a ~[Jest](https://jestjs.io/) testing syntax.

#### Format

Tests are written in jest-style:

```c

void my_tests()
{
  describe("my tests",
           []() -> void
           {
             it("should pass the test",
                []() -> void
                {
                  int rc = 0;
                  Expect(rc).ToBe(0);
                });
            });
}

```

Tests also support before and after hooks, allowing you to clean up resources before/after tests are finished running.

```cpp

describe("an_extern_function tests",
  []() -> void
  {
    int* x;
    beforeAll([&x]() -> void {
      x = (int*)malloc(sizeof(int));
      memset(x, 0, sizeof(int));
    });

    beforeEach([&x]() -> void {
      *x = -1;
    });

    afterAll([&x]() -> void {
      free(x);
      x = nullptr;
    });

    it("should increment x",
      [&x]() -> void
      {
        an_extern_function(x, true);
        Expect(*x).ToBe(0);
      });

    it("should not increment x",
      [&x]() -> void
      {
        an_extern_function(x, false);
        Expect(*x).ToBe(-1);
      });
  });
```

#### Building

Tests are built via a `makefile` in `c/test` and are also a default build target for the project root `makefile`.

#### Executing

Tests may be run from the `c/test` folder via `make test` or from client project using `npm run z:test`.

#### Results

Test results are printed as they are executed, potentially interlaced with joblog output if using `_BPXK_JOBLOG=STDERR`, e.g.:

```txt
zstorage tests
  PASS  should obtain and free 31-bit storage
  PASS  should obtain and free 64-bit storage
zut tests
  PASS  should upper case and truncate a long string
  PASS  should upper case and pad a short string
zjb tests
  PASS  should be able to list a job
07.29.29 JOB01916  $HASP100 IEFBR14$ ON INTRDR                            FROM STC01874 DKELOSKY
07.29.29 JOB01916  IRR010I  USERID DKELOSKY IS ASSIGNED TO THIS JOB.
```

Summary results are printed upon completion, e.g.:

```txt
======== TESTS SUMMARY ========
Total Suites: 5 passed, 1 failed, 6 total
Tests:      : 13 passed, 1 failed, 14 total
```

#### Debugging

By default, LE or Metal C abends will signal for program termination. When this occurs, a message may appear:

```txt
    unexpected ABEND occurred.  Add `TEST_OPTIONS.remove_signal_handling = true` to `it(...)` to capture abend dump
```

In this situation, no CEEDUMP is captured. To disable this behavior, disable signal handling by passing a `TEST_OPTIONS` object as a parameter to `it()`, e.g.:

```c
             TEST_OPTIONS opts = {};
             opts.remove_signal_handling = true;

             it("should recover from an abend", []() -> void
                {
                  int rc = ZRCVYEN();
                Expect(rc).ToBe(0); }, opts);
           });
```

#### Timeouts

Tests have a default timeout of 10 seconds to prevent hung tests. For tests that need more time (e.g., mainframe I/O operations), the timeout can be increased:

```c
             TEST_OPTIONS opts = {};
             opts.timeout_sec = 30;

             it("should complete slow operation", []() -> void
                {
                  // Test with 30 second timeout
                }, opts);
```

If `timeout_sec` is 0, the default timeout of 10 seconds applies to tests and hooks.

#### API

The testing infrastructure provides the following APIs

##### describe(name, fn)

`describe(name, fn)` creates a block that groups related tests. One top level `describe()` must exist and nesting is not yet supported.

##### `it(name, fn)

`it(name, fn)` is a test case contained within a `describe()` block. Individual tests and assertions are achieved with `expect()` calls and are contained within the `fn` provided by the second operand.

##### expect(value)

The `expect()` function is used to test a value (`int`, `string`, `pointer`, etc...) which returns a `RESULT_CHECK` object. To assert that a result matches an expected value,
use the `ToBe()` function on the `RESULT_CHECK` object.

Multiple `expect()` functions can exist within a `test()` to test multiple scenarios. However, if an `expect()` fails, the remaining `expect()` calls in a `test()` are skipped.

###### Expect(value) && ExpectWithContext(value, context)

`Expect(value)` and `ExpectWithContext(value, context)` are two C macros (`#define`s) which provide extra information whenever an `expect` fails. These provide the source filename and line number for which `expect` failed. `ExpectWithContext()` allows providing a second parameter to provide extra debugging information alongside a failed test.

For example:

```c
ExpectWithContext(5, "Failed to submit job").ToBe(3);
```

Produces:

```txt
  FAIL  should fail
    expected int '5' to be '3'
    at ./zjb.test.cpp:132   (Failed to submit job)
```

##### ToBe(value)

Use `.ToBe()` to compare values set in `RESULT_CHECK` through `expect`. If the expected value and checked value do not match, an exception is thrown and the test fails.

##### Not()

Use `.Not()` to inverse compare values behavior. `Not()` returns a `RESULT_CHECK` object which is then checked vai `ToBE()`.

### Creating a CHDR from a DSECT

To create a C header from an HLASM DSECT:

- Create the `asmchdr` element, e.g. `cvt.s`
- Generate & download the header via `npm run z:chdsect cvt.s`
- Save the file to ensure proper formating

OR:

- create a `.s` file in `asmchdr` folder, e.g. `asasymbp.s`
- upload, e.g. `npm run z:upload asmchdr/asasymbp.s`
- allocate output adata data set if none exists, e.g. `zo data-set create-adata <hlq>.adata`
- allocate output chdr data set if none exists, e.g. `zo data-set create-vb <hlq>.chdr`
- build `.s` file, e.g. `as -madata --gadata="//'<hlq>.USER(ASASYMBP)'" asasymbp.s`
- convert the file via `ccnedsct`, e.g. `zo tool ccnedsct --ad "<hlq>.adata(asasymbp)" --cd "<hlq>.chdr(asasymbp)"`
- copy, download the `.h` file where needed, e.g. `zowe files download ds "<hlq>.chdr(asasymbp)" --file native/c/chdsect/asasymbp.h`

By default, `zo tool ccnedsct` retains its SYSPRINT and SYSOUT listings in unique files under `~/.zowex/ccnedsct` and prints their paths. The `.zowex` and `ccnedsct` directories and the generated files are owned by the current user with mode `0700`.
Existing output directories must already meet these requirements and must not be symlinks; the command fails rather than changing their permissions. The home directory must be user-owned, with trusted, non-group/world-writable parent directories.
Use `--sysprint` and `--sysout` to choose explicit output paths; these overrides retain their existing create/truncate behavior. Remove retained listings when no longer needed.

### Recovery

In order to use `ESTAEX`-type recovery, Language Environment (LE) must be enabled with `TRAP(ON,NOSPIE)`. Otherwise, `ESPIE` recovery will gain control in an
abend scenario and bypass any established `ESTAEX`.

- `_CEE_RUNOPTS` https://www.ibm.com/docs/en/zos/3.1.0?topic=options-how-specify-runtime
- `TRAP(ON,NOSPIE)` https://www.ibm.com/docs/en/zos/3.1.0?topic=ulero-trap

### Debugging Metal C

You can add temporary debugging messages within Metal C code which will issue `WTO` messages with a message prefix of `ZWEX0001I`. These messages may be used in development only
and must not appear in distributed versions of `zo`.

To see these debugging messages within z/OS UNIX, set the environment variable `export _BPXK_JOBLOG=STDERR`. Then add debug messages via `zwto_debug(...)` found within `zwto.h` in the same format as C `printf` format strings, e.g. `zwto_debug("return code was %d", rc);`.

### Dumping Storage

Raw storage address contents can be printed to the console or a file to help with debugging scenarios. This can be achieved using the `zdbg.h` header file.

### LE C/C++

To dump storage in LE, use:

```c
#include "zdbg.h"

  int data = 3;

  zut_dump_storage(title, &data, sizeof(data));

```

### Metal C

You must ensure `zut_alloc_debug()` is called to allocate an output DD for log messages. Then in Metal C, use

```c
#include "zdbg.h"

  ZJB zjb = {0};

  zut_dump_storage("ZJB", zjb, sizeof(ZJB), ZUTDBGMG);

```

By default, output is printed to `/tmp/zowex_debug_<PID>.txt` (where PID is the current `zo` process ID) when using `ZUTDBGMG()`; however, you may provide a Metal C compatible alternative.
