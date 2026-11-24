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

#include "json_output.hpp"
#include "zbase64.h"
#include "zjson.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace json_output
{
namespace
{
/**
 * Cap on the payload copied into the envelope. The JSON-RPC server diverts
 * anything past LARGE_DATA_THRESHOLD out of the document entirely; the CLI has
 * no such channel, and would otherwise hold the raw text, the escaped copy and
 * the serializer's copy at once.
 */
const size_t MAX_PAYLOAD_BYTES = 8u * 1024u * 1024u;

/**
 * @brief Whether text survives JSON string escaping unchanged
 *
 * zjson::escape_json_string maps every control byte it does not have an escape
 * for onto U+FFFD, deliberately: IBM-1047 control bytes do not share Unicode
 * code points, so emitting \\u00XX would misreport them. That is fine for
 * diagnostics but silently corrupts binary payloads, which are base64-encoded
 * instead.
 */
bool is_json_safe_text(const std::string &text)
{
  return std::all_of(text.begin(), text.end(), [](char ch)
  {
    if (ch == '\b' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t')
    {
      return true;
    }
    return !iscntrl(static_cast<unsigned char>(ch)); });
}

struct Payload
{
  std::string text;
  bool present = false;
  bool encoded = false;
  bool truncated = false;
};

/**
 * @brief Decide what becomes of the text a handler wrote to stdout
 *
 * Three cases: a command whose payload *is* stdout gets it verbatim; a handler
 * that produced no fields of its own gets it too, so no command emits an empty
 * payload; anything else already represents that text as fields, so it is
 * dropped rather than duplicated.
 */
Payload resolve_payload(const Envelope &envelope, const zjson::Value &data)
{
  Payload payload;
  if (envelope.captured_out.empty())
  {
    return payload;
  }

  const zjson::ObjectMap &fields = data.as_object();
  if (fields.find("data") != fields.end())
  {
    return payload;
  }

  if (!envelope.stdout_is_payload && !fields.empty())
  {
    return payload;
  }

  payload.present = true;
  if (envelope.captured_out.size() > MAX_PAYLOAD_BYTES)
  {
    // Copy only what will be kept; a `ds view` of a very large member would
    // otherwise duplicate the whole thing just to discard most of it.
    payload.text.assign(envelope.captured_out, 0, MAX_PAYLOAD_BYTES);
    payload.truncated = true;
  }
  else
  {
    payload.text = envelope.captured_out;
  }
  if (!is_json_safe_text(payload.text))
  {
    payload.text = zbase64::encode(payload.text);
    payload.encoded = true;
  }
  return payload;
}

/**
 * @brief The handler's result object, always shaped as a JSON object
 *
 * zjson refuses to serialize a primitive root, and consumers expect `data` to
 * be an object, so a handler that set an array or a scalar has it nested under
 * `value`. A handler that set nothing at all yields `{}`.
 */
zjson::Value build_data(const ast::Node &node)
{
  if (!node)
  {
    return zjson::Value::create_object();
  }

  zjson::Value data = ast_to_json(node);
  if (data.is_object())
  {
    return data;
  }

  zjson::Value wrapped = zjson::Value::create_object();
  wrapped.add_to_object("value", data);
  return wrapped;
}

/**
 * @brief Diagnostics for the envelope
 *
 * A handler's stderr is captured; a parser-level failure never reaches a
 * handler and carries its message on the envelope instead.
 */
const std::string &diagnostics(const Envelope &envelope)
{
  return envelope.captured_err.empty() ? envelope.error_message : envelope.captured_err;
}

/**
 * @brief Minimal envelope for when serialization itself fails
 *
 * Hand-built so that `--json` still emits parseable JSON on stdout.
 */
std::string fallback(const Envelope &envelope)
{
  std::stringstream out;
  out << "{\"success\":" << (envelope.exit_code == 0 ? "true" : "false")
      << ",\"exitCode\":" << envelope.exit_code
      << ",\"data\":{}"
      << R"(,"stderr":"")"
      << R"(,"jsonError":"failed to serialize command result"})";
  return out.str();
}
} // namespace

zjson::Value ast_to_json(const ast::Node &node)
{
  if (!node)
  {
    return zjson::Value(); // null
  }

  switch (node->kind())
  {
  case ast::Ast::Null:
    return zjson::Value(); // null

  case ast::Ast::Boolean:
    return zjson::Value(node->as_bool());

  case ast::Ast::Integer:
    return zjson::Value(node->as_integer());

  case ast::Ast::Number:
    return zjson::Value(node->as_number());

  case ast::Ast::String:
    return zjson::Value(node->as_string());

  case ast::Ast::Array:
  {
    zjson::Value array_value = zjson::Value::create_array();
    const std::vector<ast::Node> &items = node->as_array();
    array_value.reserve_array(items.size());

    for (const ast::Node &item : items)
    {
      array_value.add_to_array(ast_to_json(item));
    }
    return array_value;
  }

  case ast::Ast::Object:
  {
    zjson::Value object_value = zjson::Value::create_object();
    const ast::ObjMap &fields = node->as_object();

    for (const auto &field : fields)
    {
      object_value.add_to_object(field.first, ast_to_json(field.second));
    }
    return object_value;
  }

  default:
    return zjson::Value(); // null for unknown types
  }
}

std::string serialize(const Envelope &envelope)
{
  try
  {
    zjson::Value data = build_data(envelope.data);
    const Payload payload = resolve_payload(envelope, data);
    if (payload.present)
    {
      data.add_to_object("data", zjson::Value(payload.text));
    }

    zjson::Value root = zjson::Value::create_object();
    root.add_to_object("success", zjson::Value(envelope.exit_code == 0));
    root.add_to_object("exitCode", zjson::Value(static_cast<long long>(envelope.exit_code)));
    root.add_to_object("data", data);
    root.add_to_object("stderr", zjson::Value(diagnostics(envelope)));
    if (payload.encoded)
    {
      root.add_to_object("encoding", zjson::Value(std::string("base64")));
    }
    if (payload.truncated)
    {
      root.add_to_object("truncated", zjson::Value(true));
    }

    zstd::expected<std::string, zjson::Error> serialized = zjson::to_string(root);
    if (serialized.has_value())
    {
      return serialized.value();
    }
  }
  catch (const zjson::Error &e)
  {
    // Serialization failed; report why, then fall through to the hand-built envelope below.
    std::cerr << "json_output: " << e.what() << std::endl;
  }

  return fallback(envelope);
}
} // namespace json_output
