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

#include "zdsm.h"
#include "iggcsina.h"
#include "zdstype.h"
#include "zmetal.h"
#include "zdbg.h"
#include "zwto.h"
#include "ztime.h"
#include <ctype.h>
#include <string.h>
#include "zam.h"

// OBTAIN option parameters for CAMLST
const unsigned char OPTION_EADSCB = 0x08;  // EADSCB=OK
const unsigned char OPTION_NOQUEUE = 0x04; // NOQUEUE=OK

#ifndef MAX_DSCBS
#define MAX_DSCBS 12
#endif

#pragma prolog(ZDSDEL, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZDSDEL, " ZWEEPILG ")
void ZDSDEL(ZDS *zds)
{
  if (zds->csi)
  {
    delete_module("IGGCSI00");
  }

  zds->csi = NULL;
}

// https://www.ibm.com/docs/en/zos/3.1.0?topic=retrieval-parameters
typedef int (*IGGCSI00)(int *PTR32 rsn, CSIFIELD *PTR32 selection,
                        void *PTR32 work_area) ATTRIBUTE(amode31);

#pragma prolog(ZDSCSI00, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZDSCSI00, " ZWEEPILG ")
int ZDSCSI00(ZDS *zds, CSIFIELD *selection, void *work_area)
{
  int rc = 0;
  int rsn = 0;

  // load our service on first call
  if (!zds->csi)
  {
    zds->csi = (void *PTR64)load_module31("IGGCSI00"); // EP which doesn't require R0 == 0
  }

  if (!zds->csi)
  {
    strcpy(zds->diag.service_name, "LOAD");
    ZDIAG_SET_MSG(&zds->diag, "Load failure for IGGCSI00");
    zds->diag.detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  rc = ((IGGCSI00)(zds->csi))(&rsn, selection, work_area);

  // https://www.ibm.com/docs/en/SSLTBW_3.1.0/com.ibm.zos.v3r1.idac100/c1055.htm
  if (0 != rc)
  {
    strcpy(zds->diag.service_name, "IGGCSI00");
    ZDIAG_SET_MSG(&zds->diag, "IGGCSI00 rc was: '%d', rsn was: '%04x'", rc, rsn);
    zds->diag.service_rc = rc;
    zds->diag.service_rsn = rsn;
    zds->diag.detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  return rc;
}

typedef struct
{
  unsigned char data[30];
} ZSMS_DATA;

// https://www.ibm.com/docs/en/zos/3.1.0?topic=retrieval-parameters
typedef int (*IGWASMS)(
    int *rc,
    int *rsn,
    int problem_data[2],
    int *dsn_len,
    const char *dsn,
    ZSMS_DATA sms_data[3],
    int *ds_type // 1 for PDSE, 2 for HFS
    ) ATTRIBUTE(amode31);

#pragma prolog(ZDSDSCB1, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZDSDSCB1, " ZWEEPILG ")

// Obtain the Data Set Control Block 1 for a data set, given its name and volser
// Full PDF for DFSMSdfp advanced services: https://www.ibm.com/docs/en/SSLTBW_2.5.0/pdf/idas300_v2r5.pdf
// Doc page: https://www.ibm.com/docs/en/zos/3.1.0?topic=macros-reading-dscbs-from-vtoc-using-obtain
int ZDSDSCB1(ZDS *zds, const char *dsn, const char *volser, DSCBFormat1 *dscb)
{
  // workarea: each DSCB is 140 bytes, we need enough space for format-1 DSCB, format-8 DSCB and max possible format-3 DSCBs (10)
  // adding 140 bytes for the workarea itself
  char workarea[sizeof(IndexableDSCBFormat1) * MAX_DSCBS];
  ObtainParams params = {0};

  // We're using OBTAIN through CAMLST SEARCH to get the DSCBs, see here for more info:
  // https://www.ibm.com/docs/en/zos/3.1.0?topic=obtain-reading-dscb-by-data-set-name

  // OBTAIN by data set name
  params.reserved = 0xC1;
  params.number_dscbs = MAX_DSCBS;
  params.option_flags = OPTION_EADSCB;
  // Allow lookup of format-1 or format-8 DSCB
  char dsname[ZDS_MAX_DSNAME_LENGTH] = {0};
  memset(dsname, ' ', sizeof(dsname));
  memcpy(dsname, dsn, ZMIN(strlen(dsn), ZDS_MAX_DSNAME_LENGTH));
  params.listname_addrx.dsname_ptr = dsname;
  char volume[ZDS_MAX_VOLSER_LENGTH] = {0};
  memset(volume, ' ', sizeof(volume));
  memcpy(volume, volser, ZMIN(strlen(volser), ZDS_MAX_VOLSER_LENGTH));
  params.listname_addrx.volume_ptr = volume;
  params.listname_addrx.workarea_ptr = workarea;

  int rc = obtain_camlst(params);
  if (0 != rc)
  {
    strcpy(zds->diag.service_name, "OBTAIN");
    ZDIAG_SET_MSG(&zds->diag, "OBTAIN SVC failed for %.44s on %.6s with rc=%d, workarea_ptr=%p",
                  dsn, volser, rc, workarea);
    zds->diag.service_rc = rc;
    zds->diag.detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  IndexableDSCBFormat1 indexable_dscb = {0};
  for (size_t i = 0ULL; i < (size_t)MAX_DSCBS - 1ULL; i++)
  {
    memcpy(&indexable_dscb, workarea + (i * (sizeof(IndexableDSCBFormat1) - 1ULL)), sizeof(indexable_dscb));
    // The returned DSCB does not include the key, but we can infer the returned variables by re-aligning the struct
    const DSCBFormat1 *temp_dscb = (DSCBFormat1 *)&indexable_dscb;

    // Look for either Format-1 or Format-8 DSCB
    if (temp_dscb == NULL || (temp_dscb->ds1fmtid != '1' && temp_dscb->ds1fmtid != '8'))
    {
      continue;
    }

    memcpy(dscb, temp_dscb, sizeof(DSCBFormat1));
    return RTNCD_SUCCESS;
  }

  strcpy(zds->diag.service_name, "OBTAIN");
  ZDIAG_SET_MSG(&zds->diag, "Could not find Format-1 or Format-8 DSCB, OBTAIN rc=%d, sizeof(dscb)=%d", rc, sizeof(IndexableDSCBFormat1));
  zds->diag.detail_rc = ZDS_RTNCD_UNEXPECTED_ERROR;
  return RTNCD_FAILURE;
}

#pragma prolog(ZDSOIVSM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSOIVSM, " ZWEEPILG ")
int ZDSOIVSM(ZDS *zds, IO_CTRL **ioc, const char *ddname)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  char ddname31[8] = {0};
  memcpy(ddname31, ddname, sizeof(ddname31));

  IO_CTRL *PTR32 ioc31 = NULL;
  rc = open_input_vsam(&zds31.diag, &ioc31, ddname31);
  *ioc = ioc31;
  memcpy(zds, &zds31, sizeof(ZDS));

  if (0 != rc)
  {
    ZDS zds_close = {0};
    close_input_vsam(&zds_close.diag, ioc31);
    *ioc = NULL;
  }

  return rc;
}

#pragma prolog(ZDSPIVSM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSPIVSM, " ZWEEPILG ")
int ZDSPIVSM(ZDS *zds, IO_CTRL *ioc)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));

  __pack((unsigned char *)&zds->date, sizeof(zds->date), (unsigned char *)&zds->ebcdic_date, sizeof(zds->ebcdic_date));

  TIME_STRUCT time_struct = {0};
  memcpy(&time_struct.time, &zds->ts_binary, sizeof(zds->ts_binary));
  memcpy(&time_struct.date, &zds->date, sizeof(zds->date));

  rc = point_input_vsam(&zds31.diag, ioc, &time_struct);
  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}

