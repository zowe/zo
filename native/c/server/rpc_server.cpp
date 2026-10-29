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

#include "rpc_server.hpp"
#include "rpcio.hpp"
#include "dispatcher.hpp"
#include "../json_output.hpp"
#include "../zlogger.hpp"
#include <iostream>

ZJSON_DERIVE(RpcNotification, jsonrpc, method, params);
ZJSON_DERIVE(RpcRequest, jsonrpc, method, params, id);
ZJSON_DERIVE(ErrorDetails, code, message, data);
ZJSON_SERIALIZABLE(RpcResponse,
                   ZJSON_FIELD(RpcResponse, jsonrpc),
                   ZJSON_FIELD(RpcResponse, result).skip_serializing_if_none(),
                   ZJSON_FIELD(RpcResponse, error).skip_serializing_if_none(),
                   ZJSON_FIELD(RpcResponse, id));

using std::string;

// Process error output to extract message and data
static void process_error_output(const string &error_output, string &out_message, string &out_data)
{
  // Find first newline and split the output into message and data
  size_t newline_pos = error_output.find('\n');

  if (newline_pos != string::npos)
  {
    out_message.assign(error_output, 0, newline_pos);
    out_data.assign(error_output, newline_pos + 1, string::npos);
  }
  else
  {
    out_message.assign(error_output);
    out_data.clear();
  }
}

void RpcServer::process_request(const string &request_data, std::function<void()> heartbeat_callback)
{
  try
  {
    // Parse the JSON request
    auto parse_result = zjson::from_str<RpcRequest>(request_data);

    if (!parse_result.has_value())
    {
      const string error_msg = parse_result.error().what();
      print_error(-1, RpcErrorCode::PARSE_ERROR, "Failed to parse command request", &error_msg);
      return;
    }

    RpcRequest request = parse_result.value();

    // A newly received ID supersedes any earlier request that timed out under it, so
    // clear it from the abandoned list. Guards against a client that restarts its ID
    // sequence while this server is still running, which would otherwise silently
    // discard a legitimate response.
    clear_abandoned(request.id);

    // Use CommandDispatcher singleton to handle the command
    CommandDispatcher &dispatcher = CommandDispatcher::get_instance();

    // Check if command is registered
    if (!dispatcher.has_command(request.method))
    {
      print_error(request.id, RpcErrorCode::METHOD_NOT_FOUND,
                  "Unrecognized command " + request.method);
      return;
    }

    // Validate params if a request validator is registered for this command
    if (request.params.has_value())
    {
      auto validation_result = validate_json_with_schema(request.method, request.params.value(), true);
      if (!validation_result.is_valid)
      {
        print_error(request.id, RpcErrorCode::INVALID_PARAMS, "Request validation failed (" + request.method + ")", &validation_result.error_message);
        return;
      }
    }

    // Convert JSON params to ArgumentMap
    plugin::ArgumentMap args;
    if (request.params.has_value())
    {
      args = convert_json_params_to_argument_map(request.params.value());
    }

    // Create MiddlewareContext for the command
    MiddlewareContext context(request.method, args);
    context.set_heartbeat_callback(std::move(heartbeat_callback));

    // Dispatch the command
    int result = dispatcher.dispatch(request.method, context);

    if (result != 0)
    {
      const string error_output = context.get_error_content();
      string error_message;
      string error_data;
      process_error_output(error_output, error_message, error_data);

      if (error_message.empty())
      {
        error_message = "Command execution failed (" + request.method + ")";
      }

      // A handler may attach a structured error object (e.g. the SAF/ESM return,
      // reason, and function codes from a failed certificate command) via
      // context.set_object(). When present, that structured payload must survive
      // to the JSON-RPC error's "data" field instead of being discarded in favor
      // of the plain-text stderr detail, so RPC/SDK consumers can inspect it
      // programmatically.
      const auto &err_object = context.get_object();
      if (err_object)
      {
        if (!error_data.empty() && err_object->is_object())
        {
          err_object->set("details", ast::str(error_data));
        }
        print_error(request.id, result, error_message, convert_ast_to_json(err_object));
        return;
      }

      const string *detail_ptr = error_data.empty() ? nullptr : &error_data;
      print_error(request.id, result, error_message, detail_ptr);
      return;
    }

    // Success - check if context has an object set, otherwise use output content
    zjson::Value result_json;

    const auto &ast_object = context.get_object();
    if (ast_object)
    {
      // Convert AST object to zjson::Value
      result_json = convert_ast_to_json(ast_object);
    }
    else
    {
      // Fallback to output content if no AST object is set
      string output = context.get_output_content();
      result_json = convert_output_to_json(output);
    }

    result_json.add_to_object("success", zjson::Value(context.get_error_content().empty()));

    // Validate result if a response validator is registered for this command
    auto validation_result = validate_json_with_schema(request.method, result_json, false);
    if (!validation_result.is_valid)
    {
      // Response validation failed - return internal error
      print_error(request.id, RpcErrorCode::INTERNAL_ERROR,
                  "Response validation failed (" + request.method + ")", &validation_result.error_message);
      return;
    }

    // Build success response
    RpcResponse response;
    response.jsonrpc = "2.0";
    response.result = std::optional<zjson::Value>(result_json);
    response.error = std::optional<ErrorDetails>();
    response.id = std::optional<int>(request.id);

    print_response(response, &context);
  }
  catch (const std::exception &e)
  {
    const string error_msg = e.what();
    print_error(-1, RpcErrorCode::PARSE_ERROR, "Failed to parse command request", &error_msg);
  }
}

