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

#ifndef ZUSFTYPE_H
#define ZUSFTYPE_H

#include <stdint.h>
#include "ztype.h"
#include <functional>

#define ZUSF_RTNCD_SERVICE_FAILURE -2
#define ZUSF_RTNCD_MAX_JOBS_REACHED -3
#define ZUSF_RTNCD_INSUFFICIENT_BUFFER -4
#define ZUSF_RTNCD_JOB_NOT_FOUND -5

#define ZUSF_DEFAULT_BUFFER_SIZE 128000
#define ZUSF_DEFAULT_MAX_JOBS 100
#define ZUSF_DEFAULT_MAX_DDS 100

ZNP_PACK_ON

// NOTE(zFernand0): Figure out how to visualize the struct in memory
// NOTE(Kelosky): struct is padded to nearest double word boundary; ensure proper alignment for fields

typedef struct
{
  char eye[3];              // future use
  unsigned char version[1]; // future use
  int32_t len;              // future use

  int16_t mode;         // permissions
  char file_name[1024]; // filename

  ZEncode encoding_opts;
  char etag[34]; // Two 64-bit hex values, separator, and null terminator

  ZDIAG diag;

  bool created;
  char _pad[3];

  std::function<void(uint64_t)> set_size_callback;

  // Invoked per chunk during streamed read/write to signal that the request
  // is still making progress (e.g. to feed a caller's request-timeout watchdog)
  std::function<void()> update_heartbeat_callback;
} ZUSF;

ZNP_PACK_OFF

#endif
