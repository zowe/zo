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

#ifndef RPCIO_HPP
#define RPCIO_HPP

#include "../extend/plugin.hpp"
#include "../zjson.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

// JSON-RPC 2.0 Standard Error Codes
namespace RpcErrorCode
{
enum
{
  PARSE_ERROR = -32700,      // Invalid JSON was received
  INVALID_REQUEST = -32600,  // The JSON sent is not a valid Request object
  METHOD_NOT_FOUND = -32601, // The method does not exist / is not available
  INVALID_PARAMS = -32602,   // Invalid method parameter(s)
  INTERNAL_ERROR = -32603,   // Internal JSON-RPC error
  // -32000 to -32099 are reserved for implementation-defined server-errors
  REQUEST_TIMEOUT = -32001 // Request exceeded timeout limit
};
}

struct RpcNotification
{
  std::string jsonrpc;
  std::string method;
  std::optional<zjson::Value> params;
};

struct RpcRequest : RpcNotification
{
  int id;
};

struct ErrorDetails
{
  int code;
  std::string message;
  std::optional<zjson::Value> data;
};

struct RpcResponse
{
  std::string jsonrpc;
  std::optional<zjson::Value> result;
  std::optional<ErrorDetails> error;
  std::optional<int> id;
};

static constexpr size_t LARGE_DATA_THRESHOLD = 16 * 1024 * 1024; // 16MB

class MiddlewareContext : public plugin::InvocationContext
{
public:
  MiddlewareContext(const std::string &command_path, const plugin::ArgumentMap &args);

  // Get access to the string streams for reading/writing content
  std::stringstream &get_input_stream();
  std::stringstream &get_output_stream();
  std::stringstream &get_error_stream();

  // Helper methods to set input content and get output/error content
  void set_input_content(const std::string &content);
  std::string get_output_content() const;
  std::string get_error_content() const;

  // Provide mutable access to arguments for transforms
  plugin::ArgumentMap &mutable_arguments()
  {
    return m_args;
  }

  // Set content length and send pending notification if present
  void set_content_len(size_t content_length) override;

  // Set the callback invoked by update_heartbeat(). Passed in by RpcServer::process_request()
  // for this one request, which got it from the worker thread servicing the request (see
  // Worker::process_request) - not stored per-thread, since a fresh MiddlewareContext is
  // constructed for every request.
  void set_heartbeat_callback(std::function<void()> callback);

  // Invoke the callback set via set_heartbeat_callback(), if any
  void update_heartbeat() override;

  // Store pending notification for delayed sending
  void set_pending_notification(const RpcNotification &notification);

  // Get large data map
  std::map<std::string, std::string> &get_large_data()
  {
    return m_large_data;
  }

  // Data is spliced verbatim between quotes into the serialized response, so it must
  // already be safe as JSON string content (base64 or zjson-escaped).
  void store_large_data(const std::string &field_name, const std::string &data);

private:
  std::stringstream m_input_stream;
  std::stringstream m_output_stream;
  std::stringstream m_error_stream;
  std::unique_ptr<RpcNotification> m_pending_notification;
  std::map<std::string, std::string> m_large_data;
  std::function<void()> m_heartbeat_callback;
};

#endif
