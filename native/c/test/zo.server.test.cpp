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

#include <stdexcept>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fstream>
#include <fcntl.h>
#include <atomic>
#include "ztest.hpp"
#include "../ztype.h"
#include "../commands/server.hpp"
#include "zo.server.test.hpp"

using namespace ztst;

std::string read_line_from_server(ServerHandle &handle, int timeout_ms)
{
  fd_set read_fds;
  struct timeval timeout;
  int fd = fileno(handle.output_stream);
  if (fd == -1)
  {
    throw std::runtime_error("Failed to get file descriptor");
  }

  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);

  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  int result = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
  if (result <= 0)
  {
    throw std::runtime_error("Timeout waiting for server output");
  }

  char buffer[512];
  if (fgets(buffer, sizeof(buffer), handle.output_stream))
  {
    return std::string(buffer);
  }

  throw std::runtime_error("Failed to read from server");
}

std::string read_rpc_response(ServerHandle &handle, int timeout_ms)
{
  fd_set read_fds;
  struct timeval timeout;
  int fd = fileno(handle.output_stream);
  if (fd == -1)
  {
    throw std::runtime_error("Failed to get file descriptor");
  }

  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);

  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  int result = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
  if (result <= 0)
  {
    throw std::runtime_error("Timeout waiting for server output");
  }

  std::string line;
  int c;
  while ((c = fgetc(handle.output_stream)) != EOF)
  {
    line += static_cast<char>(c);
    if (c == '\n')
    {
      break;
    }
  }
  return line;
}

void write_to_server(ServerHandle &handle, const std::string &input)
{
  if (fputs(input.c_str(), handle.input_stream) == EOF || fflush(handle.input_stream) != 0)
  {
    throw std::runtime_error("Failed to write to server");
  }
}

ServerHandle start_server(const std::string &command, bool read_ready_message)
{
  int output_pipe[2];
  int input_pipe[2];

  if (pipe(output_pipe) == -1 || pipe(input_pipe) == -1)
  {
    throw std::runtime_error("Failed to create pipes");
  }

  pid_t pid = fork();
  if (pid == -1)
  {
    throw std::runtime_error("Failed to fork process");
  }

  if (pid == 0)
  {
    close(output_pipe[0]);
    close(input_pipe[1]);

    dup2(input_pipe[0], STDIN_FILENO);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(output_pipe[1], STDERR_FILENO);

    close(input_pipe[0]);
    close(output_pipe[1]);

    const char *shell = getenv("SHELL") ? getenv("SHELL") : "/bin/sh";
    execl(shell, shell, "-c", command.c_str(), (char *)nullptr);
    _exit(127);
  }

  close(output_pipe[1]);
  close(input_pipe[0]);

  FILE *output_stream = fdopen(output_pipe[0], "r");
  FILE *input_stream = fdopen(input_pipe[1], "w");

  if (!output_stream || !input_stream)
  {
    if (output_stream)
      fclose(output_stream);
    if (input_stream)
      fclose(input_stream);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    throw std::runtime_error("Failed to open streams");
  }

  auto handle = ServerHandle{pid, output_stream, input_stream};
  if (read_ready_message)
  {
    read_line_from_server(handle);
  }

  return handle;
}

void stop_server(ServerHandle &handle)
{
  kill(handle.pid, SIGINT);
  fclose(handle.input_stream);
  fclose(handle.output_stream);
  waitpid(handle.pid, nullptr, 0);
}

int next_rpc_id()
{
  static std::atomic<int> id_counter{1};
  return id_counter++;
}

std::string make_rpc_request(const std::string &method, const std::string &params, int &id)
{
  id = next_rpc_id();
  return "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params + ",\"id\":" + std::to_string(id) +
         "}\n";
}

std::string make_rpc_request(const std::string &method, const std::string &params)
{
  int dummy_id;
  return make_rpc_request(method, params, dummy_id);
}

const std::string zo_dir = "./../build-out";
const std::string zo_server_command = zo_dir + "/zo server";

