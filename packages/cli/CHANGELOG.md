# Change Log

All notable changes to the Client code for "zowex-for-zowe-cli" are documented in this file.

Check [Keep a Changelog](http://keepachangelog.com/) for recommendations on how to structure this file.

## `1.1.0`

- Documented PBES2/AES-256 certificate exports, their System SSL/ICSF prerequisites, and the requirement for strong, unique 8–128-character passphrases in command help. [#1205](https://github.com/zowe/zo/pull/1205)
- Added the `zowe zo delete vsam` command with `--erase` and `--purge` options to delete VSAM clusters over SSH. [#490](https://github.com/zowe/zowex/issues/490)

## `1.0.2`

- Corrected the `create data-set-member` success message to say `Member` instead of `Dataset`. [#639](https://github.com/zowe/zowex/issues/639)
- Added support for SSH agents (`IdentityAgent` in SSH config) with the `--agent` option. [#342](https://github.com/zowe/zowex/issues/342)

## `1.0.1`

- Added `--response-timeout` option to all commands. It defaults to 60 seconds and can be increased for large uploads or downloads over slow connections. [#988](https://github.com/zowe/zowex/issues/988)

## `1.0.0`

- The `zowex` top level command group of this plug-in has been renamed to `zo`. `zowex` and `zssh` will still function as aliases. [#1119](https://github.com/zowe/zowex/pull/1119)
- Added the `zssh files list alias` command to the CLI. [#1108](https://github.com/zowe/zowex/pull/1108)

## `0.9.0`

- Added the `zssh issue console-command` command. The `--console-name` option is now optional and defaults to the current user ID. [#1112](https://github.com/zowe/zowex/issues/1112)

## `0.8.0`

- Added ESM (RACF or equivalent) certificate and key ring management to the `zssh system` command group over SSH: `zssh system keyring create|delete|list|list-rings|count` and `zssh system cert import|export|delete|show|connect|set-default|trust|rename|refresh`. [#1079](https://github.com/zowe/zowex/pull/1079)
- Added `--exact-match` option to the `zssh list data-set` command. This option defaults to false for backwards compatibility, but can be set to true to avoid appending `.**` to the end of data set patterns. [#914](https://github.com/zowe/zowex/issues/914)
- Fixed attribute columns being displayed when `zssh list data-set` is run without the `--attributes` option. [#1012](https://github.com/zowe/zowex/issues/1012)

## `0.6.1`

- Added `--encoding` flag to `zssh submit local-file` and `zssh submit stdin` commands to specify the encoding of the submitted JCL. [#1050](https://github.com/zowe/zowex/pull/1050)
- Added `zssh copy data-set` command to copy data sets and members with optional `--replace` and `--overwrite` flags. Supports PDS-to-PDS, member-to-member, and sequential-to-sequential copies. [#995](https://github.com/zowe/zowex/pull/995)
- Added the `zssh system view-syslog` command to the CLI.

## `0.6.0`

- Added `disableCreateNewProfile` option to the `promptForProfile` function to restrict profile creation during set-up operations. This allows selection from existing profiles only. [#971](https://github.com/zowe/zowex/pull/971)

## `0.4.0`

- Added an `--attributes` flag to list ISPF statistics for member attributes. [#630](https://github.com/zowe/zowex/issues/630)
- Added the `zssh uss copy` command to the CLI. [#379](https://github.com/zowe/zowex/pull/379).
- Updated `stream` fields to match new requirements from the SDK. [#548](https://github.com/zowe/zowex/issues/548)

## `0.3.0`

- Used new SDK groupings that align with zowex. [#807](https://github.com/zowe/zowex/pull/808)
- Added the `pattern` option to the `list data-set-members` command to filter the returned members. [#817](https://github.com/zowe/zowex/pull/817)

## `0.2.4`

- Added the `zssh rename data-set-member` command to the CLI. [#765] (https://github.com/zowe/zowex/pull/765).
- Added `+` suffix after volser for multi-volume data sets when listing data sets with attributes. [#782](https://github.com/zowe/zowex/pull/782)

## `0.2.3`

- Added the `zssh rename data-set` command to the CLI. [#376](https://github.com/zowe/zowex/issues/376).
- Added `owner` field to the table returned by the `zssh list jobs` and `zssh view job-status` commands. [#749](https://github.com/zowe/zowex/pull/749)
- Updated the `zssh server install` command to locate server PAX bundled in the SDK package. [#760](https://github.com/zowe/zowex/pull/760)
- The user is now warned if their data is truncated when uploading to a data set. Users can avoid truncation by keeping records within the maximum record length. [#751](https://github.com/zowe/zowex/pull/751)

## `0.2.1`

- When listing data sets with attributes, a comprehensive set is now retrieved that is similar to what ISPF displays. [#629](https://github.com/zowe/zowex/issues/629)

## `0.2.0`

- Updated `CliPromptApi` class to gracefully handle unused functions from the `AbstractConfigManager` class. [#605](https://github.com/zowe/zowex/pull/605)
- Added a `--depth` option to the `zssh list uss` command for listing USS directories recursively. [#575](https://github.com/zowe/zowex/pull/575)
- Added `zssh issue tso-command` command. [#595](https://github.com/zowe/zowex/pull/595)

## `0.1.10`

- Added a `storeServerPath` function as placeholder for future CLI functionality to persist deploy directory paths from user input. [#527](https://github.com/zowe/zowex/issues/527)

## `0.1.9`

- Added support for the `local-encoding` option to data set, USS file, and job file commands (view, download, upload) to specify the source encoding of content (defaults to UTF-8). [#511](https://github.com/zowe/zowex/issues/511)
- Added support for `--volume-serial` option when uploading/downloading data sets. [#439](https://github.com/zowe/zowex/issues/439)
- Fixed an issue where the `zssh config setup` command did not prompt the user for a password if the given private key was not recognized by the host. [#524](https://github.com/zowe/zowex/issues/524)
- Added a new prompt that shows if the user has an invalid private key on an existing profile when running the `zssh config setup` command. Now, if an invalid private key is detected, it is moved to a new comment in the JSON file and the user is given options to proceed. They can undo the comment action, delete the comment entirely, or preserve the comment and succeed with setup. [#524](https://github.com/zowe/zowex/issues/524)
- Added a `translateCliError` function to provide more user-friendly messages for an encountered error. If a user-friendly translation is unavailable, the original error is returned. [#533](https://github.com/zowe/zowex/pull/533)
- Updated the `zssh server install` command to display more user-friendly error messages if an issue was encountered during the installation process. [#228](https://github.com/zowe/zowex/issues/228)

## `0.1.5`

- Added support for progress messages for USS files downloaded and uploaded via the CLI plug-in. [#426](https://github.com/zowe/zowex/pull/426)
- Added support for listing the long format for USS files with the `long` request option. When provided, the CLI plug-in prints additional file attributes, including permissions, file tag, file size, number of links, owner, and group. [#346](https://github.com/zowe/zowex/issues/346)
- Added support for listing all USS files with the `all` request option. When provided, the CLI plug-in includes hidden files in the list response. [#421](https://github.com/zowe/zowex/pull/421)
- Added support for the `encoding` option to the `zssh upload uss` and the `zssh upload ds` commands. When the `encoding` option is provided, the command uses the option value as the target encoding for the uploaded content. [#427](https://github.com/zowe/zowex/issues/427)
- Added support for the `file` option to the `zssh download uss` and `zssh download ds` commands. When the `file` option is provided, the command uses the option value as the destination file path for the downloaded content. [#428](https://github.com/zowe/zowex/issues/428)
- Adopted streaming for commands that upload/download data sets and USS files. [#358](https://github.com/zowe/zowex/pull/358)

## `0.1.1`

- Fixed issue where a jobs list request returns unexpected results whenever a search query does not match any jobs. [#217](https://github.com/zowe/zowex/pull/217)

## `0.1.0`

- Added `zssh restore dataset` command. [#38](https://github.com/zowe/zowex/pull/38)
- Added `zssh view uss-file` command. [#38](https://github.com/zowe/zowex/pull/38)
- Added `zssh list spool-files` command. [#38](https://github.com/zowe/zowex/pull/38)
- Added support for cancelling jobs. [#138](https://github.com/zowe/zowex/pull/138)
- Added support for holding and releasing jobs. [#182](https://github.com/zowe/zowex/pull/182)
- Updated error handling for listing data sets. [#185](https://github.com/zowe/zowex/pull/185)
- Added `zssh submit uss-file|local-file|data-set` commands. [#184](https://github.com/zowe/zowex/pull/184)
- Added support for conflict detection through use of e-tags. When a data set or USS file is viewed, the e-tag is displayed to the user and can be passed for future write requests to prevent overwriting new changes on the target system. [#144](https://github.com/zowe/zowex/issues/144)

## [Unreleased]

- Initial release
