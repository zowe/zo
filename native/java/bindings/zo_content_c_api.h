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

#ifndef ZO_CONTENT_C_API_H
#define ZO_CONTENT_C_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZO_CONTENT_OK 0
#define ZO_CONTENT_FAILED 1
#define ZO_CONTENT_NOT_FOUND 2
#define ZO_CONTENT_ETAG_MISMATCH 3
#define ZO_CONTENT_INVALID 4

/** All strings are ASCII (ISO8859-1) and may be NULL. */
typedef struct {
    const char *encoding;       /* codepage of the z/OS resource; "binary" for no conversion */
    const char *local_encoding; /* codepage of the caller's bytes; NULL = UTF-8 */
    const char *volser;         /* data sets only: volume for uncataloged data sets */
    const char *etag;           /* writes only: expected current ETag */
} ZoContentOptions_C;

/**
 * Content bytes in `data` are exchanged unconverted (already in local_encoding, or raw for
 * binary). All other strings are ASCII. `status` is one of ZO_CONTENT_*.
 */
typedef struct {
    int status;
    int created;           /* writes: 1 if the member/file did not exist before */
    char *data;
    size_t length;
    char *etag;
    char *warning;         /* writes: truncation warning */
    char *error_message;
} ZoContentResult_C;

ZoContentResult_C *zo_c_read_dataset(const char *dsname, const ZoContentOptions_C *options);
ZoContentResult_C *zo_c_write_dataset(const char *dsname, const char *data, size_t length, const ZoContentOptions_C *options);
ZoContentResult_C *zo_c_read_file(const char *path, const ZoContentOptions_C *options);
ZoContentResult_C *zo_c_write_file(const char *path, const char *data, size_t length, const ZoContentOptions_C *options);
void zo_c_free_content_result(ZoContentResult_C *result);

#ifdef __cplusplus
}
#endif

#endif // ZO_CONTENT_C_API_H
