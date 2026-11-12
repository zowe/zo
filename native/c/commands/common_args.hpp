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

#ifndef COMMON_ARGS_HPP
#define COMMON_ARGS_HPP

#include "../parser.hpp"

namespace commands
{
namespace common
{
using namespace parser;

// Positional Arguments
const ArgTemplate DSN = {
    "dsn",
    make_aliases(),
    "data set name, optionally with member specified",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

const ArgTemplate DSN_PATTERN = {
    "dsn",
    make_aliases(),
    "data set name pattern",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

const ArgTemplate JOB_ID = {
    "jobid",
    make_aliases(),
    "valid jobid or job correlator",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

const ArgTemplate FILE_PATH = {
    "file-path",
    make_aliases(),
    "file path",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

// only for use when there's both a source and destination
const ArgTemplate FILE_PATH_SOURCE = {
    "source-path",
    make_aliases(),
    "source path",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

// only for use when there's both a source and destination
const ArgTemplate FILE_PATH_DEST = {
    "destination-path",
    make_aliases(),
    "destination path",
    ArgType_Single,
    true,
    ArgValue(),
    make_aliases()};

// Keyword Arguments
const ArgTemplate ENCODING = {
    "encoding",
    make_aliases("--encoding", "--ec"),
    "return contents in given encoding",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases(), false, true};

const ArgTemplate ENCODING_INPUT = {
    "encoding",
    make_aliases("--encoding", "--ec"),
    "convert input contents to given encoding",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases(), false, true};

const ArgTemplate LOCAL_ENCODING = {
    "local-encoding",
    make_aliases("--local-encoding", "--lec"),
    "source encoding of the data",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases(), false, true};

const ArgTemplate RESPONSE_FORMAT_CSV = {
    "response-format-csv",
    make_aliases("--response-format-csv", "--rfc"),
    "returns the response in CSV format",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

const ArgTemplate RESPONSE_FORMAT_BYTES = {
    "response-format-bytes",
    make_aliases("--response-format-bytes", "--rfb"),
    "returns the response as raw bytes",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

const ArgTemplate RESPONSE_FORMAT_HEADER = {
    "response-format-header",
    make_aliases("--response-format-header", "--rfh"),
    "includes a header row of column names in table and CSV output",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

const ArgTemplate ETAG = {
    "etag",
    make_aliases("--etag"),
    "Provide the e-tag for a write response to detect conflicts before save",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases()};

const ArgTemplate ETAG_ONLY = {
    "etag-only",
    make_aliases("--etag-only"),
    "Only print the e-tag for a write response (when successful)",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

const ArgTemplate RETURN_ETAG = {
    "return-etag",
    make_aliases("--return-etag"),
    "Display the e-tag for a read response in addition to data",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

const ArgTemplate PIPE_PATH = {
    "pipe-path",
    make_aliases("--pipe-path"),
    "Specify a FIFO pipe path for transferring binary data",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases(), true};

const ArgTemplate VOLSER = {
    "volser",
    make_aliases("--volser", "--vs"),
    "Specify volume serial where the data set resides",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases()};

const ArgTemplate MAX_ENTRIES = {
    "max-entries",
    make_aliases("--max-entries", "--me"),
    "max number of results to return before warning generated",
    ArgType_Single,
    false,
    ArgValue(),
    make_aliases()};

const ArgTemplate WARN = {
    "warn",
    make_aliases("--warn"),
    "warn if truncated or not found",
    ArgType_Flag,
    false,
    ArgValue(true),
    make_aliases()};

const ArgTemplate RECURSIVE = {
    "recursive",
    make_aliases("--recursive", "-r"),
    "Applies the operation recursively",
    ArgType_Flag,
    false,
    ArgValue(false),
    make_aliases()};

} // namespace common
} // namespace commands

#endif // COMMON_ARGS_HPP