#pragma prolog(ZDSRIVSM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSRIVSM, " ZWEEPILG ")
int ZDSRIVSM(ZDS *zds, IO_CTRL *ioc)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  rc = read_input_vsam(&zds31.diag, ioc);

  if (0 == rc)
  {
    TIME_STRUCT time_struct = {0};
    memcpy(&time_struct.time, &ioc->dsinf.dsinstke, sizeof(ioc->dsinf.dsinstke));
    memcpy(&time_struct.date, &ioc->dsinf.dsinstke, sizeof(ioc->dsinf.dsinstke));

    etod_t etod = {0};
    memcpy(&etod[0], &ioc->dsinf.dsinstke, sizeof(ioc->dsinf.dsinstke));
    int stck_rc = stckeconv(&etod, &time_struct);
    if (0 != stck_rc)
    {
      zds31.diag.detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
      zds31.diag.service_rc = stck_rc;
      strcpy(zds31.diag.service_name, "STCKECONV");
      zds31.diag.e_msg_len = sprintf(zds31.diag.e_msg, "Failed to STCKECONV rc was: %d", stck_rc);
      memcpy(zds, &zds31, sizeof(ZDS));
      return RTNCD_FAILURE;
    }

    unsigned char ebcdic_date[8] = {0};
    __unpk(ebcdic_date, sizeof(ebcdic_date), &time_struct.date.yyyymmdd.year[0], sizeof(time_struct.date));
    memcpy(zds31.ebcdic_date, ebcdic_date, sizeof(zds31.ebcdic_date));
    memcpy(&zds31.ts_binary, &time_struct.time, sizeof(zds31.ts_binary));
  }

  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}

