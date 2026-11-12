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

#include "rpcio.hpp"
#include "rpc_server.hpp"
#include <sstream>

using std::string;

MiddlewareContext::MiddlewareContext(const string &command_path, const plugin::ArgumentMap &args)
    : plugin::InvocationContext(plugin::ContextArgs(command_path, args, {}, &m_input_stream, &m_output_stream, &m_error_stream))
{
}

std::stringstream &MiddlewareContext::get_input_stream()
{
  return m_input_stream;
}

std::stringstream &MiddlewareContext::get_output_stream()
{
  return m_output_stream;
}

std::stringstream &MiddlewareContext::get_error_stream()
{
  return m_error_stream;
}

void MiddlewareContext::set_input_content(const string &content)
{
  m_input_stream.str(content);
  m_input_stream.clear(); // Clear any error flags
}

string MiddlewareContext::get_output_content() const
{
  return m_output_stream.str();
}

string MiddlewareContext::get_error_content() const
{
  return m_error_stream.str();
}

void MiddlewareContext::set_content_len(size_t content_length)
{
  plugin::Io::set_content_len(content_length);

  // If there's a pending notification, send it now with content length
  if (m_pending_notification)
  {
    // Add content length to the notification params
    if (m_pending_notification->params.has_value())
    {
      zjson::Value &params = m_pending_notification->params.value();
      params.add_to_object("contentLen", zjson::Value(static_cast<unsigned long long>(content_length)));
    }

    // Send the notification
    RpcServer::send_notification(*m_pending_notification);

    // Clean up the pending notification
    m_pending_notification.reset();
  }
}

void MiddlewareContext::set_heartbeat_callback(std::function<void()> callback)
{
  m_heartbeat_callback = std::move(callback);
}

void MiddlewareContext::update_heartbeat()
{
  if (m_heartbeat_callback)
  {
    m_heartbeat_callback();
  }
}

void MiddlewareContext::set_pending_notification(const RpcNotification &notification)
{
  m_pending_notification.reset(new RpcNotification(notification));
}

void MiddlewareContext::store_large_data(const string &field_name, const string &data)
{
  m_large_data[field_name] = data;
}
