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

#ifndef JSON_OUTPUT_HPP
#define JSON_OUTPUT_HPP

#include "extend/plugin.hpp"
#include <string>

// zjson.hpp pulls in <hwtjic.h> and is therefore z/OS-only. It is included by
// json_output.cpp alone so that parser.hpp -- and the g++-built example in
// examples/native-cli -- stay free of that dependency.
namespace zjson
{
class Value;
}

namespace json_output
{
/**
 * @brief Convert a command handler's result object into a JSON value
 *
 * Shared by the CLI `--json` path and the JSON-RPC server so the two agree on
 * how an ast::Node maps onto JSON.
 */
zjson::Value ast_to_json(const ast::Node &node);

/**
 * @brief Everything the `--json` envelope needs from one command invocation
 */
struct Envelope
{
  /** Return code from the command handler, or 1 for a parser-level failure. */
  int exit_code = 0;
  /** Result of context.get_object(); may be null when a handler set nothing. */
  ast::Node data;
  /** Text the handler wrote to its output stream. */
  std::string captured_out;
  /** Text the handler wrote to its error stream. */
  std::string captured_err;
  /** Diagnostic for a parse error that never reached a handler. */
  std::string error_message;
  /** True when stdout carries the command's payload rather than decoration. */
  bool stdout_is_payload = false;
};

/**
 * @brief Serialize an envelope to a single compact line of JSON
 *
 * Never throws: a serialization failure yields a minimal hand-built envelope so
 * that `--json` always produces parseable output.
 */
std::string serialize(const Envelope &envelope);
} // namespace json_output

#endif // JSON_OUTPUT_HPP