#pragma prolog(ZDSCIVSM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSCIVSM, " ZWEEPILG ")
int ZDSCIVSM(ZDS *zds, IO_CTRL *ioc)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  rc = close_input_vsam(&zds31.diag, ioc);
  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}

#pragma prolog(ZDSOBPAM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSOBPAM, " ZWEEPILG ")
int ZDSOBPAM(ZDS *zds, IO_CTRL **ioc, const char *ddname)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  char ddname31[8] = {0};
  memcpy(ddname31, ddname, sizeof(ddname31));

  IO_CTRL *PTR32 ioc31 = NULL;
  rc = open_output_bpam(&zds31.diag, &ioc31, ddname31);
  *ioc = ioc31;
  memcpy(zds, &zds31, sizeof(ZDS));

  if (0 != rc)
  {
    ZDS zds_close = {0};
    close_output_bpam(&zds_close.diag, ioc31);
    *ioc = NULL;
  }

  return rc;
}

#pragma prolog(ZDSWBPAM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSWBPAM, " ZWEEPILG ")
int ZDSWBPAM(ZDS *zds, IO_CTRL *ioc, const char *data, int *length)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));

  // Handle zero-length records (empty lines) - allocate at least 1 byte to avoid NULL pointer
  int alloc_size = (*length > 0) ? *length : 1;
  char *data31 = (char *)storage_obtain31(alloc_size);
  if (*length > 0)
  {
    memcpy(data31, data, *length);
  }

  rc = write_output_bpam(&zds31.diag, ioc, data31, *length);
  storage_release(alloc_size, data31);
  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}

#pragma prolog(ZDSCBPAM, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSCBPAM, " ZWEEPILG ")
int ZDSCBPAM(ZDS *zds, IO_CTRL *ioc)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  rc = close_output_bpam(&zds31.diag, ioc);
  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}

#pragma prolog(ZDSDMBR, " ZWEPROLG NEWDSA=(YES,24) ")
#pragma epilog(ZDSDMBR, " ZWEEPILG ")
int ZDSDMBR(ZDS *zds, IO_CTRL *ioc)
{
  int rc = 0;
  ZDS zds31 = {0};
  memcpy(&zds31, zds, sizeof(ZDS));
  rc = delete_member_bpam(&zds31.diag, ioc);
  memcpy(zds, &zds31, sizeof(ZDS));
  return rc;
}