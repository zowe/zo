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

#include "builder.hpp"
#include "../zlogger.hpp"
#include "rpcio.hpp"
#include "rpc_server.hpp"
#include "../zbase64.h"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using std::string;

CommandBuilder::CommandBuilder(CommandHandler handler)
    : handler_(handler), transforms_()
{
}

CommandBuilder &CommandBuilder::rename_arg(const string &from, const string &to)
{
  // Validate that input uses camelCase (not kebab-case)
  if (from.find('-') != string::npos)
  {
    string errMsg = string("rename_arg configuration error: '") + from +
                    "' contains hyphens. Use camelCase instead.";
    std::cerr << errMsg << std::endl;
    LOG_ERROR("%s", errMsg.c_str());
  }

  transforms_.push_back(ArgTransform(ArgTransform::RenameArg, from, to));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, const char *default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(default_value)));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, const string &default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(default_value)));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, bool default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(default_value)));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, int default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(static_cast<long long>(default_value))));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, long long default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(default_value)));
  return *this;
}

CommandBuilder &CommandBuilder::set_default(const string &arg_name, double default_value)
{
  transforms_.push_back(ArgTransform(ArgTransform::SetDefault, arg_name, plugin::Argument(default_value)));
  return *this;
}

CommandBuilder &CommandBuilder::handle_fifo(const string &rpc_id_param, const string &arg_name, FifoMode mode, bool defer)
{
  transforms_.push_back(ArgTransform(ArgTransform::HandleFifo, rpc_id_param, arg_name, mode, defer));
  return *this;
}

CommandBuilder &CommandBuilder::read_stdout(const string &arg_name, bool b64_encode)
{
  transforms_.push_back(ArgTransform(ArgTransform::ReadStdout, arg_name, b64_encode));
  return *this;
}

CommandBuilder &CommandBuilder::write_stdin(const string &arg_name, bool b64_decode)
{
  transforms_.push_back(ArgTransform(ArgTransform::WriteStdin, arg_name, b64_decode));
  return *this;
}

CommandBuilder &CommandBuilder::flatten_obj(const string &arg_name)
{
  transforms_.push_back(ArgTransform(ArgTransform::FlattenObj, arg_name));
  return *this;
}

void CommandBuilder::apply_input_transforms(MiddlewareContext &context) const
{
  auto &args = context.mutable_arguments();

  for (const auto &transform : transforms_)
  {
    // Find the argument
    auto arg_it = args.find(transform.arg_name);

    switch (transform.kind)
    {
    case ArgTransform::RenameArg:
    {
      // Rename: argument must exist as camelCase
      string kebab_name = RpcServer::camel_case_to_kebab_case(transform.arg_name);
      arg_it = args.find(kebab_name);

      if (arg_it == args.end())
      {
        LOG_DEBUG("Argument '%s' not found for rename transform, skipping", transform.arg_name.c_str());
        continue;
      }

      plugin::Argument value = arg_it->second;
      args.erase(arg_it);
      args[transform.renamed_to] = value;
      break;
    }

    case ArgTransform::SetDefault:
    {
      // Set default value if argument doesn't exist
      if (arg_it == args.end())
      {
        args[transform.arg_name] = transform.default_value;
      }
      break;
    }

    case ArgTransform::WriteStdin:
    {
      // WriteStdin: Read argument value and write to stdin
      // If base64 is true, decode base64 before writing to stdin
      if (arg_it != args.end())
      {
        try
        {
          string data = arg_it->second.get_string_value();

          // Decode base64 if requested
          if (transform.base64)
          {
            data = zbase64::decode(data);
          }

          // Write to stdin
          context.set_input_content(data);

          // Remove the argument from args
          args.erase(arg_it);
        }
        catch (const std::exception &e)
        {
          string errMsg = string("Failed to process WriteStdin transform: ") + e.what();
          context.errln(errMsg.c_str());
          LOG_ERROR("%s", errMsg.c_str());
        }
      }
      break;
    }

    case ArgTransform::FlattenObj:
    {
      // FlattenObj: Flatten a JSON object argument into the argument map
      if (arg_it != args.end())
      {
        try
        {
          // Parse the JSON string into a zjson::Value
          const auto parse_result = zjson::from_str<zjson::Value>(arg_it->second.get_string_value());
          if (!parse_result.has_value())
          {
            string errMsg = string("Failed to parse JSON for FlattenObj transform: ") + parse_result.error().what();
            context.errln(errMsg.c_str());
            LOG_ERROR("%s", errMsg.c_str());
            break;
          }

          const auto &jsonValue = parse_result.value();

          // Verify it's an object
          if (!jsonValue.is_object())
          {
            context.errln("FlattenObj transform requires a JSON object");
            LOG_ERROR("FlattenObj transform requires a JSON object");
            break;
          }

          // Add each property from the object to the argument map
          for (const auto &property : jsonValue.as_object())
          {
            const auto &key = property.first;
            const auto &value = property.second;

            // Convert zjson::Value to plugin::Argument
            if (value.is_bool())
            {
              args[key] = plugin::Argument(value.as_bool());
            }
            else if (value.is_integer())
            {
              args[key] = plugin::Argument(value.as_int64());
            }
            else if (value.is_double())
            {
              args[key] = plugin::Argument(value.as_double());
            }
            else if (value.is_string())
            {
              args[key] = plugin::Argument(value.as_string());
            }
          }

          // Remove the original object argument
          args.erase(arg_it);
        }
        catch (const std::exception &e)
        {
          string errMsg = string("Failed to process FlattenObj transform: ") + e.what();
          context.errln(errMsg.c_str());
          LOG_ERROR("%s", errMsg.c_str());
        }
      }
      break;
    }

    case ArgTransform::HandleFifo:
    {
      // HandleFifo: Create FIFO pipe and send appropriate notification
      // Find the RPC ID argument
      const auto rpc_id_arg = args.find(transform.rpc_id_param);
      if (rpc_id_arg != args.end())
      {
        try
        {
          // Get stream_id from the argument
          const long long *stream_id_ptr = rpc_id_arg->second.get_int();
          if (stream_id_ptr == nullptr)
          {
            context.errln("HandleFifo: RPC ID argument is not an integer");
            LOG_ERROR("HandleFifo: RPC ID argument is not an integer");
            break;
          }
          long long stream_id = *stream_id_ptr;

          // Create pipe path: /tmp/zowex_{uid}_{pid}_{stream_id}_fifo
          const char *tmp_dir = std::getenv("TMPDIR");
          if (tmp_dir == nullptr || tmp_dir[0] == '\0')
          {
            tmp_dir = "/tmp";
          }

          std::ostringstream pipe_path_stream;
          pipe_path_stream << tmp_dir << "/zowex_"
                           << geteuid() << "_"
                           << getpid() << "_"
                           << stream_id << "_fifo";
          const string pipe_path = pipe_path_stream.str();

          // Remove any existing pipe (ignore errors if it doesn't exist)
          if (unlink(pipe_path.c_str()) != 0 && errno != ENOENT)
          {
            string errMsg = string("Failed to delete existing FIFO pipe: ") + pipe_path + ", errno: " + std::to_string(errno);
            context.errln(errMsg.c_str());
            LOG_ERROR("%s", errMsg.c_str());
            break;
          }

          // Create the FIFO pipe
          if (mkfifo(pipe_path.c_str(), 0600) != 0)
          {
            string errMsg = string("Failed to create FIFO pipe: ") + pipe_path + ", errno: " + std::to_string(errno);
            context.errln(errMsg.c_str());
            LOG_ERROR("%s", errMsg.c_str());
            break;
          }

          LOG_DEBUG("Created FIFO pipe: %s", pipe_path.c_str());

          // Set the pipe path as the output argument
          args[transform.arg_name] = plugin::Argument(pipe_path);

          // Create notification based on mode
          zjson::Value params_obj = zjson::Value::create_object();
          params_obj.add_to_object("id", zjson::Value(static_cast<int>(stream_id)));
          params_obj.add_to_object("pipePath", zjson::Value(pipe_path));

          RpcNotification notification = RpcNotification{
              .jsonrpc = "2.0",
              .method = (transform.fifo_mode == FifoMode::GET) ? "receiveStream" : "sendStream",
              .params = std::optional<zjson::Value>(params_obj),
          };

          // If defer is true, store the notification for later
          // Otherwise, send it immediately
          if (transform.defer)
          {
            context.set_pending_notification(notification);
          }
          else
          {
            RpcServer::send_notification(notification);
          }
        }
        catch (const std::exception &e)
        {
          string errMsg = string("Failed to process HandleFifo transform: ") + e.what();
          context.errln(errMsg.c_str());
          LOG_ERROR("%s", errMsg.c_str());
        }
      }
      break;
    }

    default:
      // Not an input transform, skip
      break;
    }
  }
}

