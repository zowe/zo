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

#ifndef ZLOGGER_METAL_H
#define ZLOGGER_METAL_H

/* Metal C compatible includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
  ZLOGLEVEL_TRACE = 0,
  ZLOGLEVEL_DEBUG = 1,
  ZLOGLEVEL_INFO = 2,
  ZLOGLEVEL_WARN = 3,
  ZLOGLEVEL_ERROR = 4,
  ZLOGLEVEL_FATAL = 5,
  ZLOGLEVEL_OFF = 6
} zlog_level_t;

#define ZLOG_MAX_MSG 1024

#if defined(__cplusplus) && defined(__MVS__)
extern "OS"
{
#elif defined(__cplusplus)
extern "C"
{
#endif

  /**
   * Initialize logger from Metal C code
   * @param log_file_path Path to log file (use Unix path format in USS)
   * @param min_level Minimum log level
   * @return 0 on success, -1 on error
   */
  int ZLGINIT(const char *log_file_path, int *min_level);

  /**
   * Write a pre-formatted message from Metal C
   * @param level Log level (use ZLOG_METAL_* constants)
   * @param message Pre-formatted message string
   * @return 0 on success, -1 on error
   */
  int ZLGWRITE(int *level, const char *message);

  /**
   * Format and write a message from Metal C with simple formatting
   * Note: Metal C has limited printf support, so this provides basic formatting
   * @param level Log level
   * @param prefix Message prefix/category
   * @param message Main message
   * @param suffix Optional suffix (can be NULL)
   * @return 0 on success, -1 on error
   */
  int ZLGWRFMT(int level, const char *prefix, const char *message, const char *suffix);

  /**
   * Set log level from Metal C
   * @param level New minimum log level
   */
  void ZLGSTLVL(int *level);

  /**
   * Get current log level from Metal C
   * @return Current minimum log level
   */
  int ZLGGTLVL(void);

  /**
   * Cleanup logger from Metal C
   */
  void ZLGCLEAN(void);

#if defined(__cplusplus)
}
#endif

/* Convenience macros for Metal C logging
 * ZLOG_ENABLE is set only for DEBUG builds and affects Metal C instrumentation,
 * not C++ logging. Without it these macros are no-ops with zero overhead.
 */
#ifdef ZLOG_ENABLE
#define ZLOGTMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_TRACE}), msg)
#define ZLOGDMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_DEBUG}), msg)
#define ZLOGIMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_INFO}), msg)
#define ZLOGWMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_WARN}), msg)
#define ZLOGEMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_ERROR}), msg)
#define ZLOGFMSG(msg) ZLGWRITE(&((int){ZLOGLEVEL_FATAL}), msg)

#define ZLOGTFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_TRACE, prefix, msg, suffix)
#define ZLOGDFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_DEBUG, prefix, msg, suffix)
#define ZLOGIFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_INFO, prefix, msg, suffix)
#define ZLOGWFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_WARN, prefix, msg, suffix)
#define ZLOGEFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_ERROR, prefix, msg, suffix)
#define ZLOGFFMT(prefix, msg, suffix) ZLGWRFMT(ZLOGLEVEL_FATAL, prefix, msg, suffix)
#else
/* When ZLOG_ENABLE is not defined, logging macros become no-ops */
#define ZLOGTMSG(msg) ((void)0)
#define ZLOGDMSG(msg) ((void)0)
#define ZLOGIMSG(msg) ((void)0)
#define ZLOGWMSG(msg) ((void)0)
#define ZLOGEMSG(msg) ((void)0)
#define ZLOGFMSG(msg) ((void)0)

#define ZLOGTFMT(prefix, msg, suffix) ((void)0)
#define ZLOGDFMT(prefix, msg, suffix) ((void)0)
#define ZLOGIFMT(prefix, msg, suffix) ((void)0)
#define ZLOGWFMT(prefix, msg, suffix) ((void)0)
#define ZLOGEFMT(prefix, msg, suffix) ((void)0)
#define ZLOGFFMT(prefix, msg, suffix) ((void)0)
#endif /* ZLOG_ENABLE */

#endif /* ZLOGGER_METAL_H */