RpcRequest RpcServer::parse_rpc_request(const zjson::Value &json)
{
  if (!json.is_object())
  {
    throw std::runtime_error("JSON-RPC request must be an object");
  }

  auto result = zjson::from_value<RpcRequest>(json);
  if (!result.has_value())
  {
    throw std::runtime_error(string("Failed to parse RPC request: ") + result.error().what());
  }
  return result.value();
}

string RpcServer::camel_case_to_kebab_case(const string &input)
{
  string result;
  result.reserve(input.length() + 5); // Reserve some extra space for hyphens

  for (size_t i = 0; i < input.length(); ++i)
  {
    char c = input[i];

    // If uppercase letter, convert to lowercase and prepend hyphen (unless it's the first character)
    if (std::isupper(c))
    {
      if (i > 0)
      {
        result += '-';
      }
      result += std::tolower(c);
    }
    else
    {
      result += c;
    }
  }

  return result;
}

plugin::ArgumentMap RpcServer::convert_json_params_to_argument_map(const zjson::Value &params)
{
  plugin::ArgumentMap args;

  if (!params.is_object())
  {
    throw std::runtime_error("Invalid parameters - must be an object");
  }

  // Convert JSON object to ArgumentMap
  for (const auto &pair : params.as_object())
  {
    // Convert camelCase keys to kebab-case
    const string kebab_key = camel_case_to_kebab_case(pair.first);
    const zjson::Value &value = pair.second;

    if (value.is_bool())
    {
      args[kebab_key] = plugin::Argument(value.as_bool());
    }
    else if (value.is_integer())
    {
      args[kebab_key] = plugin::Argument(value.as_int64());
    }
    else if (value.is_double())
    {
      args[kebab_key] = plugin::Argument(value.as_double());
    }
    else if (value.is_string())
    {
      args[kebab_key] = plugin::Argument(value.as_string());
    }
    // For other types (null, array, object), convert to string representation
    else
    {
      auto str_result = zjson::to_string(value);
      args[kebab_key] = plugin::Argument(str_result.value_or(""));
    }
  }

  return args;
}

zjson::Value RpcServer::convert_output_to_json(const string &output)
{
  if (!output.empty())
  {
    // Try to parse output as JSON, fallback to string
    auto parse_result = zjson::from_str<zjson::Value>(output);
    if (parse_result.has_value())
    {
      return parse_result.value();
    }
    else
    {
      return zjson::Value(output);
    }
  }
  else
  {
    return zjson::Value::create_object();
  }
}

zjson::Value RpcServer::convert_ast_to_json(const ast::Node &ast_node)
{
  // Shared with the CLI's --json path so both agree on the ast -> JSON mapping.
  return json_output::ast_to_json(ast_node);
}