void zo_server_tests()
{

  describe("zo server tests", []() -> void
  {
    it("should print ready message on startup", []() -> void
    {
      ServerHandle server = start_server(zo_server_command);
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"message\":\"zo server is ready to accept input\"");
      Expect(response).ToContain("\"status\":\"ready\"");
    });
    it("should print ready message with version", []() -> void
    {
      ServerHandle server = start_server(zo_server_command);
      std::string response = read_line_from_server(server);
      stop_server(server);

      static const std::regex re(R"("version"\s*:\s*\"([^"]*)\")");
      std::smatch m;

      Expect(std::regex_search(response, m, re)).ToBe(true);
      const std::string version = m[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum
      Expect(response).ToContain("\"message\":\"zo server is ready to accept input\"");
      Expect(response).ToContain("\"status\":\"ready\"");
    });
    it("should return error message for invalid JSON input", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(server, "invalid\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"code\":-32700");
      Expect(response).ToContain("\"message\":\"Failed to parse command request\"");
    });
    it("should execute unixCommand and return output", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(
          server,
          "{\"jsonrpc\":\"2.0\",\"method\":\"unixCommand\",\"params\":{\"commandText\":\"whoami\"},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"success\":true");
      Expect(response).ToContain("\"data\":");
    });

    it("should execute getInfo and return output", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(server, "{\"jsonrpc\":\"2.0\",\"method\":\"getInfo\",\"params\":{},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"success\":true");

      // Version information
      static const std::regex rev(R"("version"\s*:\s*\"([^"]*)\")");
      std::smatch mv;

      Expect(std::regex_search(response, mv, rev)).ToBe(true);
      const std::string version = mv[1].str();

      Expect(version.length()).ToBeGreaterThanOrEqualTo(5); // X.X.X at minimum

      // Build date information
      static const std::regex red(R"("buildDate"\s*:\s*\"([^"]*)\")");
      std::smatch md;

      Expect(std::regex_search(response, md, red)).ToBe(true);
      const std::string buildDate = md[1].str();

      Expect(buildDate.length()).ToBeGreaterThanOrEqualTo(11); // MMM DD YYYY at minimum
    });

    it("should execute consoleCommand via zoa and return output", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(
          server,
          "{\"jsonrpc\":\"2.0\",\"method\":\"consoleCommand\",\"params\":{\"commandText\":\"D T\"},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"success\":true");
      Expect(response).ToContain("IEE136I");
    });

    it("should return an error when zoa cannot be found", []() -> void
    {
      ServerHandle server = start_server("ZOA_PATH=/nonexistent/zoa " + zo_server_command, true);
      write_to_server(
          server,
          "{\"jsonrpc\":\"2.0\",\"method\":\"consoleCommand\",\"params\":{\"commandText\":\"D T\"},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).Not().ToContain("\"success\":true");
      Expect(response).ToContain("zoa");
    });

    it("should preserve stderr detail in RPC error responses", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(
          server,
          "{\"jsonrpc\":\"2.0\",\"method\":\"unixCommand\",\"params\":{\"commandText\":\"echo ONLY_STDERR >&2; exit 7\"},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).Not().ToContain("\"success\":true");
      Expect(response).ToContain("\"details\"");
      Expect(response).ToContain("ONLY_STDERR");
    });

    it("should surface stderr from a successful unixCommand", []() -> void
    {
      ServerHandle server = start_server(zo_server_command, true);
      write_to_server(
          server,
          "{\"jsonrpc\":\"2.0\",\"method\":\"unixCommand\",\"params\":{\"commandText\":\"echo THIS_IS_STDOUT; echo THIS_IS_STDERR >&2\"},\"id\":1}\n");
      std::string response = read_line_from_server(server);
      stop_server(server);

      Expect(response).ToContain("\"success\":true");
      Expect(response).ToContain("THIS_IS_STDOUT");
      Expect(response).ToContain("\"stderr\"");
      Expect(response).ToContain("THIS_IS_STDERR");
    });
  });
}
