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

#ifndef ZO_RPC_C_API_H
#define ZO_RPC_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receives one native log record. `message` is ASCII (ISO8859-1) and only valid for the
 * duration of the call. Invoked on the thread that produced the record.
 */
typedef void (*ZoLogCallback)(int level, const char *message);

/**
 * One-time initialization: registers zo RPC commands and routes native logging to
 * `log_callback` (pass NULL to discard native logs). Safe to call more than once; only
 * the first call registers commands, later calls update the log callback and level.
 * @return 0 on success
 */
int zo_c_init(ZoLogCallback log_callback, int log_level);

/** Change the native log level (zlog_level_t values: 0=TRACE .. 6=OFF). */
void zo_c_set_log_level(int log_level);

/**
 * Execute one JSON-RPC 2.0 request in-process and return the JSON-RPC response.
 * Only methods on the embedded allowlist are accepted; anything else returns a
 * METHOD_NOT_FOUND error without reaching the dispatcher. Request and response are
 * ASCII (ISO8859-1); non-ASCII characters must be sent as JSON \u escapes.
 * @return malloc'd response string, free with zo_c_free(); NULL only on allocation failure
 */
char *zo_c_rpc_call(const char *request_json);

void zo_c_free(char *ptr);

#ifdef __cplusplus
}
#endif

#endif // ZO_RPC_C_API_H
