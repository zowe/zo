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

#ifndef ZO_SERVER_TEST_HPP
#define ZO_SERVER_TEST_HPP

#include <string>
#include <cstdio>
#include <sys/types.h>

struct ServerHandle
{
  pid_t pid;
  FILE *output_stream;
  FILE *input_stream;
};

std::string read_line_from_server(ServerHandle &handle, int timeout_ms = 5000);
std::string read_rpc_response(ServerHandle &handle, int timeout_ms = 5000);
void write_to_server(ServerHandle &handle, const std::string &input);
ServerHandle start_server(const std::string &command, bool read_ready_message = false);
void stop_server(ServerHandle &handle);
int next_rpc_id();
std::string make_rpc_request(const std::string &method, const std::string &params, int &id);
std::string make_rpc_request(const std::string &method, const std::string &params);

extern const std::string zo_dir;
extern const std::string zo_server_command;

void zo_server_tests();

#endif
