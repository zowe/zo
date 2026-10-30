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

#ifndef ZAM_H
#define ZAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zmetal.h"
#include "dcbd.h"
#include "ihaecb.h"
#include "zecb.h"
#include "zstorage.h"
#include "ihadcbe.h"
#include "jfcb.h"
#include "ihaexlst.h"
#include "zamtypes.h"
#include "ztime.h"

#if defined(__IBM_METAL__)
#pragma map(open_output_assert, "opnoasrt")
#pragma map(open_input_assert, "opniasrt")
#pragma map(close_assert, "closasrt")
#endif

IO_CTRL *PTR32 open_output_assert(char *, int, int, unsigned char) ATTRIBUTE(amode31);
IO_CTRL *PTR32 open_input_assert(char *, int, int, unsigned char) ATTRIBUTE(amode31);
void close_assert(IO_CTRL *) ATTRIBUTE(amode31);

int write_sync(IO_CTRL *, char *) ATTRIBUTE(amode31);
int read_sync(IO_CTRL *, char *) ATTRIBUTE(amode31);

#if defined(__IBM_METAL__)
#pragma map(open_input_dcb, "OPNIDCB")
#pragma map(open_output_dcb, "OPNODCB")
#pragma map(open_input_acb, "OPNIACB")
#endif

int open_input_dcb(IHADCB *) ATTRIBUTE(amode31);
int open_output_dcb(IHADCB *) ATTRIBUTE(amode31);
int open_input_acb(IFGACB *) ATTRIBUTE(amode31);

#ifdef __IBM_METAL__
#pragma map(close_acb, "CLOSACB")
#pragma map(close_dcb, "CLOSEDCB")
#endif

int close_acb(IFGACB *) ATTRIBUTE(amode31);
int close_dcb(IHADCB *) ATTRIBUTE(amode31);

#if defined(__IBM_METAL__)
#pragma map(write_dcb, "WRITEDCB")
#pragma map(read_dcb, "READDCB")
#endif

int write_dcb(IHADCB *, WRITE_PL *, char *) ATTRIBUTE(amode31);
void read_dcb(IHADCB *, READ_PL *, char *) ATTRIBUTE(amode31);

#if defined(__IBM_METAL__)
#pragma map(open_output_bpam, "OPNOBPAM")
#pragma map(close_output_bpam, "CLSOBPAM")
#pragma map(write_output_bpam, "WRTOBPAM")
#pragma map(delete_member_bpam, "DELMBPAM")
#endif

int open_output_bpam(ZDIAG *PTR32, IO_CTRL * PTR32 * PTR32, const char *PTR32) ATTRIBUTE(amode31);
int delete_member_bpam(ZDIAG *PTR32, IO_CTRL *PTR32) ATTRIBUTE(amode31);
int write_output_bpam(ZDIAG *PTR32, IO_CTRL *PTR32, const char *PTR32, int length) ATTRIBUTE(amode31);
int close_output_bpam(ZDIAG *PTR32, IO_CTRL *PTR32) ATTRIBUTE(amode31);

#if defined(__IBM_METAL__)
#pragma map(open_input_vsam, "OPNIVSAM")
#pragma map(read_input_vsam, "RDIVSAM")
#pragma map(point_input_vsam, "PTNIVSAM")
#pragma map(close_input_vsam, "CLSIVSAM")
#endif
int open_input_vsam(ZDIAG *PTR32 diag, IO_CTRL * PTR32 * PTR32, const char *PTR32) ATTRIBUTE(amode31);
int read_input_vsam(ZDIAG *PTR32 diag, IO_CTRL *PTR32) ATTRIBUTE(amode31);
int point_input_vsam(ZDIAG *PTR32 diag, IO_CTRL *PTR32, TIME_STRUCT *time_struct) ATTRIBUTE(amode31);
int close_input_vsam(ZDIAG *PTR32, IO_CTRL *PTR32) ATTRIBUTE(amode31);

#if defined(__IBM_METAL__)
#pragma map(read_input_jfcb, "RIJFCB")
#pragma map(read_output_jfcb, "ROJFCB")
#endif

// TODO: This function needs to be reworked to address some issues around opening a JFCB for input. Avoid using for now
int read_input_jfcb(IO_CTRL *ioc) ATTRIBUTE(amode31);

int read_output_jfcb(IO_CTRL *ioc) ATTRIBUTE(amode31);

int bldl(IO_CTRL *, BLDL_PL *, int *rsn) ATTRIBUTE(amode31);
int stow(IO_CTRL *, int *rsn) ATTRIBUTE(amode31);
int note(IO_CTRL *, NOTE_RESPONSE *PTR32 note_response, int *rsn) ATTRIBUTE(amode31);
int find_member(IO_CTRL *ioc, int *rsn) ATTRIBUTE(amode31);

int check(DECB *ecb) ATTRIBUTE(amode31);

int snap(IHADCB *, SNAP_HEADER *, void *, int) ATTRIBUTE(amode31);

void eodad() ATTRIBUTE(amode31);

enum AMS_ERR
{
  UNKOWN_MODE,
  OPEN_OUTPUT_ASSERT_RC,
  OPEN_OUTPUT_ASSERT_FAIL,
  OPEN_INPUT_ASSERT_RC,
  OPEN_INPUT_ASSERT_FAIL,
  CLOSE_ASSERT_RC,
  DCBE_REQUIRED,
  UNSUPPORTED_RECFM
};

static IO_CTRL *PTR32 new_io_ctrl()
{
  IO_CTRL *ioc = storage_obtain24(sizeof(IO_CTRL));
  memcpy(ioc->eye, EYE, strlen(EYE));
  memset(ioc, 0x00, sizeof(IO_CTRL));
  return ioc;
}

// TODO(Kelosky): remove this function??
static void set_dcb_info(IHADCB *PTR32 dcb, char *PTR32 ddname, int lrecl, int blkSize, unsigned char recfm)
{
  char ddnam[9] = {0};
  sprintf(ddnam, "%-8.8s", ddname);
  memcpy(dcb->dcbddnam, ddnam, sizeof(dcb->dcbddnam));
  dcb->dcblrecl = lrecl;
  dcb->dcbblksi = blkSize;
  dcb->dcbrecfm = recfm;
}

typedef void (*PTR32 EODAD)() ATTRIBUTE(amode31);

static void set_dcb_dcbe(IHADCB *PTR32 dcb)
{
  // get space for DCBE + buffer
  int ctrl_len = (int)sizeof(FILE_CTRL) + (int)dcb->dcbblksi;
  FILE_CTRL *PTR32 fc = storage_obtain31(ctrl_len);
  memset(fc, 0x00, ctrl_len);

  // init file control
  fc->ctrl_len = ctrl_len;
  fc->buffer_len = dcb->dcbblksi;

  // buffer is at the end of the structure
  fc->buffer = (unsigned char *PTR32)fc + offsetof(FILE_CTRL, buffer) + sizeof(fc->buffer);

  // init DCBE
  fc->dcbe.dcbelen = sizeof(DCBE);
  char *dcbeid = "DCBE";
  memcpy(fc->dcbe.dcbeid, dcbeid, strlen(dcbeid));

  // retain access to DCB / file control
  dcb->dcbdcbe = fc;
}

static void set_eod(IHADCB *PTR32 dcb, EODAD eodad)
{
  FILE_CTRL *fc = dcb->dcbdcbe;
  // retain access to DCB / file control
  fc->dcbe.dcbeeoda = (void *PTR32)eodad;
}

#endif