void RpcServer::print_response(const RpcResponse &response, MiddlewareContext *context)
{
  // A detached worker eventually finishes the request it was timed out on. The
  // client already failed that ID, so emitting this would only surface a bogus
  // "missing promise for response ID" error on the other end.
  if (response.id.has_value() && is_abandoned(response.id.value()))
  {
    LOG_WARN("Discarding late response for abandoned request ID %d", response.id.value());
    return;
  }

  // Log errors to the log file
  if (response.error.has_value())
  {
    const ErrorDetails &error = response.error.value();
    if (error.data.has_value())
    {
      const auto &val = error.data.value();
      string data_str = val.is_string() ? val.as_string() : serialize_json(val);
      LOG_ERROR("%s: %s", error.message.c_str(), data_str.c_str());
    }
    else
    {
      LOG_ERROR("%s", error.message.c_str());
    }
  }

  string json_string = serialize_json(rpc_response_to_json(response));

  // Replace placeholders with actual large data
  if (context && context->get_large_data().size() > 0)
  {
    auto &large_data_map = context->get_large_data();
    for (auto it = large_data_map.begin(); it != large_data_map.end();)
    {
      add_large_data_to_json(json_string, it->first, it->second);
      it = large_data_map.erase(it);
    }
  }

  auto &stream = response.error.has_value() ? std::cerr : std::cout;
  std::lock_guard<std::mutex> lock(output_mutex());
  stream << json_string << std::endl;
}

void RpcServer::add_large_data_to_json(string &json_string, const string &field_name, const string &data)
{
  // Find the field in JSON: "fieldName":""
  const string search_pattern = "\"" + field_name + "\":\"\"";
  size_t pos = json_string.find(search_pattern);

  if (pos == string::npos)
  {
    LOG_ERROR("Could not find empty field '%s' in JSON for large data replacement", field_name.c_str());
    return;
  }

  // Assume that data is Base64 encoded so no JSON escaping needed
  const string replacement = "\"" + field_name + "\":\"" + data + "\"";
  json_string.replace(pos, search_pattern.length(), replacement);

  LOG_DEBUG("Replaced empty field '%s' with large data (%zu bytes)", field_name.c_str(), data.size());
}

// Static utility methods
string RpcServer::serialize_json(const zjson::Value &val, bool prettify)
{
  auto result = prettify ? zjson::to_string_pretty(val) : zjson::to_string(val);
  if (!result.has_value())
  {
    throw std::runtime_error(string("Failed to serialize JSON: ") + result.error().what());
  }
  return result.value();
}

RpcRequest RpcServer::parse_rpc_request_from_json(const zjson::Value &json)
{
  if (!json.is_object())
  {
    throw std::runtime_error("JSON-RPC request must be an object");
  }

  auto result = zjson::from_value<RpcRequest>(json);
  if (!result.has_value())
  {
    throw std::runtime_error(string("Failed to parse RPC request: ") + result.error().what());
  }
  return result.value();
}

zjson::Value RpcServer::rpc_response_to_json(const RpcResponse &response)
{
  auto result = zjson::to_value(response);
  return result.value_or(zjson::Value::create_object());
}

zjson::Value RpcServer::error_details_to_json(const ErrorDetails &error)
{
  auto result = zjson::to_value(error);
  return result.value_or(zjson::Value::create_object());
}

std::mutex &RpcServer::output_mutex()
{
  static std::mutex mutex;
  return mutex;
}

std::mutex &RpcServer::abandoned_mutex()
{
  static std::mutex mutex;
  return mutex;
}

std::set<int> &RpcServer::abandoned_ids()
{
  static std::set<int> ids;
  return ids;
}

std::deque<int> &RpcServer::abandoned_order()
{
  static std::deque<int> order;
  return order;
}

bool RpcServer::is_abandoned(int request_id)
{
  if (request_id < 0)
    return false;

  std::lock_guard<std::mutex> lock(abandoned_mutex());
  return abandoned_ids().count(request_id) > 0;
}

void RpcServer::clear_abandoned(int request_id)
{
  if (request_id < 0)
    return;

  std::lock_guard<std::mutex> lock(abandoned_mutex());
  if (abandoned_ids().erase(request_id) == 0)
    return;

  for (auto it = abandoned_order().begin(); it != abandoned_order().end(); ++it)
  {
    if (*it == request_id)
    {
      abandoned_order().erase(it);
      break;
    }
  }
}

