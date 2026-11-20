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

#ifndef ZJBM_H
#define ZJBM_H

#include "ztype.h"
#include "zssitype.h"
#include "zjbtype.h"

ZNP_PACK_ON
typedef struct
{
  int32_t time;
  unsigned char date[4];
} ZJB_JOB_TIME;

typedef struct
{
  ZJB_JOB_TIME submitted;
  char system[8];
  char member[8];
  ZJB_JOB_TIME started;
  ZJB_JOB_TIME ended;
} ZJB_JOB_EXEC;

typedef struct
{
  STATJQTR statjqtr;
  char phase_text[64 + 1];
  char subsystem[4 + 1];
  ZJB_JOB_EXEC execution;
} ZJB_JOB_INFO;

ZNP_PACK_OFF

#if defined(__cplusplus) && defined(__MVS__)
extern "OS"
{
#elif defined(__cplusplus)
extern "C"
{
#endif

#define DSN_ENTRY_SIZE 44
#define MAX_DSN_ENTRIES 100
#define MAX_DSN_ENTRY_SIZE (MAX_DSN_ENTRIES * DSN_ENTRY_SIZE)

  int ZJBMVIEW(ZJB *PTR64, ZJB_JOB_INFO **PTR64, int *PTR64);
  int ZJBMLIST(ZJB *PTR64, ZJB_JOB_INFO **PTR64, int *PTR64);
  int ZJBMGJQ(ZJB *, SSOB *, STAT *, STATJQ * PTR32 * PTR32);
  int ZJBMEMSG(ZJB *, STAT *PTR64, SSOB *PTR64, int);
  int ZJBMTCOM(ZJB *PTR64, STAT *PTR64 stat, ZJB_JOB_INFO **PTR64, int *PTR64);
  int ZJBMLSDS(ZJB *PTR64, STATSEVB **PTR64, int *PTR64);
  int ZJBMLPRC(ZJB *PTR64, char *buffer, int *buffer_size, int *entries);
  int ZJBSYMB(ZJB *PTR64, const char *PTR64, char *PTR64, int *value_size);
  int ZJBMPRG(ZJB *PTR64);
  int ZJBMCNL(ZJB *PTR64, int flags);
  int ZJBMHLD(ZJB *PTR64);
  int ZJBMRLS(ZJB *PTR64);
  int ZJBMMOD(ZJB *PTR64, int type, int flags);

#if defined(__cplusplus)
}
#endif

#endif