void CommandBuilder::apply_output_transforms(MiddlewareContext &context) const
{
  auto obj = context.get_object();

  // If no object is set, create one
  if (!obj)
  {
    obj = ast::obj();
    context.set_object(obj);
  }

  // Only process objects (not arrays or primitives)
  if (!obj->is_object())
  {
    return;
  }

  for (const auto &transform : transforms_)
  {
    // Get the field value from the object if it exists
    auto field_value = obj->get(transform.arg_name);

    switch (transform.kind)
    {
    case ArgTransform::ReadStdout:
    {
      // ReadStdout: Read from stdout and write to output argument
      // If base64 is true, encode base64 before writing to output
      try
      {
        string data = context.get_output_content();

        if (transform.base64)
        {
          data = zbase64::encode(data);
        }

        if (data.size() >= LARGE_DATA_THRESHOLD)
        {
          context.store_large_data(transform.arg_name, std::move(data));
          obj->set(transform.arg_name, ast::str(""));

          // Clear output buffer to free memory ASAP
          std::stringstream().swap(context.get_output_stream());
        }
        else
        {
          obj->set(transform.arg_name, ast::str(data));
        }
      }
      catch (const std::exception &e)
      {
        string errMsg = string("Failed to process ReadStdout transform: ") + e.what();
        context.errln(errMsg.c_str());
        LOG_ERROR("%s", errMsg.c_str());
      }
      break;
    }

    case ArgTransform::HandleFifo:
    {
      // HandleFifo cleanup: Remove the FIFO pipe after command execution
      const auto &args = context.arguments();
      const auto pipe_path_arg = args.find(transform.arg_name);

      if (pipe_path_arg != args.end())
      {
        // Remove the pipe (ignore errors if already removed)
        const string pipe_path = pipe_path_arg->second.get_string_value();
        if (unlink(pipe_path.c_str()) == 0)
        {
          LOG_DEBUG("Cleaned up FIFO pipe: %s", pipe_path.c_str());
        }
        else if (errno != ENOENT)
        {
          LOG_ERROR("Failed to delete FIFO pipe: %s, errno: %d", pipe_path.c_str(), errno);
        }
      }
      break;
    }

    default:
      // Not an output transform, skip
      break;
    }
  }
}