void RpcServer::send_notification(const RpcNotification &notification)
{
  string json_string = serialize_json(zjson::to_value(notification).value());
  std::lock_guard<std::mutex> lock(output_mutex());
  std::cout << json_string << std::endl;
}

void RpcServer::send_timeout_error(const string &request_data, int64_t timeout_ms)
{
  int request_id = -1;
  string method = "unknown";

  // Try to parse the request to extract the ID
  try
  {
    auto parse_result = zjson::from_str<zjson::Value>(request_data);
    if (parse_result.has_value())
    {
      const zjson::Value &json = parse_result.value();
      if (json.is_object())
      {
        // Extract request ID if present
        const zjson::Value &id_value = json["id"];
        if (!id_value.is_null() && id_value.is_integer())
        {
          request_id = static_cast<int>(id_value.as_int64());
        }

        // Extract method name for better error messages
        const zjson::Value &method_value = json["method"];
        if (!method_value.is_null() && method_value.is_string())
        {
          method = method_value.as_string();
        }
      }
    }
  }
  catch (const zjson::Error &e)
  {
    LOG_ERROR("Failed to parse timed-out request for error response: %s", e.what());
  }

  // Build timeout error message
  const auto timeout_message = "Request timed out after " + std::to_string(timeout_ms) + " ms (method: " + method + ")";

  // Send the error response
  print_error(request_id, RpcErrorCode::REQUEST_TIMEOUT, timeout_message);

  // Mark the ID afterwards, so the timeout error itself is not filtered. The
  // detached worker is still inside its stuck operation, so losing this race would
  // at worst restore the old duplicate-response behaviour rather than swallow the
  // error the client is waiting on.
  if (request_id >= 0)
  {
    std::lock_guard<std::mutex> lock(abandoned_mutex());
    if (abandoned_ids().insert(request_id).second)
    {
      abandoned_order().push_back(request_id);
      while (abandoned_order().size() > kMaxAbandonedIds)
      {
        abandoned_ids().erase(abandoned_order().front());
        abandoned_order().pop_front();
      }
    }
  }

  LOG_WARN("Sent timeout error response for request ID %d (method: %s)", request_id, method.c_str());
}

void RpcServer::print_error(int request_id, int code, const string &message, const string *data)
{
  std::optional<zjson::Value> error_data;
  if (data != nullptr)
  {
    error_data = zjson::Value(*data);
  }

  ErrorDetails error{code, message, error_data};

  RpcResponse response;
  response.jsonrpc = "2.0";
  response.result = std::optional<zjson::Value>();
  response.error = std::optional<ErrorDetails>(error);
  // Use -1 as sentinel for null ID (per JSON-RPC spec for parse errors)
  response.id = (request_id == -1) ? std::optional<int>() : std::optional<int>(request_id);

  print_response(response);
}

void RpcServer::print_error(int request_id, int code, const string &message, const zjson::Value &data)
{
  ErrorDetails error{code, message, std::optional<zjson::Value>(data)};

  RpcResponse response;
  response.jsonrpc = "2.0";
  response.result = std::optional<zjson::Value>();
  response.error = std::optional<ErrorDetails>(error);
  // Use -1 as sentinel for null ID (per JSON-RPC spec for parse errors)
  response.id = (request_id == -1) ? std::optional<int>() : std::optional<int>(request_id);

  print_response(response);
}

validator::ValidationResult RpcServer::validate_json_with_schema(const string &method, const zjson::Value &data, bool is_request)
{
  const auto &dispatcher = CommandDispatcher::get_instance();

  const auto &builders = dispatcher.get_builders();
  const auto &it = builders.find(method);
  if (it == builders.end())
  {
    return validator::ValidationResult::success();
  }

  const CommandBuilder &builder = it->second;
  const auto &schema = is_request ? builder.get_request_schema() : builder.get_response_schema();

  if (!schema)
  {
    return validator::ValidationResult::success();
  }

  return validator::validate_schema(data, schema->fields, schema->field_count, is_request);
}
