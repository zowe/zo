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

#include "zo_rpc_c_api.h"
#include "../../c/server/dispatcher.hpp"
#include "../../c/server/rpc_commands.hpp"
#include "../../c/server/rpc_server.hpp"
#include "../../c/server/rpcio.hpp"
#include "../../c/zjson.hpp"
#include "../../c/zlogger.hpp"
#include "../../python/bindings/conversion.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace
{

enum class Lane
{
  // Handler is safe to run concurrently with other requests in the same address space
  Concurrent,
  // Handler allocates fixed DD names (shared per address space), so run one at a time
  Serialized
};

// Methods reachable from the JVM. Excluded on purpose:
// - FIFO-streaming content methods (readDataset/writeDataset/readFile/writeFile): typed FFM shims
// - methods that spawn child processes or need APF (tsoCommand, unixCommand, consoleCommand,
//   submitUss): child identity under thread-level security is unverified
std::map<std::string, Lane> &allowed_methods()
{
  static std::map<std::string, Lane> methods = {
      // Data sets
      {"listDatasets", Lane::Concurrent},
      {"listDsMembers", Lane::Concurrent},
      {"createDataset", Lane::Concurrent},
      {"createMember", Lane::Concurrent},
      {"deleteDataset", Lane::Concurrent},
      {"deleteVsam", Lane::Concurrent},
      {"renameDataset", Lane::Concurrent},
      {"renameMember", Lane::Concurrent},
      {"copyDatasetOrMember", Lane::Concurrent},
      {"resolveDsAlias", Lane::Concurrent},
      {"restoreDataset", Lane::Concurrent},
      // Jobs
      {"listJobs", Lane::Concurrent},
      {"getJobStatus", Lane::Concurrent},
      {"listSpools", Lane::Concurrent},
      {"readSpool", Lane::Concurrent},
      {"getJcl", Lane::Concurrent},
      {"submitJcl", Lane::Concurrent},
      {"submitJob", Lane::Concurrent},
      {"cancelJob", Lane::Concurrent},
      {"deleteJob", Lane::Concurrent},
      {"holdJob", Lane::Concurrent},
      {"releaseJob", Lane::Concurrent},
      // USS
      {"listFiles", Lane::Concurrent},
      {"createFile", Lane::Concurrent},
      {"deleteFile", Lane::Concurrent},
      {"moveFile", Lane::Concurrent},
      {"copyUss", Lane::Concurrent},
      {"chmodFile", Lane::Concurrent},
      {"chownFile", Lane::Concurrent},
      {"chtagFile", Lane::Concurrent},
      // Tools / system
      {"toolSearch", Lane::Serialized},
      {"getInfo", Lane::Concurrent},
      {"viewSyslog", Lane::Concurrent},
      {"listParmlib", Lane::Concurrent},
      {"listProclib", Lane::Concurrent},
      {"listApf", Lane::Concurrent},
      {"listLinklist", Lane::Concurrent},
      // Certificates / keyrings
      {"listRings", Lane::Concurrent},
      {"countRing", Lane::Concurrent},
      {"listCertificates", Lane::Concurrent},
      {"showCertificate", Lane::Concurrent},
      {"exportCertificate", Lane::Concurrent},
      {"importCertificate", Lane::Concurrent},
      {"createKeyring", Lane::Concurrent},
      {"deleteKeyring", Lane::Concurrent},
      {"deleteCertificate", Lane::Concurrent},
      {"connectCertificate", Lane::Concurrent},
      {"setDefaultCertificate", Lane::Concurrent},
      {"trustCertificate", Lane::Concurrent},
      {"renameCertificate", Lane::Concurrent},
      {"refreshDigtcert", Lane::Concurrent},
  };
  return methods;
}

// Methods that spawn child processes. Off unless ZO_EXPERIMENTAL_METHODS names them, so their
// identity under thread-level security can be evaluated without exposing them by default.
const char *const k_experimental_methods[] = {"unixCommand", "tsoCommand"};

void enable_experimental_methods()
{
  const char *env = std::getenv("ZO_EXPERIMENTAL_METHODS");
  if (!env || !*env)
  {
    return;
  }
  const std::string requested = std::string(",") + env + ",";
  for (const char *method : k_experimental_methods)
  {
    if (requested.find(std::string(",") + method + ",") != std::string::npos)
    {
      allowed_methods()[method] = Lane::Concurrent;
      LOG_WARN("Experimental RPC method enabled: %s", method);
    }
  }
}

std::atomic<ZoLogCallback> g_log_callback{nullptr};
std::once_flag g_register_once;
std::mutex g_serialized_lane;

void forward_log(int level, const char *message)
{
  const ZoLogCallback callback = g_log_callback.load(std::memory_order_acquire);
  if (!callback || !message)
  {
    return;
  }
  std::string ascii(message);
  e2a_inplace(ascii);
  callback(level, ascii.c_str());
}

char *to_c_string(const std::string &str)
{
  char *copy = static_cast<char *>(std::malloc(str.size() + 1));
  if (copy)
  {
    std::memcpy(copy, str.c_str(), str.size() + 1);
  }
  return copy;
}

// Builds an EBCDIC JSON-RPC error; `message` must be plain text without quotes or backslashes.
std::string error_response(int code, const std::string &message)
{
  return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" + message +
         "\"},\"id\":null}";
}

std::string dispatch(const std::string &request)
{
  auto parsed = zjson::from_str<zjson::Value>(request);
  if (!parsed.has_value() || !parsed.value().is_object())
  {
    return error_response(RpcErrorCode::PARSE_ERROR, "Failed to parse command request");
  }

  const zjson::Value &method = parsed.value()["method"];
  if (!method.is_string())
  {
    return error_response(RpcErrorCode::INVALID_REQUEST, "Missing method");
  }

  const auto &methods = allowed_methods();
  const auto it = methods.find(method.as_string());
  if (it == methods.end())
  {
    return error_response(RpcErrorCode::METHOD_NOT_FOUND, "Method not available");
  }

  const zjson::Value &params = parsed.value()["params"];
  if (params.is_object() && !params["stream"].is_null())
  {
    return error_response(RpcErrorCode::INVALID_PARAMS, "Streaming is not supported");
  }

  if (it->second == Lane::Serialized)
  {
    const std::lock_guard<std::mutex> lock(g_serialized_lane);
    return RpcServer::get_instance().call(request);
  }
  return RpcServer::get_instance().call(request);
}

} // namespace

extern "C"
{

  int zo_c_init(ZoLogCallback log_callback, int log_level)
  {
    g_log_callback.store(log_callback, std::memory_order_release);
    ZLogger::get_instance().set_host_sink(forward_log, static_cast<LogLevel>(log_level));
    std::call_once(g_register_once, []()
                   {
                     register_all_commands(CommandDispatcher::get_instance());
                     enable_experimental_methods(); });
    return 0;
  }

  void zo_c_set_log_level(int log_level)
  {
    ZLogger::get_instance().set_log_level(static_cast<LogLevel>(log_level));
  }

  char *zo_c_rpc_call(const char *request_json)
  {
    std::string request = request_json ? request_json : "";
    std::string response;
    try
    {
      a2e_inplace(request);
      response = dispatch(request);
    }
    catch (const std::exception &e)
    {
      LOG_ERROR("zo_c_rpc_call failed: %s", e.what());
      response = error_response(RpcErrorCode::INTERNAL_ERROR, "Internal error");
    }
    catch (...)
    {
      response = error_response(RpcErrorCode::INTERNAL_ERROR, "Internal error");
    }
    e2a_inplace(response);
    return to_c_string(response);
  }

  void zo_c_free(char *ptr)
  {
    std::free(ptr);
  }
}
