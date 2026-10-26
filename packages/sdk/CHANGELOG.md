# Change Log

All notable changes to the Client code for "@zowe/zowex-for-zowe-sdk" are documented in this file.

Check [Keep a Changelog](http://keepachangelog.com/) for recommendations on how to structure this file.

## Recent Changes

- BugFix: Censored sensitive info when logging RPC request and response details in ZSshClient. [#1190](https://github.com/zowe/zo/pull/1190)
- Fixed SSH utility helpers closing their connection before asynchronous operations completed when passed an `SshSession`. [#1195](https://github.com/zowe/zo/issues/1195)

## `1.0.2`

- Fixed an error when issuing streamed RPC requests to a `zowex` server running in Bash shell. [#1155](https://github.com/zowe/zowex/pull/1155)
- Updated RPC command method signatures so that the parameters object is optional when none of the request properties are required. [#1174](https://github.com/zowe/zowex/pull/1174)
- Added `SessionContext` class to cache SSH connections so they can be reused across utility methods. [#1144](https://github.com/zowe/zowex/pull/1144)
- Added support for SSH agents (`IdentityAgent` in SSH config) as an authentication method. [#342](https://github.com/zowe/zowex/issues/342)

## `1.0.1`

- Enhanced the post-failure cleanup in the `ZSshUtils.installServer` method to delete the `zo` program if the installation process failed while extracting the `server.pax.Z` file. [#1143](https://github.com/zowe/zowex/pull/1143)
- Fixed an error where `zowex` is not found on the PATH if the OMVS shell is Bash. The expected behavior is that the `zowex` binary is automatically deployed. [#1141](https://github.com/zowe/zowex/pull/1141)
- Exported the `ZSshConstants` module so that extenders can access its constants. [#1142](https://github.com/zowe/zowex/pull/1142)

## `1.0.0`

- **Breaking** The `zowex` program bundled in `server.pax.Z` is now renamed to `zo`. [#1119](https://github.com/zowe/zowex/pull/1119)
- Added JSON-RPC support for listing parmlib data sets. [#1124](https://github.com/zowe/zowex/pull/1124)
- Added support for the `resolveDsAlias` RPC command which allows the caller to resolve data set aliases. [#1108](https://github.com/zowe/zowex/pull/1108)

## `0.9.0`

- Enable console commands through JSON-RPC. [#1112](https://github.com/zowe/zowex/pull/1110)

## `0.8.1`

- Fixed the `SearchParser.parseSearchOutput` function silently dropping matches when searching a sequential data set [#1104](https://github.com/zowe/zowex/pull/1104)

## `0.8.0`

- Added detection of available disk space on the z/OS Unix deployment directory in the `ZSshUtils.installServer` method, giving the user the opportunity to cancel deployment if they have less than the recommended amount of available space. [#1088](https://github.com/zowe/zowex/pull/1088)
- Fixed server deploy/setup failing with "SSH setup cancelled" when connecting with a private key that requires a passphrase, even after entering the correct passphrase. [#1094](https://github.com/zowe/zowex/pull/1094)
- Added a `certificates` client API for managing ESM (RACF or equivalent) key rings and digital certificates, exposing the following functions: `createKeyring`, `deleteKeyring`, `refreshDigtcert`, `deleteCertificate`, `listCertificates`, `exportCertificate`, `importCertificate`, `showCertificate`, `listRings`, `countRing`, `connectCertificate`, `setDefaultCertificate`, `trustCertificate`, and `renameCertificate`. [#1079](https://github.com/zowe/zowex/pull/1079)
- Added recognition of Language Environment load failures. `CEE3561S` and `CEE3501S` error codes now produce an actionable error naming the required z/OS release and Language Environment APARs, with `errorCode: "ELERUNTIME"`, instead of a generic "Error starting Zowe server." Startup output is now accumulated before it is classified, so a diagnostic split across reads is still recognized. [#871](https://github.com/zowe/zowex/issues/871)
- Added a verification step to `installServer()` that runs the installed server binary and reports whether the remote system can load it, so a runtime mismatch surfaces during installation rather than on the next operation. The target system level from the `uname -srv` command is included in the error details. [#871](https://github.com/zowe/zowex/issues/871)
- Added `exactMatch` property to the `ListDatasetsRequest` type. This option defaults to false for backwards compatibility, but can be set to true to avoid appending `.**` to the end of data set patterns. [#914](https://github.com/zowe/zowex/issues/914)
- When a command fails, a structured error payload attached by the server (e.g. the `safReturns` SAF/ESM codes from certificate commands) is now delivered to SDK consumers via the `ImperativeError`'s `causeErrors`, instead of being flattened into the error text. [#1079](https://github.com/zowe/zowex/pull/1079)

## `0.7.1`

- Fixed SSH connection softlocks and resource leaks when a connection drops or stalls during startup: the `ZSshClient.create` function now rejects when the connection closes before the server is ready or when a `serverStartupTimeout` (default 60s) elapses, cleans up the SSH connection on startup failure, rejects pending requests fast when the connection closes, and supports the `keepAliveCountMax` option (default 3). [#1076](https://github.com/zowe/zowex/pull/1076)
- Fixed requests hanging until the response timeout when the Zowe Remote SSH server process ended while the SSH transport stayed healthy. `ZSshClient` now watches the server channel for the lifetime of the client and rejects requests written to a channel that is no longer writable. [#1076](https://github.com/zowe/zowex/pull/1076)
- Fixed socket errors raised while tearing down a closed client being reported through the `onError` handler, which surfaced spurious errors to callers during a normal disconnect. [#1076](https://github.com/zowe/zowex/pull/1076)

## `0.7.0`

- **Breaking:** Updated the `checkIfOutdated()` utility to compare version numbers with semantic versioning rather than comparing checksums of the `zowex` program. Its parameter is changed to accept the version number of `zowex` on the remote system rather than checksums, and it is no longer an `async` method. [#1073](https://github.com/zowe/zowex/pull/1073/)
- **Breaking:** Removed the `checksums.asc` file from `zowex` server pax files and removed the `serverChecksums` getter method from the `ZSshClient` class. [#1073](https://github.com/zowe/zowex/pull/1073/)
- Added `serverVersion` getter method to the `ZSshClient` class. [#1073](https://github.com/zowe/zowex/pull/1073/)
- Added support to list link list data sets. [#1061](https://github.com/zowe/zowex/pull/1061)

## `0.6.1`

- Added new functions to the `ZSshUtils` class called `detectServerOnPath()` and `lacksWriteAccess()`. [#1055](https://github.com/zowe/zowex/pull/1055)
- Added support to list APF and PROCLIB data sets. [#1029](https://github.com/zowe/zowex/pull/1029)
- Added support for viewing syslog.
- Added response content for copy data sets and members operations. [#995](https://github.com/zowe/zowex/pull/995)

## `0.6.0`

- Added `disableCreateNewProfile` option to `AbstractConfigManager.promptForProfile` function to prevent profile creation and migration, allowing selection only from existing SSH profiles. [#971](https://github.com/zowe/zowex/pull/971)
- **Breaking:** Modified `promptForProfile` function to use an options object for `setExistingProfile` and `prioritizeProjectLevelConfig` parameters. [#964](https://github.com/zowe/zowex/pull/964)
- Added `prioritizeProjectLevelConfig` to the options parameter for `promptForProfile` function to allow toggling between project-level and global configuration creation when setting up new SSH profiles. [#964](https://github.com/zowe/zowex/pull/964)
- Updated the `russh` dependency for technical currency. [#963](https://github.com/zowe/zowex/pull/963)

## `0.5.0`

- Added support for copying data sets and members files in the `RpcClientApi` class. [#932](https://github.com/zowe/zowex/issues/932)
- Fixed an issue when searching a single member did not returned properly parsed results. [#941](https://github.com/zowe/zowex/issues/941)
- Added support for invoking the `getInfo` server command, which allows the client SDK to get version and build information from the server. [#922](https://github.com/zowe/zowex/pull/922)
- Added warning to `AbstractConfigManager.validateDeployPath` method when server path ends in `/c/build-out`, preventing developers from accidentally overwriting a dev deployment. [#912](https://github.com/zowe/zowex/pull/912)
- Added `overwrite` property to the `CreateMemberRequest` and `CreateFileRequest` types. [#642](https://github.com/zowe/zowex/issues/642)

## `0.4.0`

- **Breaking:** `ZSshClient` (the SSH-based client for communicating with the z/OS server) now connects to `zowex server` (the embedded server subcommand of the `zowex` z/OS backend binary) instead of the standalone `zowed` binary. Users must re-deploy the server to update the binaries at their `serverPath` (the z/OS path where server binaries are deployed). [#846](https://github.com/zowe/zowex/issues/846)
- Updated `ZSshClient` so callers can now safely collect and replay in-flight requests when they detect an unrecoverable error. [#548](https://github.com/zowe/zowex/issues/548)
- Added support for issuing USS shell commands via `uss.issueCmd` (backed by the new `unixCommand` RPC). [#867](https://github.com/zowe/zowex/pull/867)
- Added expired password detection (`FOTS1668`/`FOTS1669`) in `installServer` and `uninstallServer`, throwing an `ImperativeError` with `errorCode: "EPASSWD_EXPIRED"` instead of returning a generic failure. [#867](https://github.com/zowe/zowex/pull/867)
- Added support for listing member attributes to `DsMember` type [#630](https://github.com/zowe/zowex/issues/630)
- Added support for invoking the `toolSearch` server command, which allows the client SDK to search for data sets.
- Added support for copying USS files in the `RpcClientApi` class. [#379](https://github.com/zowe/zowex/issues/379)
- Added support for USS Move operations. [#789](https://github.com/zowe/zowex/pull/789)
- Fixed `user` property not defined as secure when new team configuration profile is created. [#845](https://github.com/zowe/zowex/pull/845)
- Fixed check for outdated server always returning true which could trigger unnecessary re-deploy. [#862](https://github.com/zowe/zowex/pull/862)
- Added experimental native client for improved performance. To enable it, pass the `useNativeSsh` option to the `ZSshClient.create` method. [#833](https://github.com/zowe/zowex/pull/833)

## `0.3.0`

- Changed SDK groupings to align with zowex. [#807](https://github.com/zowe/zowex/issues/807)

## `0.2.4`

- Added support for renaming data set members in the `RpcClientApi` class. [#765](https://github.com/zowe/zowex/pull/765)
- Made the `recursive` property optional for USS request types. [#648](https://github.com/zowe/zowex/issues/648)
- Fixed an issue where deploying to the `/tmp` directory would result in an error. [#781](https://github.com/zowe/zowex/pull/781)
- Added `multivolume` property to the `Dataset` type for listing data sets with attributes. [#782](https://github.com/zowe/zowex/pull/782)
- Exported `ISshSession` and `SshSession` types from the Zowe USS SDK. [#799](https://github.com/zowe/zowex/pull/799)
- Added `verbose` property to the `ClientOptions` interface used by the `ZSshClient.create` method. [#801](https://github.com/zowe/zowex/issues/801)
- Enhanced the `ZSshClient.create` method to return `ImperativeError` containing error details when `zowed` fails to start. [#802](https://github.com/zowe/zowex/issues/802)

## `0.2.3`

- Added support for renaming data sets in the `RpcClientApi` class. [#376](https://github.com/zowe/zowex/issues/376)
- Fixed an issue where connecting to the host with an expired password resulted in an ambiguous error. [#732](https://github.com/zowe/zowex/issues/732)
- The `submitJcl` and `submitJob` functions in the `SshJesApi` class now return the job name as a property (`jobname`) within the response object. [#733](https://github.com/zowe/zowex/issues/733)
- Enhanced the `Job` type to include more properties such as `subsystem`, `owner`, `type`, and `class` when listing jobs. [#749](https://github.com/zowe/zowex/pull/749)
- Added `bin` folder that bundles server binaries in the SDK package. [#760](https://github.com/zowe/zowex/pull/760)
- Data set truncation warnings are now included in the response object for write operations. [#751](https://github.com/zowe/zowex/pull/751)

## `0.2.2`

- Fixed an issue where the deploy directory was not created recursively when a non-existent multi-level directory was submitted. [#705](https://github.com/zowe/zowex/pull/705)
- Fixed an issue where global team config was created rather than storing profile in existing team config in the active workspace directory. [#710](https://github.com/zowe/zowex/pull/710)

## `0.2.1`

- Fixed an issue where `validateConfig` function would not fallback to password when both an invalid `privateKey` and `password` exist on a profile. [#525](https://github.com/zowe/zowex/issues/525)
- Updated the `num-workers` option in the `ZSshClient` class to reflect the new format used for parsing `zowed` options. [#655](https://github.com/zowe/zowex/issues/655)
- Filtered out `Include` and `Host *` directives from SSH config files when creating new profiles. [#678](https://github.com/zowe/zowex/pull/678)
- Added missing properties to the `Dataset` type for listing data sets with attributes. [#629](https://github.com/zowe/zowex/issues/629)

## `0.2.0`

- Added unit tests for `SshConfigUtils` class. [#614](https://github.com/zowe/zowex/pull/614)
- Addressed an issue where `~` was not being resolved to the home directory within ssh configuration files. [#614](https://github.com/zowe/zowex/pull/614)
- Added additional error messages to the `AbstractConfigManager` class to provide better feedback during connection attempts. [#605](https://github.com/zowe/zowex/pull/605)
- Added `depth` property to the `ListFilesRequest` type for listing USS directories recursively. [#575](https://github.com/zowe/zowex/pull/575)
- Replaced `this.uninstallServer` with the class reference `ZSshUtils.uninstallServer` in `ZSshUtils.ts`. [#586](https://github.com/zowe/zowex/pull/586)
- Added `recfm` property to the `Dataset` type for listing data sets with attributes. [#558](https://github.com/zowe/zowex/pull/558)
- Restructured RPC request and response types to be human-maintained rather than auto-generated to improve maintainability. [#590](https://github.com/zowe/zowex/pull/590)
- Made attribute properties optional in the `Dataset` and `UssItem` types. [#608](https://github.com/zowe/zowex/pull/608)
- Fixed an issue where the input validation logic in the `AbstractConfigManager.promptForDeployDirectory` function would falsely detect paths as invalid. [#609](https://github.com/zowe/zowex/issues/609)

## `0.1.10`

- Added a `promptForDeployDirectory` function to prompt the users to choose a deploy directory aside from the default. [#527](https://github.com/zowe/zowex/issues/527)

## `0.1.9`

- Updated the `ZSshUtils.uninstallServer` function to remove the deploy directory and all of its contents. [#484](https://github.com/zowe/zowex/issues/484)
- Changed default number of `zowex` worker threads from 10 to 3 to reduce resource usage on z/OS. [#514](https://github.com/zowe/zowex/pull/514)
- Added support for `localEncoding` option in data set, USS file, and job file operations to specify the source encoding of content (defaults to UTF-8). [#511](https://github.com/zowe/zowex/issues/511)
- Added support for `volume` option when reading/writing data sets. [#439](https://github.com/zowe/zowex/issues/439)
- Added a new `ConfigFileUtils` class with helper functions for commenting out properties, removing comments after the `properties` section of a profile, and restoring properties from comments to their original values. [#534](https://github.com/zowe/zowex/issues/534)
- Added an `onError` callback to the `installServer` and `uninstallServer` functions, allowing applications to implement custom error handling and retry logic during server deployment. [#533](https://github.com/zowe/zowex/pull/533)

## `0.1.7`

- Fixed inconsistent type of the `data` property between the `ReadDatasetResponse` and `ReadFileResponse` types. [#488](https://github.com/zowe/zowex/pull/488)

## `0.1.6`

- Fixed a TypeError in `ZSshClient.request` that caused stream operations to fail if a callback was not provided for progress updates. [#482](https://github.com/zowe/zowex/issues/482)

## `0.1.5`

- Added support for progress messages for USS files downloaded and uploaded via the CLI plug-in. [#426](https://github.com/zowe/zowex/pull/426)
- Added support for listing the long format for USS files with the `long` request option. When provided, the SDK returns additional file attributes, including permissions, file tag, file size, number of links, owner, and group. [#346](https://github.com/zowe/zowex/issues/346)
- Added support for listing all USS files with the `all` request option. When provided, the SDK includes hidden files in the list response. [#421](https://github.com/zowe/zowex/pull/421)
- Refactored handling of RPC notifications to be managed by a separate class `RpcNotificationManager`. [#358](https://github.com/zowe/zowex/pull/358)
- Added content length validation for streamed requests. [#358](https://github.com/zowe/zowex/pull/358)
- Removed unnecessary prompt to unlock keyring on MacOS when connecting to a new host with a private key. [#453](https://github.com/zowe/zowex/issues/453)

## `0.1.3`

- The `mode` property for a list files response now contains the UNIX permissions of the corresponding file/folder. [#341](https://github.com/zowe/zowex/pull/341)
- Fixed an issue where a non-fatal `chdir` error (OpenSSH code `FOTS1681`) prevented the clients from handling the middleware's ready message. Now, the SSH client waits for the ready message from the `zowed` middleware before returning the new client connection.

## `0.1.2`

- Improved error handling when SSH client connects to throw `ENOTFOUND` if server binary is missing. [#44](https://github.com/zowe/zowex/issues/44)
- Added `ZSshUtils.checkIfOutdated` method to check if deployed server is out of date. [#44](https://github.com/zowe/zowex/issues/44)
- Added `keepAliveInterval` option when creating an instance of the `ZSshClient` class that defaults to 30 seconds. [#260](https://github.com/zowe/zowex/issues/260)
- Added support for streaming data in chunks to handle large requests and responses in the `ZSshClient` class. This allows streaming USS file contents for read and write operations (`ReadDatasetRequest`, `ReadFileRequest`, `WriteDatasetRequest`, `WriteFileRequest`). [#311](https://github.com/zowe/zowex/pull/311)

## `0.1.1`

- Improved unclear error message when uploading PAX file fails. [#220](https://github.com/zowe/zowex/pull/220)

## `0.1.0`

- Added `ds.restoreDataset` function. [#38](https://github.com/zowe/zowex/pull/38)
- Added support for cancelling jobs. [#138](https://github.com/zowe/zowex/pull/138)
- Added support for holding and releasing jobs. [#182](https://github.com/zowe/zowex/pull/182)
- Added `jobs.submitUss` function. [#184](https://github.com/zowe/zowex/pull/184)

## [Unreleased]

- Initial release
