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

#ifndef RPC_SERVER_HPP
#define RPC_SERVER_HPP

#include <string>
#include <mutex>
#include <deque>
#include <set>
#include <functional>
#include "../extend/plugin.hpp"
#include "../singleton.hpp"

// Forward declarations
namespace zjson
{
class Value;
}
namespace validator
{
struct ValidationResult;
}
class MiddlewareContext;
struct RpcRequest;
struct RpcResponse;
struct RpcNotification;
struct ErrorDetails;

/**
 * Thread-safe singleton RPC server that handles JSON-RPC request parsing,
 * command execution, and response serialization.
 */
class RpcServer : public Singleton<RpcServer>
{
  friend class Singleton<RpcServer>;

private:
  /**
   * @brief Guards every write to stdout/stderr on the wire
   *
   * Static because send_notification() is static; notifications and responses
   * share one stream, and an unsynchronized write can interleave mid-line and
   * hand the client invalid JSON.
   */
  static std::mutex &output_mutex();

  /**
   * @brief IDs whose response was already answered with a timeout error
   *
   * A detached worker keeps running and will eventually emit its own response for
   * the same ID. The client has already failed that request, so a second response
   * only produces a spurious "missing promise" error. Bounded ring of recent IDs.
   */
  static std::mutex &abandoned_mutex();
  static std::set<int> &abandoned_ids();
  static std::deque<int> &abandoned_order();
  static bool is_abandoned(int request_id);
  static void clear_abandoned(int request_id);

  static constexpr size_t kMaxAbandonedIds = 64;

  // Private constructor for singleton
  RpcServer() = default;

  // Helper methods for JSON processing
  RpcRequest parse_rpc_request(const zjson::Value &json);
  plugin::ArgumentMap convert_json_params_to_argument_map(const zjson::Value &params);
  zjson::Value convert_output_to_json(const std::string &output);
  zjson::Value convert_ast_to_json(const ast::Node &ast_node);
  void print_response(const RpcResponse &response, MiddlewareContext *context = nullptr);
  void print_error(int request_id, int code, const std::string &message, const std::string *data = nullptr);
  void print_error(int request_id, int code, const std::string &message, const zjson::Value &data);
  validator::ValidationResult validate_json_with_schema(const std::string &method, const zjson::Value &params, bool is_request);
  void add_large_data_to_json(std::string &json_string, const std::string &field_name, const std::string &data);

public:
  /**
   * Convert camelCase string to kebab-case
   * @param input The camelCase string to convert
   * @return The kebab-case version of the string
   */
  static std::string camel_case_to_kebab_case(const std::string &input);
  /**
   * Process a JSON-RPC request string and return the response
   * This method is thread-safe and handles all JSON parsing, command execution,
   * and response serialization
   * @param request_data The raw JSON-RPC request string
   * @param heartbeat_callback Callback a streaming command handler can invoke per chunk
   *        (e.g. during a large file upload/download) to signal that the request is
   *        still making progress. Set on the MiddlewareContext built for this request.
   */
  void process_request(const std::string &request_data, std::function<void()> heartbeat_callback = nullptr);

  /**
   * Utility function to serialize JSON with error handling
   * @param val The JSON value to serialize
   * @param prettify Whether to format the output with indentation
   * @return Serialized JSON string
   */
  static std::string serialize_json(const zjson::Value &val, bool prettify = false);

  /**
   * Helper function for parsing RPC requests from JSON
   * @param json The JSON value containing the RPC request
   * @return Parsed RpcRequest structure
   */
  static RpcRequest parse_rpc_request_from_json(const zjson::Value &json);

  /**
   * Helper function to convert RPC response to JSON
   * @param response The RpcResponse structure
   * @return JSON representation of the response
   */
  static zjson::Value rpc_response_to_json(const RpcResponse &response);

  /**
   * Helper function to convert error details to JSON
   * @param error The ErrorDetails structure
   * @return JSON representation of the error
   */
  static zjson::Value error_details_to_json(const ErrorDetails &error);

  /**
   * Helper function to send an RPC notification
   * Serializes the notification and outputs it to stdout
   * @param notification The RpcNotification to send
   */
  static void send_notification(const RpcNotification &notification);

  /**
   * Send a timeout error response for a request that exceeded the timeout limit
   * This method parses the request to extract the ID and sends an appropriate error response
   * @param request_data The raw JSON-RPC request string that timed out
   * @param timeout_ms The timeout value that was exceeded (in milliseconds)
   */
  void send_timeout_error(const std::string &request_data, int64_t timeout_ms);
};

#endif
