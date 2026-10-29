# Miscellaneous Native Development Notes

## Metal C/C/C++ & HLASM

The following sections relate to code in the `native/c` folder.

### Logging

The C++ application uses `ZLogger` for command and server logging in every build, without requiring Metal C. Both `LOG_*` and `ZLOG_*` macros are available from `zlogger.hpp`, including in shared command handlers. Ordinary commands write to `~/.zowex/logs/zowex.log`; `ZServer::run` selects server mode before starting workers, so server and handler messages share `~/.zowex/logs/zowex_server.log`. Logs remain per-user, with newly created directories restricted to `0700` and files to `0600`.

Only Debug builds (`make -DBuildType=DEBUG`) define `ZLOG_ENABLE`, enabling the Metal C instrumentation macros and linking their logger. Metal C records use a separate `zowex_metal.log` file so BSAM cannot overwrite C++ logs. C++ messages always use standard file I/O, even in Debug builds. The former `make -DZLOG_ENABLE=1` option is no longer needed for C++ logging and does not enable Metal C instrumentation outside Debug builds.

When switching build types, force a rebuild with `make -u -DBuildType=DEBUG` or `make -u -DBuildType=RELEASE`; object files share an output directory.

You can call the logger directly:

```cpp
int main() {
  ZLogger::get_instance().trace("This is a trace message: %s", "details here");
}
```

Or, you can use the macros:

```cpp
LOG_INFO("Command progress: %d records processed", count);
ZLOG_TRACE("Detailed command diagnostics");
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
# Start zo in interactive mode with log level "TRACE"
ZO_LOG_LEVEL=TRACE ./zo --it
```

`ZO_LOG_LEVEL` applies to both command and server mode and takes precedence over the server's `--verbose` option. Without an explicit level, the default is `INFO`, or `DEBUG` for `zo server --verbose`. Level names are case-insensitive; `WARNING` is accepted as an alias for `WARN`. `OFF` disables file messages. To change the level programmatically, use `ZLogger::set_log_level(LogLevel)`. In Debug builds this also updates the Metal C level; `ZLGSTLVL` changes only the Metal C level. `ZLOG_FATAL` writes a fatal diagnostic without exiting; `LOG_FATAL` also prints to stderr and exits with status 1.

Override where log files are written using the `ZO_LOGS_DIR` environment variable. This is useful for shared installations where users may not have write access to the default location, or for redirecting logs elsewhere for troubleshooting:

```bash
# Write logs to a custom directory instead of ~/.zowex/logs
ZO_LOGS_DIR=/tmp/my-zowex-logs ./zo --it
```

Relative log directories are resolved against the current working directory. Paths containing `..` components or control characters are rejected before creating directories. The canonical log directory must belong to the effective user and must not be group- or world-writable; invalid locations disable logging rather than falling back to another directory. C++ log files must be regular, singly linked files owned by that user. Their open descriptors are checked before any truncation or output, so symbolic or hard links cannot redirect log writes.

Both C++ logs append by default and roll over after exceeding 100 KiB, keeping one active file and nine backups (`.1` through `.9`) in FIFO order, roughly 1 MiB per log. If archiving the active file fails, logging continues through the existing append stream rather than reopening or truncating it; data preservation takes precedence over the size limit. A persistent rotation failure warns once after the active file exceeds twice the limit. Formatted C++ messages are bounded to 4095 bytes. The `fatal(message, exit_code)` overload treats its message as literal text, including percent sequences, and exits with the requested status.

`ZLogger::initialize()` selects command mode and appends. `initialize_server(verbose, truncate)` selects server mode; `truncate` defaults to `false`, and explicitly setting it to `true` clears only the active server log, not its backups. `shutdown()` closes logging until either initialization method is called again. The Debug-only Metal C logger retains its BSAM record formatting and truncation behavior; it does not participate in C++ log rotation.

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
