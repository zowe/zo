# Change Log

All notable changes to the native code for "zowex" are documented in this file.

Check [Keep a Changelog](http://keepachangelog.com/) for recommendations on how to structure this file.

## Recent Changes

- Added the `zo ds delete-vsam` command and the `deleteVsam` RPC method to delete VSAM clusters with optional erase and purge behavior. [#490](https://github.com/zowe/zowex/issues/490)

## `1.0.2`

- `c`: Clarified the `zowex ds create-member` success message to indicate it creates only the member and not the data set. [#639](https://github.com/zowe/zowex/issues/639)
- `c`: Fixed the `--encoding` help text for `zowex data-set write`, `zowex uss write`, and `zowex job submit-jcl`, which described reading contents instead of converting the input. [#649](https://github.com/zowe/zowex/issues/649)
- `c`: `ResultTable` columns are now named (`add_column("name", width)`), and `data-set list`, `data-set list-members`, `job list`, `job list-files`, and `job view-status` accept a new `--response-format-header`/`--rfh` option that prints those names as a header row in table and CSV output.
- `c`: Added a `--json` option to `zowex` commands, which prints the command's result to stdout as a single line of JSON (`success`, `exitCode`, `data`, `stderr`) so callers can parse it without starting `zowex server`. [#837](https://github.com/zowe/zowex/issues/837)
- `c`: Fixed `zowex job list-files --response-format-csv` repeating every preceding record's fields at the start of each row, so only the first row had the documented five fields.
- `c`: Fixed `zowex uss list` only returning structured results when `--response-format-csv` was also passed. [#1128](https://github.com/zowe/zowex/pull/1128)
- `c`: Fixed `plugin::Io::is_redirecting_error()` and `is_redirecting_input()` reporting the state of the output stream instead of their own. [#1128](https://github.com/zowe/zowex/pull/1128)
- `c`: Fixed a plug-in that registers an argument conflicting with an existing name or alias aborting `zowex` for every command instead of being rejected with a logged diagnostic. [#1128](https://github.com/zowe/zowex/pull/1128)
- `python`: Added `package_precompiled.py` tool to package precompiled binary assets (equivalent to Python wheels) into `zbind_bin_dist.tar.gz` for instant compiler-free installation.
- `python`: Added `package_zbind.py` tool to package a clean, self-contained source-based distribution bundle (`zbind_src_dist.tar.gz`) containing all necessary headers, sources, and objects to build the bindings on any z/OS host without SWIG.
- `python`: Fixed compilation of Python bindings on z/OS by passing required Language Environment feature macros `_EXT` and `_OPEN_SYS_FILE_EXT` to the compiler in `setup.py`.
- Reduced the size of the `zo` binary by over 40% (10MB -> 6MB) by optimizing the codebase. [#1166](https://github.com/zowe/zowex/pull/1166)
- `python`: Fixed the Python bindings compiling the shared `native/c` sources with `-fzos-le-char-mode=ascii`, which flipped their execution charset to ASCII and broke every data set and job binding. [#1121](https://github.com/zowe/zowex/pull/1121)
- `python`: `list_data_sets` accepts `show_attributes` so `dsorg`, `volser`, `recfm` and `migrated` can be populated. [#1121](https://github.com/zowe/zowex/pull/1121)
- `python`: Fixed `get_job_status` returning unconverted EBCDIC strings and `create_data_set` raising errors with an empty message. [#1121](https://github.com/zowe/zowex/pull/1121)
- `python`: Fixed `list_jobs_by_owner` failing when no job name prefix is given. [#1121](https://github.com/zowe/zowex/pull/1121)
- `python`: Fixed the USS bindings (`create_uss_file`, `create_uss_dir`, `move_uss_file_or_dir`, `list_uss_dir`, `chmod_uss_item`, `delete_uss_item`, `chown_uss_item`, `chtag_uss_item`) not converting paths, and `write_uss_file`/`write_uss_file_streamed` not converting etags, between ASCII and EBCDIC. [#1121](https://github.com/zowe/zowex/pull/1121)

## `1.0.1`

- Fixed worker threads timing out in the `zo` RPC server for a long upload or download operation that exceeds the request timeout. [#988](https://github.com/zowe/zowex/issues/988)
- `c`: Adjusted the size of the `etag` field to improve compatibility with z/OS UNIX files. [#1140](https://github.com/zowe/zowex/pull/1140)
- `c`: Rejected oversized data set etags before copying them into request storage. [#1140](https://github.com/zowe/zowex/pull/1140)
- `c`: Rejected oversized USS file etags before copying them into request storage. [#1140](https://github.com/zowe/zowex/pull/1140)

## `1.0.0`

- **Breaking:** `c`: Renamed the `ZOWEX_NUM_WORKERS` environmental variable to `ZO_NUM_WORKERS`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `ZOWEX_LOG_LEVEL` environmental variable to `ZO_LOG_LEVEL`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `ZOWEX_LOGS_DIR` environmental variable to `ZO_LOGS_DIR`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `ZOWEAX_PATH` environmental variable to `ZOA_PATH`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `ZOWEX_LOGS_DIR` environmental variable to `ZO_LOGS_DIR`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `ZOWEX_PLUGINS_DIR` environmental variable to `ZO_PLUGINS_DIR`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed `ZOWEX_PLUGIN_DECLARE_ABI()` macro to `ZO_PLUGIN_DECLARE_ABI()`. [#1119](https://github.com/zowe/zowex/pull/1119)
- **Breaking:** `c`: Renamed the `zowex` binary to `zo`, and the `zoweax` binary to `zoa`. [#1119](https://github.com/zowe/zowex/pull/1119)
- `c`: Added JSON-RPC support for listing parmlib data sets. [#1124](https://github.com/zowe/zowex/pull/1124)
- `c`: Added the `zowex ds resolve-alias <dsn>` command which allows the user to resolve data set aliases. [#1108](https://github.com/zowe/zowex/pull/1108)
- `c`: Added `zds_idcams` utility function which allows the caller to execute the z/OS IDCAMS program and retrieve its output. [#1108](https://github.com/zowe/zowex/pull/1108)

## `0.9.0`

- `c`: `unixCommand` returns stderr in a new optional `stderr` response field. [#1112](https://github.com/zowe/zowex/issues/1112)
- **Breaking:** `c`: `zowex` `console` command group is removed. [#1112](https://github.com/zowe/zowex/issues/1112)
- **Breaking:** `c`: `zoweax` contains only the `console` command group and no longer supports plug-in loading. [#1112](https://github.com/zowe/zowex/issues/1112)
- **Breaking:** `c`: Console commands are no longer issued with master authority. [#1112](https://github.com/zowe/zowex/issues/1112)
- `c`: Console commands now use a unique CART instead of `ZOWECART`. [#1112](https://github.com/zowe/zowex/issues/1112)
- `c`: The `version` command now reports the invoked binary's instead of always `zowex`. [#1112](https://github.com/zowe/zowex/issues/1112)
- `c`: Preserve LRECL on PS write operations. [#1109](https://github.com/zowe/zowex/issues/1109)
- `c`: Flipped to and from encoding arguments in error message to be accurate. [#1106](https://github.com/zowe/zowex/pull/1106)

## `0.8.0`

- `c`: Stripped ASA control characters when reading spool files. [#114](https://github.com/zowe/zowex/issues/114)
- `c`: Fixed `--local-encoding` being ignored when reading spool files. The source codepage was never copied to the `ZDS` struct passed to `zds_read`, so the conversion always fell back to the default source encoding (`UTF-8`). [#1097](https://github.com/zowe/zowex/pull/1097)
- `c`: Fixed an issue where passing numeric values to the `encoding` or `local-encoding` arguments would result in an empty string, as their argument definitions did not enable string coercion. Now, values provided to these options are converted to strings to ensure proper codepage conversion. [#1095](https://github.com/zowe/zowex/issues/1095)
- `c`: Added ESM (RACF or equivalent) certificate and key ring management to the `system` command group, migrating the `keyring-utilities` functionality (R_datalib and System SSL) into `zowex` as two subgroups: `system keyring create|delete|list|list-rings|count` and `system cert import|export|delete|show|connect|set-default|trust|rename|refresh`. [#1079](https://github.com/zowe/zowex/pull/1079)
- **Breaking:** `c`: Changed the plug-in SDK types `ast::ObjMap` and `plugin::ArgumentMap` from `std::unordered_map` to `std::map`, and added the `ZOWEX_PLUGIN_ABI_VERSION` constant with a matching load-time check. Plug-ins must now be rebuilt against the current `extend/plugin.hpp` and must expand `ZOWEX_PLUGIN_DECLARE_ABI()` alongside `register_plugin()`; a plug-in built against an earlier header is rejected at load time with a diagnostic instead of being loaded with mismatched type layouts. [#871](https://github.com/zowe/zowex/issues/871)
- `c`: Replaced the string-keyed `std::unordered_map`/`std::unordered_set` containers in `zjson.hpp` and the JSON-RPC server with the ordered `std::map`/`std::set`, removing the binary's dependency on the out-of-line libc++ symbol `std::__1_e::__hash_memory`. That symbol is absent from `CRTEQCXE` on z/OS systems below the required Language Environment maintenance level, where it caused `CEE3561S` at load time. JSON object members are now emitted in alphabetical key order rather than hash order; member order was never part of the API contract. [#871](https://github.com/zowe/zowex/issues/871)
- `c`: Pinned the build to Open XL C/C++ 2.1, since 2.2 requires z/OS 3.1+ and references a newer libc++ than a z/OS 2.5 system can provide. Added a declared minimum supported z/OS level, `ZosMinLevel=zosv2r5` in `native/c/toolchain.mk`, passed to the compiler as `-mzos-target`. `make runtime-imports` (`npm run z:imports`) reports the LE/libc++ symbols the binary imports, and `npm run lint:compat` rejects string-keyed hash containers at the source level. [#871](https://github.com/zowe/zowex/issues/871)
- `c`: Added `--exact-match` option to the `zowex ds list` command. This option defaults to false for backwards compatibility, but can be set to true to avoid appending `.**` to the end of data set patterns. [#914](https://github.com/zowe/zowex/issues/914)

## `0.7.1`

- `c`: Fixed `zowex server` resource leaks that could exhaust a z/OS LPAR after a request timed out or a connection dropped. The shutdown signal handler no longer calls into the worker pool or logger, which could deadlock and leave the process running forever; the number of live force-detached worker threads is now bounded, and the server exits when the limit is exceeded so the system can reclaim them; late responses from a detached worker are discarded instead of being sent for an ID the client already failed; notifications are serialized with responses so concurrent writes cannot corrupt the JSON stream; and worker initializer threads are drained before the pool is destroyed. [#537](https://github.com/zowe/zowex/issues/537)
- `c`: Fixed an issue with `zowex ds list-members` when reading PDS directory blocks. [#1070] (https://github.com/zowe/zowex/pull/1070)

## `0.7.0`

- `c`: `zowex` plug-in loading is now opt-in: plug-ins are only loaded when the `ZOWEX_PLUGINS_DIR` environment variable is explicitly set, replacing the previous implicit `<exec_dir>/plugins` fallback. Added directory- and file-level ownership/permission checks before a plug-in is loaded, and rejected a plug-in command from registering if its name or an alias collides with a built-in verb or another plug-in's command. [#1074](https://github.com/zowe/zowex/pull/1074)
- `c`: Added support to return information for current linklist. [#1061](https://github.com/zowe/zowex/pull/1061)
- `c`: Made handling of control bytes (`0x0`-`0x1f`) in JSON safer. Now when an object is serialized, control bytes are replaced with the Unicode substitution character, and when a string is deserialized, control bytes are rejected as invalid. [#1078](https://github.com/zowe/zowex/pull/1078)

## `0.6.1`

- `c`: Changed `zowex` and `zowex server` to write log files to the current user's home directory (`~/.zowex/logs`) instead of a location shared by all users. `zowex server` now rolls its log over in FIFO order across up to 10 files capped at 100KB each (~1MB total) instead of truncating a single file. Added support for the `ZOWEX_LOGS_DIR` environment variable to customize the log directory. [#1003](https://github.com/zowe/zowex/issues/1003)
- `c`: Implemented bounds checking and truncation for strings logged with the `zwto_debug` macro. [#1059](https://github.com/zowe/zowex/pull/1059)
- `c`: Fixed an issue where data set member write operations did not produce self-contained records for stateful mixed single-byte and double-byte character sets. Now, each record written to a data set member is self-contained to avoid code page conversion errors. [#1049](https://github.com/zowe/zowex/pull/1049)
- `c`: Fixed an issue where the `zowex ds copy` command would fail to allocate DDs when copying PDS members in parallel. [#994](https://github.com/zowe/zowex/issues/994)
- `c`: Added support for listing APF and PROCLIB data sets to JSON-RPC server.
- `c`: Optimized Adler32 calculations to compute e-tags incrementally during streamed data set downloads. This significantly reduces memory footprint and resolves a failure when downloading large data sets. [#1016](https://github.com/zowe/zowex/issues/1016)
- `c`: Fix regression on ENQ qname. [#1027](https://github.com/zowe/zowex/issues/1027)
- `c`: Added z/OS recovery around JES operations in the `zowex` CLI binary to allow recovering from abends. [#1011](https://github.com/zowe/zowex/pull/1011)
- **Breaking:** `c`: Renamed C function `zjb_read_jobs_output_by_key` in zjb.hpp to `zjb_read_job_content_by_key` to be consistent with `zjb_read_job_content_by_dsn.
- `c`: Fixes issue where `zowex` failed to allocate `sysout` DDs that were dynamically allocated under z/OS UNIX. [#840](https://github.com/zowe/zowex/issues/840)
- `c`: Add command `zowex system list-apf` to list APF authorized data sets.
- `c`: Fixed several vulnerabilities in C++ and Metal C code to improve stability. [#997](https://github.com/zowe/zowex/pull/997)
- `c`: Optimized USS file list operations by replacing CSV formatting and parsing with structured data transfer via the `ZusfListEntry` struct. This improves performance for `zowex uss list` and provides type-safe, programmatic access to file attributes for SDK consumers. [#990](https://github.com/zowe/zowex/pull/990)
- `c`: Fixed parsing of file names containing commas in USS list API responses, which caused data misalignment for SDK consumers. [#989](https://github.com/zowe/zowex/issues/989)
- `c`: Fixed an issue where the `zoweax` authorized CLI binary could run the `server` command or `plugin` command group as privileged. Now, `zoweax` disables privileges before commands are executed. Commands in the `console` group continue to run as privileged. [#958](https://github.com/zowe/zowex/issues/958)
- `c`: Added support for pre-command hooks in the command-line parser utility. [#958](https://github.com/zowe/zowex/issues/958)
- `c`: Fixes issue where ZWEPROLG could return to caller in an incorrect AMODE. [#951](https://github.com/zowe/zowex/issues/951)
- `c`: Improved error message when deleting a member fails because its parent PDS is open in ISPF. [#916](https://github.com/zowe/zowe-native-proto/issues/916)
- `c`: Fixed an issue where incorrect values could be returned for the `usedp` (Used Space) and `volsers` (Volumes) attributes when listing data sets. [#981](https://github.com/zowe/zowex/pull/981)
- `c`: Fixed an issue where invalid data could be returned for attributes of PDS members. [#1009](https://github.com/zowe/zowex/issues/1009)
- `c`: Fixed an issue where `zowex system view-syslog` could display invalid end time. [#1017](https://github.com/zowe/zowex/issues/1017)

## `0.6.0`

- `c`: Fixes wildcard search for listing data sets on multiple catalogs. [#947](https://github.com/zowe/zowex/issues/947)

## `0.5.0`

- `c`: Fixed several vulnerabilities in C++ and Metal C code to improve stability. [#940](https://github.com/zowe/zowex/pull/940)
- **Breaking:** `c`: Refactored the backend for copying data sets to use Z/OS utilities IEBCOPY and IEBGENER. Removed `--delete-target-members` flag and added `--overwrite` flag for copying a partitioned data set to an existing partitioned data set. Removed duplicate `member_exists_in_pds` function. [#932](https://github.com/zowe/zowex/issues/932)
- `c`: Fix spool read / dynalloc error via optimization assignments. [#945](https://github.com/zowe/zowex/issues/945)
- `c`: Updated `zut_read_input` so that all tests pass on `zowex`. Now there is no specific handling for line-based TTY input so that data is more consistently preserved.[#953](https://github.com/zowe/zowex/pull/953)
- **Breaking:** `c`: Refactored the `zds_write` function to consolidate data set and DD write logic into a single entry point. [#908](https://github.com/zowe/zowe-native-proto/issues/908)
- `c`: Changed `zowex --version` and `zowex -v` to return just the version number. [#925](https://github.com/zowe/zowex/pull/925)
- `c`: Removed duplicate `-v` and `--version` aliases on the `zowex version` command. [#922](https://github.com/zowe/zowex/pull/922)
- `c`: Added the `getInfo` RPC to retrieve the middleware version and build information. [#922](https://github.com/zowe/zowex/pull/922)
- `c`: Added a DCB abend exit for handling edge cases around PDS member open, write, and close operations. When an abend is encountered, it is either delayed or ignored (when applicable) and an error is returned gracefully via the diagnostic structure (`ZDIAG`) rather than terminating the process. Refer to [DCB abend exits](doc/dcb-abend-exit.md) for more information on how and when abends are handled. **Note:** In the event of an end-of-space (end-of-volume) abend, the operating system closes the DCB if the ignore request is honored from the abend exit. However, the abend might still percolate during end-of-volume `CLOSE` routines. An ESTAE/ESTAEX recovery routine should be used to gracefully handle these cases. [#839](https://github.com/zowe/zowex/issues/839)
- `c`: Fixed an issue where listing the members of a protected, partitioned data set resulted in an unclear error message. Now, the error messages are more specific and include information about insufficient permissions when applicable. [#297](https://github.com/zowe/zowex/issues/297)
- `c`: Fixed an issue where the `zowex ds create-member` command would overwrite an existing member. Added an `--overwrite` flag to the command that defaults to false. [#642](https://github.com/zowe/zowex/issues/642)

## `0.4.0`

- `c`: Added `zowex uss issue` command and the `unixCommand` RPC for executing USS shell commands using `spawn()` with `_BPX_SHAREAS=YES` for efficient same-address-space execution. [#867](https://github.com/zowe/zowex/pull/867)
- `c`: Fixed an issue where submitting JCL from the VS Code editor with CRLF line endings caused job submission errors. [#882](https://github.com/zowe/zowex/pull/882)
- `c`: Fixed an issue where VSAM index or data components returned `*VSAM*` for the volume serial. Now, an accurate volume serial is returned for both component types. [#864](https://github.com/zowe/zowex/pull/864)
- `c`: Fixed an issue where listing data sets would fail with an "unsupported entry type" error if the pattern matched a VSAM PATH or alternate index (AIX). Now, matching VSAM PATH or AIX data sets are returned in the list results. [#841](https://github.com/zowe/zowex/issues/841)
- `c`: Added `zowex system view-syslog` command. [#829](https://github.com/zowe/zowex/issues/829)
- `c`: Added an option for command handler arguments to coerce all input values into a string, allowing developers to have more control over expected formatting and type constraints. [#875](https://github.com/zowe/zowex/pull/875)
- **Breaking:** `c`: Renamed the `zds_read_from_dsn` function to `zds_read`. [#831](https://github.com/zowe/zowex/issues/831)
- **Breaking:** `c`: Removed the `zds_read_from_dd` function in favor of the newly-consolidated `zds_read` function. [#831](https://github.com/zowe/zowex/issues/831)
- **Breaking:** `c`: Removed the standalone `zowed` binary and its entry point. The server is now exclusively an embedded subcommand of `zowex` (the z/OS backend CLI binary) via `zowex server`, eliminating the need for a separate binary and `libzowed.so` dynamic loading. [#846](https://github.com/zowe/zowex/issues/846)
- **Breaking:** `c`: Renamed the `ZOWED_NUM_WORKERS` environment variable to `ZOWEX_NUM_WORKERS` (controls the number of parallel worker threads the server uses to handle requests). [#846](https://github.com/zowe/zowex/issues/846)
- `c`: Enabled `-O2` optimization for release builds, improving runtime performance of native binaries. [#846](https://github.com/zowe/zowex/issues/846)
- `c`: Added `zowex system list-subsystems` with optional filter. [#842](https://github.com/zowe/zowex/issues/842)
- `c`: Added `zowex system` command group and relocated items `list-parmlib`, `list-proclib`, and `display-symbol` from `zowex tool` to this group. [#843](https://github.com/zowe/zowex/issues/843) and [#844](https://github.com/zowe/zowex/issues/844)
- `c`: Added support for displaying member statistics. [#630](https://github.com/zowe/zowex/issues/630)
- `c`: Added `toolSearch` command in the server (used to search for data sets) for use by client SDK.
- `c`: Added the `pattern` option to the `data-set list-members` zowex command to filter the returned members. [#817](https://github.com/zowe/zowex/pull/817)
- `c`: Implemented `zowex uss copy` command to copy USS files and directories. [#379](https://github.com/zowe/zowex/issues/379)
- `c`: Implemented `zowex uss move` command to move files and directories in z/OS Unix. [#378](https://github.com/zowe/zowex/issues/378)
- `python`: Implemented basic move APIs and fixed build issues. [#789](https://github.com/zowe/zowex/pull/789)
- `c`: Updated the `gen_chdsect.sh` shell script used to generate chdsect header files to generate appropriate pragma syntax expected by the `ibm-clang` C/C++ compiler. When the headers are included in Open XL-compiled code, the new pragma syntax is used. Otherwise, the legacy `pack(packed)` and `pack(reset)` macro syntax is used. [#368](https://github.com/zowe/zowex/issues/368)
- `native`: Migrated compiler from xlclang/xlclang++ to ibm-clang/ibm-clang++ (Open XL C/C++, v2.1, supports C++17). The `xlclang-extenders` makefile target has been removed in favor of the `all` target. [#653](https://github.com/zowe/zowex/issues/653)
- `c`: Modernized C++ code to align with C++17 best practices and adopted new standard library implementations (`unordered_map`, `to_string`, `if constexpr`, brace initialization, etc.). [#812](https://github.com/zowe/zowex/pull/812)
- `c`: Removed all cases of `using namespace std` in native code to prevent namespace collisions and unintended effects on API consumers. All uses of `std` classes and utilities are now explicitly prefixed with `std::`. [#812](https://github.com/zowe/zowex/pull/812)
- `c`: Removed redundant standard library utilities from the `zstd.hpp` C++ header, in favor of the verified implementations included with ibm-clang and C++17. [#812](https://github.com/zowe/zowex/pull/812)
- `c`: Renamed `zut_int_to_string` to `zut_hex_to_string` to align with use cases for the function. [#812](https://github.com/zowe/zowex/pull/812)

## `0.2.4`

- `c`: Fixed an issue where errors during `zowex` initialization and command execution were unhandled, causing abends as a result. Now, if a fatal error is encountered, the error is caught and its details are displayed before the process exits. [#784](https://github.com/zowe/zowex/issues/784)
- `c`: Fixed an issue where the `zowex uss list` command would return the file type `d` for symlinked directories instead of `l`. Now, symlinks in a file list are marked with file type `l`. [#791](https://github.com/zowe/zowex/issues/791)
- `c`: Fixed an issue where the `zowex uss delete` command could not remove symlinks or directories containing symlinks. Now, the command properly deletes a directory with a symlink in it, and treats the link as a file. No sub-directories or files in the target link are deleted. [#792](https://github.com/zowe/zowex/issues/792)
- `c`: Added support for `multivolume` attribute when listing data sets. [#782](https://github.com/zowe/zowex/pull/782)
- `c`: Fixed an issue with the `zowex ds write` command where e-tag arguments without alphabetic characters resulted in e-tag validation being skipped. Now, e-tag validation is performed regardless of whether the e-tag contains alphabetic characters. [#676](https://github.com/zowe/zowex/issues/676)

## `0.2.3`

- `c`: Added `zowex ds copy` command to copy data sets and members with optional `--replace` flag (overwrites matching members) and `--delete-target-members` flag (deletes all target members before copying, making target match source exactly). Supports PDS-to-PDS, member-to-member, and sequential-to-sequential copies. Note: RECFM=U data sets are not supported. [#750](https://github.com/zowe/zowex/pull/750)
- `c`: Added rename data set members functionality to the backend. [#765](https://github.com/zowe/zowex/pull/765)
- `c`: Implement `zowex job watch` command to watch spool output for a string or regex until terminating.
- `c`: Implement `zut_bpxwdyn_rtdsn` to obtain and return a system allocated data set name.
- `c`: Implement command `zowex job view-file` to print contents of a job output data set.
- The `zowex job submit-jcl` command now displays the submitted job in the following format: `JobName(JobId)` [#733](https://github.com/zowe/zowex/issues/733)
- `c`: Rename command `zowex job view-file` to `zowex job view-file-by-id` so that `view-file` can be used to print a specific file (data set) name. [#740](https://github.com/zowe/zowex/issues/740)
- `c`: The zowex CLI parser now supports enabling passthrough arguments for commands. When enabled, arguments passed after a double-dash (`--`) are passed directly to the command as raw input. [#729](https://github.com/zowe/zowex/pull/729)
- Added the rename dataset functionality to the backend. [#376](https://github.com/zowe/zowex/issues/376)
- `c`: Fixed results being truncated when listing all jobs on a system. [#735](https://github.com/zowe/zowex/issues/735)
- `c`: Fixed default behavior of `zowex job list` command to list jobs for only the current user. [#739](https://github.com/zowe/zowex/issues/739)
- `c`: Added `--status` argument to `zowex job list` command that supports filtering by job status. [#743](https://github.com/zowe/zowex/issues/743)
- `zowed`: Fixed `maxItems` property being ignored when listing data sets and jobs. [#745](https://github.com/zowe/zowex/issues/745)
- `c`: Fixed an issue where writing to a `RECFM=U` data set could exhibit undefined behavior. Now, writing to a `RECFM=U` data set results in an explicit error as the record format is supported as read-only. [#751](https://github.com/zowe/zowex/pull/751)
- `c`: Fixed an issue where BPAM write operations would accidentally wipe the contents of a data set if one of the lines exceeded the record length. Now, the line is truncated according to the max record length of the data set, and the user is warned about line ranges that exceed the record length. [#587](https://github.com/zowe/zowex/issues/587)
- `c`: PDS and PDSE members are now edited using a partitioned access method to preserve and update ISPF statistics. [#587](https://github.com/zowe/zowex/issues/587)
- `c`: Added support for handling ASA control characters when writing to sequential and partitioned data sets. [#751](https://github.com/zowe/zowex/pull/751)
- `c`: Added support for line truncation detection when writing to data sets. The truncated line ranges are displayed as a warning message after the write process is complete. [#751](https://github.com/zowe/zowex/pull/751)
- `c`: Fixed an issue where the output stream was checked to determine the input method for write commands, causing unexpected behavior when piping data to the commands. Now, the commands check if the input stream is a pseudo-terminal before continuing. [#756](https://github.com/zowe/zowex/issues/756)
- `c`: Fixed an issue where the write commands would inadvertently add duplicate newlines when reading user input in interactive mode. Now, a newline character is only added between lines and after the first line of text is processed. [#755](https://github.com/zowe/zowex/issues/755)

## `0.2.2`

- `c`: Re-enable `zowex ds compress` and correct 0C4 abend. [#640](https://github.com/zowe/zowex/issues/640)
- `c`: Fixed an issue where `--wait ACTIVE` on `zowex job` commands would wait indefinitely if the job was fast enough to reach the `OUTPUT` phase before polling its status. [#700](https://github.com/zowe/zowex/pull/700)
- `zowed`: Fixed `message` property of `error` object in the JSON response to contain valuable details about errors thrown by `zowex`. [#712](https://github.com/zowe/zowex/pull/712)
- `c`: You can now access dynamic arguments from a command handler through its `InvocationContext` parameter. [#715](https://github.com/zowe/zowex/pull/715)
- `zowed`: Fixed an issue where the `submitJcl` command failed if an encoding was not specified for the JCL contents. [#724](https://github.com/zowe/zowex/pull/724)
- `c`: Fixed an issue where multibyte codepages like IBM-939 were not always encoded correctly when writing data sets and USS files. [#718](https://github.com/zowe/zowex/pull/718)

## `0.2.1`

- `c`: Disallow `zowex compress ds` to prevent lurking abend.
- `c`: Fixed `zowex uss create-file` and `create-dir` commands to respect the `mode` option when provided. [#626](https://github.com/zowe/zowex/pull/626)
- `c`: Fixed issue where `zowex uss chown` (and `zusf_chown_uss_file_or_dir`) silently succeeded with exit code `0` when a non-existent user or group was supplied. The command now validates `user:group` input and returns a non-zero exit code with a clear error message when invalid. [#565](https://github.com/zowe/zowex/pull/565)
- `c`: Fixed issue where uploading changes to a data set did not always flush to disk. [#643](https://github.com/zowe/zowex/issues/643)
- `c`: Updated commands that read data from stdin to read literal text rather than parsing hex string. [#645](https://github.com/zowe/zowex/pull/645)
- `c`: Reverted previous fix to preserve ISPF stats on PDS members since it could cause uploading changes to fail. [#669](https://github.com/zowe/zowex/pull/669)
- `zowed`: Fixed issue where enabling verbose logging would cause a deadlock during initialization. [#652](https://github.com/zowe/zowex/issues/652)
- `zowed`: Implemented support for automatic worker recovery. If a worker crashes or throws an exception, it is replaced with a new worker. If the maximum number of replacement attempts have been exceeded, the worker is disabled to prevent thrashing the CPU with replacement requests. [#410](https://github.com/zowe/zowex/issues/410)
- `zowed`: Optimized the `get_ready_worker` function to avoid worst-case linear search for the next available worker. [#651](https://github.com/zowe/zowex/pull/651)
- `zowed`: Replaced the options logic with the parser library to avoid code duplication and establish consistency with backend. [#655](https://github.com/zowe/zowex/issues/655)
- `zowed`: Implemented support for server-side request timeouts. If the request timeout is exceeded for a single worker, the hanging worker is replaced and the ongoing request is discarded. [#416](https://github.com/zowe/zowex/issues/416)
- `c`: De-duplicated makefile contents through `.INCLUDE` keyword and separate toolchain file. [#651](https://github.com/zowe/zowex/pull/651)
- `zowed`: Fixed issue where opening a file larger than 10 MB could fail with "Invalid JSON" error. [#656](https://github.com/zowe/zowex/issues/656)
- `c`: Fix PSW alignment issue. [#559](https://github.com/zowe/zowex/issues/559)
- When listing data sets with attributes, a comprehensive set is now retrieved that is similar to what ISPF displays. [#629](https://github.com/zowe/zowex/issues/629)

## `0.2.0`

- `c`: Fixed issue where uploading changes to a PDS member removed its ISPF stats. [#556](https://github.com/zowe/zowex/issues/556)
- `c`: Streamlined argument checks and access within command handlers. Handlers can now use `get`, `get_if` and `find` to search for arguments and retrieve their values, regardless of whether the argument is a keyword or positional argument. [#574](https://github.com/zowe/zowex/pull/574)
- `c`: Command handlers can now be called directly by providing an `InvocationContext`. The context provides helper functions for error, input, and output stream redirection. Handlers can use this context to print output and errors, as well as setting content length. [#574](https://github.com/zowe/zowex/pull/574)
- `c`: Organized command handlers into separate source files and namespaces in the `commands` folder. Reduced size of main entrypoint function by moving command registration to a `register_commands` function contained in each namespace. [#409](https://github.com/zowe/zowex/issues/409)
- `native`: Added a `--depth` option to the `zowex uss list` command for listing USS directories recursively. [#575](https://github.com/zowe/zowex/pull/575)
- `native`: Added `zowex job list-proclib` to list proclib concatenation.
- `c`: Fixed issue where a directory handle was leaked when running `chmod`, `chown`, `chtag` or `delete` USS commands. [#577](https://github.com/zowe/zowex/issues/577)
- `c`: Fixed issue where a file handle was leaked if the `fldata` function call failed when listing data set members. [#577](https://github.com/zowe/zowex/issues/577)
- `c`: Fixed issue where a file handle was leaked if the `fldata` function call failed in the `handle_data_set_compress` command handler. [#577](https://github.com/zowe/zowex/issues/577)
- `c`: Fixed issue in the `zut_substitute_symbol` function where passing patterns with a length greater than the input buffer would cause a buffer overflow. [#577](https://github.com/zowe/zowex/issues/577)
- `c`: Fixed undefined behavior in the `zut_bpxwdyn` function by verifying that the allocated 31-bit region is valid before writing to it. [#577](https://github.com/zowe/zowex/issues/577)
- `c`: The CLI parser now prints unexpected, trailing positional arguments when handling command input. [#594](https://github.com/zowe/zowex/issues/594)
- Rewrote the middleware (`zowed`) in C++. This removes our dependency on Golang, improves performance, and allows parallel requests to be handled in a single address space. [#595](https://github.com/zowe/zowex/pull/595)
- `c`: Fixed issue where automatic codepage conversion with `_BPXK_AUTOCVT` was not disabled for all USS read and write operations, which could interfere with the `--encoding` option. [#620](https://github.com/zowe/zowex/pull/620)

## `0.1.10`

- `c`: Added `zowex tool list-parmlib` command to list parmlib concatenation data sets.
- Added plug-in support to the `zowex` backend. Plug-ins can contribute commands that users invoke through `zowex`. For more information on how to create and register a plug-in with `zowex`, please refer to the `plugins.md` file in the `doc/` root-level folder. [#148](https://github.com/zowe/zowex/issues/148)

## `0.1.9`

- `golang`: Fixed an issue where an empty response on the `HandleReadFileRequest` function would result in a panic. [#550](https://github.com/zowe/zowex/pull/550)
- `c`: Fixed issue where the `zowex ds lm` command always returned non-zero exit code for warnings and ignored the `--no-warn` flag. [#498](https://github.com/zowe/zowex/issues/498)
- `c`: Fixed issue where the `zowex job submit-jcl` command submitted the given JCL contents twice, causing two jobs to be created. [#508](https://github.com/zowe/zowex/issues/508)
- `c`: Implemented a logger for Metal C and C++ source code for diagnostics, debug information, and printing dumps. When enabled, log messages are written to a log file named `zowex.log` in a new `logs` folder, relative to the location of the `zowex` binary. [#107](https://github.com/zowe/zowex/issues/107)
- `golang`: Moved location of log file inside "logs" directory to be consistent with `zowex`. [#514](https://github.com/zowe/zowex/pull/514)
- `c`: Fixed issue where the `zowex ds write` command automatically created a data set when it did not exist. [#292](https://github.com/zowe/zowex/issues/292)
- `native`: Fixed issue where the `zowex ds ls` command could hang when listing data sets that the system cannot open. [#496](https://github.com/zowe/zowex/issues/496)
- `c`: Added `--local-encoding` option for read and write operations on data sets, USS files, and job files to specify the source encoding of content (defaults to UTF-8). [#511](https://github.com/zowe/zowex/issues/511)
- `c`: Fixed issue where the `zowex ds create` command did not parse `--alcunit` and integer arguments (e.g., `--primary`). [#414](https://github.com/zowe/zowex/issues/414)
- `c`: Fixed issue where listing data sets fails if the `OBTAIN` service fails while obtaining attributes for a data set in the list of matches. [#529](https://github.com/zowe/zowex/issues/529)
- `native`: Added support for `volser` option when reading/writing data sets. [#439](https://github.com/zowe/zowex/issues/439)
- `native`: Reduced number of memory allocations in vectors by reserving capacity before adding elements. [#522](https://github.com/zowe/zowex/issues/522)
- `c`: Added wrappers for Web Enablement Toolkit to be invoked via Metal C as header only or from LE-C using a 64-bit wrapper.
- `c`: Added the `zstd::optional` class for handling optional values.
- `c`: Added the `zstd::unique_ptr` class and `zstd::make_unique` function for RAII-based automatic memory management.
- `c`: Added the `zstd::expected` class for error handling similar to Rust `Result` type.

## `0.1.8`

- `native`: Added default value for `--recfm` so that when no options are specified the data set will not contain errors. [#493](https://github.com/zowe/zowex/issues/493)
- Fixed issue where special characters were detected as invalid characters when provided to `zowex` commands. [#491](https://github.com/zowe/zowex/issues/491)
- `native`: Increase default max returned entries in `zowex ds list` from 100 to 5000. This helps with [#487](https://github.com/zowe/zowex/issues/487) but does not fix it. In the future, users should be able to specify on the Zowe Clients the max number of entries.

## `0.1.7`

- Updated CLI parser `find_kw_arg_bool` function to take in an optional boolean `check_for_negation` that, when `true`, looks for a negated option value. [#485](https://github.com/zowe/zowex/issues/485)
- Fixed issue where listing data set members did not check for the negated option value. Now, the command handler passes the `check_for_negation` option to the `find_kw_arg_bool` function to check the value of the negated, equivalent option. [#485](https://github.com/zowe/zowex/issues/485)
- `golang`: Fixed inconsistent type of the `data` property between the `ReadDatasetResponse` and `ReadFileResponse` types. [#488](https://github.com/zowe/zowex/pull/488)

## `0.1.6`

- `native`: Fixed regression where data set download operations would fail due to a content length mismatch, due to the content length being printed as hexadecimal rather than decimal. [#482](https://github.com/zowe/zowex/issues/482)

## `0.1.5`

- `native`: Added completion code for `POST` so that users of the library code may determine if a timeout has occurred.
- `native`: Added `timeout` for `zowex console issue` to prevent indefinite hang when no messages are returned. [#470](https://github.com/zowe/zowex/pull/470)
- `native`: Added `contentLen` property to RPC responses for reading/writing data sets and USS files. [#358](https://github.com/zowe/zowex/pull/358)
- `native`: Fixed file tag being prioritized over user-specified codepage when reading/writing USS files. [#467](https://github.com/zowe/zowex/pull/467)
- `native`: Fixed issue where `max-entries` argument was incorrectly parsed as a string rather than an integer. [#469](https://github.com/zowe/zowex/issues/469)
- `native`: The `zowex` root command now has a command handler to make adding new options easier. [#468](https://github.com/zowe/zowex/pull/468)

## `0.1.4`

- `c`: Fixed an issue where the CLI help text showed the `[options]` placeholder in the usage example before the positional arguments, which is not a supported syntax. Now, the usage text shows the `[options]` placeholder after the positional arguments for the given command.
- `c`: Added `zowex version` command to print the latest build information for the `zowex` executable. The version output contains the build date and the package version. [#366](https://github.com/zowe/zowex/issues/366)
- `c`: Added `full_status` variable from job output to the CSV output for the `zowex job view-status --rfc` command. [#450](https://github.com/zowe/zowex/pull/450)
- `golang`: If the `zowed` process abnormally terminates due to a SIGINT or SIGTERM signal, the worker processes are now gracefully terminated. [#372](https://github.com/zowe/zowex/issues/372)
- `c`: Updated `zowex uss list` command to provide same attributes as output from the `ls -l` UNIX command when the `--long` flag is specified. [#346](https://github.com/zowe/zowex/issues/346)
- `c`: Updated `zowex uss list` command to match format of the `ls -l` UNIX command. [#383](https://github.com/zowe/zowex/issues/383)
- `c`: Added `response-format-csv` option to the `zowex uss list` command to print the file attributes in CSV format. [#346](https://github.com/zowe/zowex/issues/346)
- `golang`: Added additional data points to the USS item response for the `HandleListFilesRequest` command handler. [#346](https://github.com/zowe/zowex/issues/346)

## `0.1.3`

- `c`: Fixed S0C4 where supervisor state, key 4 caller invokes `zcn_put` while running in SUPERVISOR state. [#392](https://github.com/zowe/zowex/issues/392)
- `c`: Fixed S0C4 where supervisor state, key 4 caller invokes `zcn` APIs and several miscellaneous issues. [#389](https://github.com/zowe/zowex/issues/389)
- `c`: Fixed issue where canceled jobs displayed as "CC nnnn" instead of the string "CANCELED". [#169](https://github.com/zowe/zowex/issues/169)
- `c`: Fixed issue where input job SYSOUT files could not be listed or displayed". [#196](https://github.com/zowe/zowex/issues/196)
- `native`: Fixed issue where `zowed` failed to process RPC requests larger than 64 KB. [#364](https://github.com/zowe/zowex/pull/364)
- `golang`: `zowed` now returns the UNIX permissions for each item's `mode` property in a USS list response. [#341](https://github.com/zowe/zowex/pull/341)
- `c`: Added `--long` option to the `zowex uss list` command to return the long format for list output, containing additional file metadata. [#341](https://github.com/zowe/zowex/pull/341)
- `c`: Added `--all` option to the `zowex uss list` command to show hidden files when listing a UNIX directory. [#341](https://github.com/zowe/zowex/pull/341)
- `c`: Added `ListOptions` parameter to the `zusf_list_uss_file_path` function to support listing the long format and hidden files. [#341](https://github.com/zowe/zowex/pull/341)
- `c`: Fixed issue where the record format (`recfm` attribute) was listed as unknown for a Partitioned Data Set (PDS) with no members. Now, the record format for all data sets is retrieved through the Volume Table of Contents. [#351](https://github.com/zowe/zowex/pull/351)
- `c`: Added CLI parser and lexer library for use in `zowex`. For an example of how to use the new CLI parser library, refer to the sample CLI code in `examples/native-cli/testcli.cpp`.
- `c`: Fixed an issue where the zowex `ds list` command always printed data set attributes when passing the argument `--response-format-csv`, even if the attributes argument was `false`.
- `c`: Fixed an issue where the `zusf_chmod_uss_file_or_dir` function did not handle invalid input before passing the mode to the `chmod` C standard library function. [#399](https://github.com/zowe/zowex/pull/399)
- `c`: Refactored the Base64 encoder and decoder to remove external dependency. [#385](https://github.com/zowe/zowex/issues/385)

## `0.1.2`

- `golang`: `zowed` now prints a ready message once it can accept input over stdin. [#221](https://github.com/zowe/zowex/pull/221)
- `golang`: Reduced startup time for `zowed` by initializing workers in the background. [#237](https://github.com/zowe/zowex/pull/237)
- `golang`: Added verbose option to enable debug logging. [#237](https://github.com/zowe/zowex/pull/237)
- `golang`: Added SHA256 checksums to the ready message to allow checks for outdated server. [#236](https://github.com/zowe/zowex/pull/236)
- `c,golang`: Added support for streaming USS file contents for read and write operations (`zusf_read_from_uss_file_streamed`, `zusf_write_to_uss_file_streamed`). [#311](https://github.com/zowe/zowex/pull/311)
- `c,golang`: Added support for streaming data set contents for read and write operations (`zds_read_from_dsn_streamed`, `zds_write_to_dsn_streamed`). [#326](https://github.com/zowe/zowex/pull/326)
- `c`: Added support for `recfm` (record format) attribute when listing data sets.
- `c`: Fixed issue where data sets were not opened with the right `recfm` for reading/writing.

## `0.1.1`

- `c`: Fixed issue where running the `zowex uss write` or `zowex ds write` commands without the `--etag-only` parameter resulted in a S0C4 and abrupt termination. [#216](https://github.com/zowe/zowex/pull/216)
- `c`: Fixed issue where running the `zowex uss write` or `zowex ds write` commands without the `--encoding` parameter resulted in a no-op. [#216](https://github.com/zowe/zowex/pull/216)
- `golang`: Fixed issue where a jobs list request returns unexpected results whenever a search query does not match any jobs. [#217](https://github.com/zowe/zowex/pull/217)
- `c`: Fixed issue where e-tag calculation did not match when a data set was saved with the `--encoding` parameter provided. [#219](https://github.com/zowe/zowex/pull/219)

## `0.1.0`

- `c`: Enable `langlvl(extended0x)` for C++ code to support C++0x features (`auto`, `nullptr`, etc.) [#35](https://github.com/zowe/zowex/pull/35)
- `c`: Replace all applicable `NULL` references with `nullptr` in C++ code. [#36](https://github.com/zowe/zowex/pull/36)
- `c,golang`: Add support for writing data sets and USS files with the given encoding. [#37](https://github.com/zowe/zowex/pull/37)
- `golang`: Added `restoreDataset` function in the middleware. [#38](https://github.com/zowe/zowex/pull/38)
- `golang`: Added `procstep` and `dsname` to the list of fields when getting spool files
- `golang`: Added `tygo` to generate types based on the Go types for the TypeScript SDK. [#71](https://github.com/zowe/zowex/pull/71)
- `c,golang`: Added `watch:native` npm script to detect and upload changes to files during development. [#100](https://github.com/zowe/zowex/pull/100)
- `c,golang`: Added `chmod`, `chown`, `chtag` and `delete` functionality for USS files & folders. [#143](https://github.com/zowe/zowex/pull/143)
- `c,golang`: Added support for recursive `chmod` functionality for USS folders. [#143](https://github.com/zowe/zowex/pull/143)
- `c,golang`: Added capability to submit JCL from stdin. [#143](https://github.com/zowe/zowex/pull/143)
- `c`: Add support for `mkdir -p` behavior when making a new USS directory. [#143](https://github.com/zowe/zowex/pull/143)
- `golang`: Refactored error handling in Go layer to forward errors to client as JSON. [#143](https://github.com/zowe/zowex/pull/143)
- `c`: Fixed dangling pointers in CLI code & refactored reading resource contents to avoid manual memory allocation. [#167](https://github.com/zowe/zowex/pull/167)
- `golang`: Added `createDataset` function in the middleware. [#95](https://github.com/zowe/zowex/pull/95)
- `c,golang`: Added `createMember` function. [#95](https://github.com/zowe/zowex/pull/95)
- `c,golang`: Added `cancelJob` function. [#138](https://github.com/zowe/zowex/pull/138)
- `c,golang`: Added `holdJob` and `releaseJob` functions. [#182](https://github.com/zowe/zowex/pull/182)
- `c`: Fixed issue where data set search patterns did not return the same results as z/OSMF. [#74](https://github.com/zowe/zowex/issues/74)
- `c`: Added check for maximum data set pattern length before making a list request. Now, requests with patterns longer than 44 characters are rejected. [#185](https://github.com/zowe/zowex/pull/185)
- `c,golang`: Fixed issue where submit JCL handler did not convert input data from UTF-8 and did not support an `--encoding` option. [#198](https://github.com/zowe/zowex/pull/198)
- `c`: Fixed issue where submit JCL handler did not support raw bytes from stdin when the binary is directly invoked through a shell. [#198](https://github.com/zowe/zowex/pull/198)
- `c,golang`: Added `submitUss` function. [#184](https://github.com/zowe/zowex/pull/184)
- `golang`: Fixed issue where listing a non-existent data set pattern resulted in a panic and abrupt termination of `zowed`. [#200](https://github.com/zowe/zowex/issues/200)
- `golang`: Fixed issue where a newline was present in the job ID when returning a response for the "submitJcl" command. [#211](https://github.com/zowe/zowex/pull/211)
- `c`: Added conflict detection for USS and Data Set write operations through use of the `--etag` option. [#144](https://github.com/zowe/zowex/issues/144)
- `golang`: Added `Etag` property to request and response types for both USS and Data Set write operations. [#144](https://github.com/zowe/zowex/issues/144)

## [Unreleased]

- Initial release
