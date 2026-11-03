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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zmetal.h"
#include "asasymbp.h"
#include "zstorage.h"
#include "ztype.h"
#include "zutm.h"
#include "ztime.h"
#include "zutm31.h"
#include "zam.h"
#include "zuttype.h"
#include "zssi.h"
#include "csvapfaa.h"
#include "csvdlaa.h"
#include "zwto.h"
#include "zdbg.h"
#include "zrecovery.h"

#define ZUT_BPXWDYN_SERVICE_FAILURE -2

static const char *const zut_dfsmsdfp_names[] = {
    [ZUTMSDFP_IEBCOPY] = "IEBCOPY",
    [ZUTMSDFP_IEBGENER] = "IEBGENER",
};

#pragma prolog(ZUTMSDFP, " ZWEPROLG NEWDSA=(YES,4),LOC24=YES ")
#pragma epilog(ZUTMSDFP, " ZWEEPILG ")
int ZUTMSDFP(ZDIAG *diag, ZUTMSDFP_UTILITY *utility, DFSMSdfp_OPT_LIST *opts, DFSMSdfp_DD_LIST *ddlist)
{
  int rc = 0;

  if ((size_t)*utility >= sizeof(zut_dfsmsdfp_names) / sizeof(zut_dfsmsdfp_names[0]))
  {
    ZDIAG_SET_MSG(diag, "Unknown DFSMSdfp utility index %d", (int)*utility);
    diag->detail_rc = ZUT_RTNCD_LOAD_FAILURE;
    return RTNCD_FAILURE;
  }

  const char *utility_name = zut_dfsmsdfp_names[*utility];

  // Bring the utility into storage. LOAD keeps it resident across the call below and
  // yields its entry point with the module's AMODE bit set, which zutm1call24 uses
  // to enter the utility in its own AMODE.
  void *PTR64 ep = load_module(utility_name);
  if (!ep)
  {
    ZDIAG_SET_MSG(diag, "Load failure for program '%.8s', not found", utility_name);
    diag->detail_rc = ZUT_RTNCD_LOAD_FAILURE;
    return RTNCD_FAILURE;
  }

  // Below the line: the option list, the ddname list, and the parameter list must all be
  // 24-bit addressable. This function's DSA is LOC24, so these locals are below the line.
  DFSMSdfp_OPT_LIST btl_opts = {0};
  DFSMSdfp_DD_LIST btl_dd_list = {0};

  if (opts)
  {
    memcpy(&btl_opts, opts, sizeof(DFSMSdfp_OPT_LIST));
  }
  if (ddlist)
  {
    memcpy(&btl_dd_list, ddlist, sizeof(DFSMSdfp_DD_LIST));
  }

  // Problem-program parameter list (R1) for the utility: the option-list address followed
  // by the ddname-list address, with the high-order (VL) end-of-list bit set on the last
  // entry. Built as integer words so the compiler does not strip the VL bit while
  // sanitizing a pointer argument for the AMODE 31 call.
  unsigned int parm_list[2];
  parm_list[0] = (unsigned int)(uintptr_t)&btl_opts;
  parm_list[1] = (unsigned int)(uintptr_t)&btl_dd_list.TotalLength | 0x80000000U;

  rc = zutm1call24((unsigned int)(uintptr_t)ep, parm_list);

  delete_module(utility_name);

  if (0 != rc)
  {
    ZDIAG_SET_MSG(diag, "Execution failure for program '%.8s'", utility_name);
    diag->detail_rc = ZUT_RTNCD_LOAD_FAILURE;
    return RTNCD_FAILURE;
  }

  return RTNCD_SUCCESS;
}

// takes a conventional paramter list
typedef int (*BPXWDYN)(
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32,
    BPXWDYN_PARM *PTR32) ATTRIBUTE(amode31);

// Doc:
// * keywords - https://www.ibm.com/docs/en/zos/3.1.0?topic=output-requesting-dynamic-allocation
// * return codes - https://www.ibm.com/docs/en/zos/3.1.0?topic=output-bpxwdyn-return-codes
// * detail codes (high 4 hex bytes) - https://www.ibm.com/docs/en/zos/3.1.0?topic=codes-interpreting-error-reason-from-dynalloc#erc__mjfig8
// * parm list - https://www.ibm.com/docs/en/zos/3.1.0?topic=conventions-conventional-mvs-parameter-list
#pragma prolog(ZUTWDYN, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZUTWDYN, " ZWEEPILG ")
int ZUTWDYN(BPXWDYN_PARM *parm, BPXWDYN_RESPONSE *response)
{
  int rc = 0;

  int rtddn_index = RTDDN_INDEX;
  int rtdsn_index = RTDSN_INDEX;
  int msg_index = MSG_INDEX;
  int input_parameters = INPUT_PARAMETERS;

  // load our service
  BPXWDYN dynalloc = (BPXWDYN)load_module31("BPXWDY2"); // EP which doesn't require R0 == 0
  if (!dynalloc)
  {
    // TODO(Kelosky): pass diag information
    // strcpy(zds->diag.service_name, "LOAD");
    // zds->diag.e_msg_len = sprintf(zds->diag.e_msg, "Load failure for IGGCSI00");
    // zds->diag.detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  // allow for MSG_ENTRIES response parameters + 2 input parameters
  BPXWDYN_RET_ARG parameters[MSG_ENTRIES + INPUT_PARAMETERS] = {0};
  memcpy(&parameters[ALLOC_STRING_INDEX], parm, sizeof(BPXWDYN_RESPONSE));

  if (parm->rtdd)
  {
    parameters[rtddn_index].len = 8 + 1; // max ddname length is 8 + 1 for the null terminator
    strcpy(parameters[rtddn_index].str, "RTDDN");
  }
  else if (parm->rtdsn)
  {
    parameters[rtdsn_index].len = 44 + 1; // max dsname length is 44 + 1 for the null terminator
    strcpy(parameters[rtdsn_index].str, "RTDSN");
  }
  else
  {
    msg_index--;
    input_parameters--;
  }
  parameters[msg_index].len = sprintf(parameters[msg_index].str, "%s", "MSG");

  int index = 0;

  // assign all response parameter stem values
  for (int i = input_parameters; i < MSG_ENTRIES + input_parameters; i++)
  {
    parameters[i].len = RET_ARG_MAX_LEN - sizeof(parameters[i].len);
    sprintf(parameters[i].str, "MSG.%d", ++index);
  }

  // build a contiguous list of pointers to all parameters
  BPXWDYN_RET_ARG *PTR32 parms[MSG_ENTRIES + INPUT_PARAMETERS] = {0};
  for (int i = 0; i < MSG_ENTRIES + input_parameters; i++)
  {
    parms[i] = &parameters[i];
  }

  // set the high bit on last parm
  parms[MSG_ENTRIES + input_parameters - 1] = (void *PTR32)((unsigned int)parms[MSG_ENTRIES + input_parameters - 1] | 0x80000000);

  // NOTE(Kelosky): to prevent the compiler optimizer from discarding any memory assignments,
  // we need to ensure a reference to all data is passed to this external function
  rc = dynalloc(
      parms[0],
      parms[1],
      parms[2],
      parms[3],
      parms[4],
      parms[5],
      parms[6],
      parms[7],
      parms[8],
      parms[9],
      parms[10],
      parms[11],
      parms[12],
      parms[13],
      parms[14],
      parms[15],
      parms[16],
      parms[17],
      parms[18],
      parms[19],
      parms[20],
      parms[21],
      parms[22],
      parms[23],
      parms[24],
      parms[25],
      parms[26],
      parms[27]); // last parameter index is MSG_ENTRIES + INPUT_PARAMETERS - 1

  response->code = rc;

  delete_module("BPXWDY2");

  // obtain any messages returned
  char *respp = response->response;
  for (int i = 0, j = atoi(parameters[msg_index].str); i < j && i < MSG_ENTRIES + input_parameters; i++)
  {
    // if we have messages but the message length is set to the original max length, return failure
    if (parameters[i + input_parameters].len == RET_ARG_MAX_LEN - (int)sizeof(parameters[i + input_parameters].len))
    {
      return (0 != rc) ? ZUT_BPXWDYN_SERVICE_FAILURE : RTNCD_SUCCESS;
    }
    // otherwise, append the message to the response
    size_t remaining = sizeof(response->response) - (respp - response->response);
    if (remaining <= 1)
      break;
    size_t write_len = parameters[i + input_parameters].len;
    if (write_len > remaining - 2)
      write_len = remaining - 2;
    int len = sprintf(respp, "%.*s\n", (int)write_len, parameters[i + input_parameters].str);
    respp = respp + len;
  }

  if (parm->rtdd)
  {
    strcpy(response->ddname, parameters[rtddn_index].str);
  }
  else if (parm->rtdsn)
  {
    strcpy(response->dsname, parameters[rtdsn_index].str);
  }

  return (0 != rc) ? ZUT_BPXWDYN_SERVICE_FAILURE : RTNCD_SUCCESS;
}

typedef struct symbfp SYMBFP;
typedef int (*ASASYMBF)(SYMBFP) ATTRIBUTE(amode31);
#pragma prolog(ZUTSYMBP, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTSYMBP, " ZWEEPILG ")
// symbol examples: https://www.ibm.com/docs/en/zos/3.1.0?topic=symbols-static-system
// similar tool: https://www.ibm.com/docs/en/zos/3.1.0?topic=descriptions-sysvar-display-static-system-symbols
// asasymbf callable service: https://www.ibm.com/docs/en/zos/3.1.0?topic=service-calling-asasymbm-asasymbf
int ZUTSYMBP(SYMBOL_DATA *data)
{
  int rc = 0;

  SYMBFP parms = {0};

  unsigned char work_area[1024] = {0};

  parms.symbfppatternaddr = (void *PTR32)data->input;
  parms.symbfppatternlength = (int)strlen(data->input);

  parms.symbfptargetaddr = data->output;
  parms.symbfptargetlengthaddr = &data->length;

  parms.symbfpreturncodeaddr = &rc;

  parms.symbfpworkareaaddr = work_area;

  ASASYMBF substitute = (ASASYMBF)load_module31("ASASYMBF");
  substitute(parms);

  delete_module("ASASYMBF");
  return rc;
}

typedef int (*ISRSUPC)(void *) ATTRIBUTE(amode31);
#pragma prolog(ZUTSRCH, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTSRCH, " ZWEEPILG ")
typedef struct
{
  short len;
  char parms[100];
} PARMS;

typedef struct
{
  unsigned int params[5];
} PARAM_CHAIN31;

typedef struct
{
  unsigned long params[5];
} PARAM_CHAIN64;

int ZUTSRCH(const char *parms)
{
  int rc = 0;

  PARMS p = {0};
  p.len = sprintf(p.parms, "%.*s", (int)(sizeof(p.parms) - 1), parms);

  ISRSUPC search = (ISRSUPC)load_module31("ISRSUPC");
  rc = search(&p);
  delete_module("ISRSUPC");

  return rc;
}

#pragma prolog(ZUTRUN, " ZWEPROLG NEWDSA=(YES,16),LOC24=YES ")
#pragma epilog(ZUTRUN, " ZWEEPILG ")
typedef int (*PGM64)(void *) ATTRIBUTE(amode64);
typedef int (*PGM31)(void *) ATTRIBUTE(amode31);

int ZUTRUN(ZDIAG *diag, const char *program, const char *parms)
{
  int rc = 0;

  PARMS pstruct = {0};
  if (parms)
  {
    pstruct.len = sprintf(pstruct.parms, "%.*s", (int)(sizeof(pstruct.parms) - 1), parms);
  }

  PARMS *pptr = &pstruct;

  char name_truncated[8 + 1] = {0};
  memset(name_truncated, ' ', sizeof(name_truncated) - 1);                                                                      // pad with spaces
  memcpy(name_truncated, program, strlen(program) > sizeof(name_truncated) - 1 ? sizeof(name_truncated) - 1 : strlen(program)); // truncate

  void *p = load_module(name_truncated);
  if (p)
  {

    long long unsigned int ifunction = (long long unsigned int)p;

    if (ifunction & 0x00000000000000001ULL) // 64-bit
    {
      ifunction &= 0xFFFFFFFFFFFFFFFEULL; // clear low (AMODE-64) bit
      PGM64 p64 = (PGM64)ifunction;
      rc = p64(pptr);
    }
    else if (ifunction & 0x0000000080000000ULL) // 31-bit
    {
      ifunction &= 0x000000007FFFFFFFULL; // clear high bit
      PGM31 p31 = (PGM31)ifunction;
      rc = p31(pptr);
    }
    else // 24-bit
    {
      // AMODE 24: route through the 24-bit shim, which enters the program in
      // its own AMODE (from the entry-point bit) and returns correctly to this
      // above-the-line caller.
      unsigned int plist[1];
      plist[0] = (unsigned int)(uintptr_t)pptr | 0x80000000U; // single parm + VL bit
      rc = zutm1call24((unsigned int)ifunction, plist);
    }
  }
  else
  {
    ZDIAG_SET_MSG(diag, "Load failure for program '%.8s', not found", name_truncated);
    diag->detail_rc = ZUT_RTNCD_LOAD_FAILURE;
    return RTNCD_FAILURE;
  }

  delete_module(name_truncated);

  return rc;
}

typedef struct
{
  short len;
  char parms[100];
} EDSCT_PARMS;

typedef int (*CCNEDSCT)(EDSCT_PARMS *) ATTRIBUTE(amode31);
#pragma prolog(ZUTEDSCT, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTEDSCT, " ZWEEPILG ")
int ZUTEDSCT()
{
  int rc = 0;

  CCNEDSCT convert = (CCNEDSCT)load_module31("CCNEDSCT");
  EDSCT_PARMS p = {0};
  p.len = sprintf(p.parms, "%.100s", "PPCOND,EQUATE(DEF),BITF0XL,HDRSKIP,UNIQ,LP64,LEGACY,SECT(ALL)");
  rc = convert(&p);
  delete_module("CCNEDSCT");
  return rc;
}

#pragma prolog(ZUTMAPFQ, " ZWEPROLG NEWDSA=(YES,8) ")
#pragma epilog(ZUTMAPFQ, " ZWEEPILG ")
int ZUTMAPFQ(ZDIAG *diag, struct apfhdr *answer, int *answer_len, int *rsn) //, int *num_dsns, PARMLIB_DSNS *dsns)
{
  int rsn31 = 0;
  int rc = 0;
  int answer_len31 = *answer_len;

  rc = zutm1apf(answer, &answer_len31, &rsn31);
  *rsn = (rsn31 & 0x0000FFFF);

  if (0 != rc)
  {
    ZDIAG_SET_MSG(diag, "CSVAPF failed with return code '%d' and reason code '%x'", rc, *rsn);
    diag->detail_rc = ZUT_RTNCD_SERVICE_FAILURE;
    diag->service_rc = rc;
    diag->service_rsn = *rsn;
    return RTNCD_FAILURE;
  }

  return rc;
}

#pragma prolog(ZUTMDYNQ, " ZWEPROLG NEWDSA=(YES,8) ")
#pragma epilog(ZUTMDYNQ, " ZWEEPILG ")
int ZUTMDYNQ(ZDIAG *diag, DLAAHDR *answer, int *answer_len, int *rsn)
{
  int rsn31 = 0;
  int rc = 0;
  int answer_len31 = *answer_len;

  rc = zutm1dyn(answer, &answer_len31, &rsn31);
  *rsn = (rsn31 & 0x0000FFFF);

  if (0 != rc)
  {
    if (csvdynlrc___warn == rc)
    {
      *rsn = csvdynlrsnnotalldatareturned;
    }
    else
    {
      ZDIAG_SET_MSG(diag, "CSVDYNL failed with return code '%d' and reason code '%x'", rc, *rsn);
      diag->detail_rc = ZUT_RTNCD_SERVICE_FAILURE;
      diag->service_rc = rc;
      diag->service_rsn = *rsn;
    }
    return RTNCD_FAILURE;
  }

  return rc;
}

#pragma prolog(ZUTMLPLB, " ZWEPROLG NEWDSA=(YES,128) ") // TODO(Kelosky): ensure this is large enough
#pragma epilog(ZUTMLPLB, " ZWEEPILG ")
int ZUTMLPLB(ZDIAG *diag, int *num_dsns, PARMLIB_DSNS *dsns)
{
  int rc = 0;

  ZDIAG diag31 = {0};
  int num_dsns31 = 0;
  PARMLIB_DSNS dsns31 = {0};

  rc = zutm1lpl(&diag31, &num_dsns31, &dsns31);

  memcpy(dsns->dsn, &dsns31.dsn, (size_t)num_dsns31 * sizeof(dsns31.dsn[0]));

  memcpy(diag, &diag31, sizeof(ZDIAG));
  *num_dsns = num_dsns31;

  return rc;
}

// NOTE(Kelosky): this is unused in favor of `getlogin()` but retained for other usages of IAZXJSAB
#pragma prolog(ZUTMGUSR, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTMGUSR, " ZWEEPILG ")
int ZUTMGUSR(char user[8])
{
  char user31[8] = {0};
  int rc = zutm1gur(user31);

  if (0 != rc)
    return rc;

  memcpy(user, user31, sizeof(user31));
  return 0;
}

#pragma prolog(ZUTMFR64, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTMFR64, " ZWEEPILG ")
int ZUTMFR64(void *PTR64 data)
{
  storage_free64(data);
  return 0;
}

#pragma prolog(ZUTMGT64, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTMGT64, " ZWEEPILG ")
int ZUTMGT64(void **PTR64 data, int *len)
{
  *data = storage_get64(*len);
  return 0;
}

#pragma prolog(ZUTMGKEY, " ZWEPROLG NEWDSA=(YES,1) ")
#pragma epilog(ZUTMGKEY, " ZWEEPILG ")
unsigned char ZUTMGKEY()
{
  return get_key();
}

#pragma prolog(ZUTSSIQ, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTSSIQ, " ZWEEPILG ")
int ZUTSSIQ(ZDIAG *diag, JQRY_HEADER **area, const char *filter)
{
  int rc = 0;
  JQRY_HEADER *PTR32 area31 = NULL;
  int rsn = 0;
  rc = iefssi_query(&area31, &rsn, filter);
  if (0 != rc)
  {
    ZDIAG_SET_MSG(diag, "IEFSSI_QUERY rc was: '%d', RSN was: '%d'", rc, rsn);
    diag->detail_rc = ZUT_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }
  *area = area31;
  return rc;
}

#pragma prolog(ZUTNOAUT, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTNOAUT, " ZWEEPILG ")
int ZUTNOAUT()
{
  if (0 != test_auth())
  {
    return RTNCD_SUCCESS; // not APF-authorized; nothing to relinquish
  }

  int rc = RTNCD_SUCCESS;
  ZRCVY_ENV zenv = {0};

  if (0 == enable_recovery(&zenv))
  {
    rc = auth_off();
  }
  else
  {
    rc = RTNCD_FAILURE; // abend in elevated window; retry restored problem state & key
  }

  disable_recovery(&zenv);

  // fail closed - confirm authorization was actually relinquished
  if (RTNCD_SUCCESS == rc && 0 == test_auth())
  {
    rc = RTNCD_FAILURE;
  }

  return rc;
}

#pragma prolog(ZUTMSREL, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTMSREL, " ZWEEPILG ")
int ZUTMSREL(int *size, void *data)
{
  storage_release(*size, data);
  return 0;
}

#pragma prolog(ZUTCVTD, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTCVTD, " ZWEEPILG ")
int ZUTCVTD(const char *ptr, char *time)
{
  TIME_STRUCT time_in = {0};
  memcpy(&time_in.date, ptr, 4);
  unsigned long long tod = 0;
  int rc = convtod(&time_in, &tod);

  if (0 != rc)
    return rc;

  TIME_STRUCT time_out = {0};
  rc = stckconv(&tod, &time_out);

  if (0 == rc)
  {
    sprintf(time, "%02x/%02x/%02x%02x",
            time_out.date.mmddyyyy.month,
            time_out.date.mmddyyyy.day,
            time_out.date.mmddyyyy.year[0],
            time_out.date.mmddyyyy.year[1]);
  }
  return rc;
}

struct idcamsDdNameList
{
  unsigned short len;
  char _unused[32];
  char sysin[8];
  char sysprint[8];
};
typedef int (*IDCAMS)(unsigned int, unsigned int) ATTRIBUTE(amode31);
#pragma prolog(ZUTIDCAM, " ZWEPROLG NEWDSA=(YES,4) ")
#pragma epilog(ZUTIDCAM, " ZWEEPILG ")
int ZUTIDCAM(const char *sysinDdName, const char *sysprintDdName)
{
  int rc = 0;

  unsigned short options = 0;
  struct idcamsDdNameList ddNameList = {0};
  ddNameList.len = sizeof(ddNameList._unused) + sizeof(ddNameList.sysin) + sizeof(ddNameList.sysprint);

  // DD name replacements should be padded with blanks
  memset(ddNameList.sysin, ' ', sizeof(ddNameList.sysin));
  memset(ddNameList.sysprint, ' ', sizeof(ddNameList.sysprint));
  memcpy(ddNameList.sysin, sysinDdName,
         strlen(sysinDdName) > sizeof(ddNameList.sysin) ? sizeof(ddNameList.sysin) : strlen(sysinDdName));
  memcpy(ddNameList.sysprint, sysprintDdName,
         strlen(sysprintDdName) > sizeof(ddNameList.sysprint) ? sizeof(ddNameList.sysprint) : strlen(sysprintDdName));

  char programName[8] = "IDCAMS";
  // IDCAMS must be entered in 31-bit mode. https://www.ibm.com/docs/en/zos/3.1.0?topic=commands-invoking-access-method-services-from-your-program
  IDCAMS idcams = (IDCAMS)load_module31(programName);
  // www.ibm.com/docs/en/zos/3.1.0?topic=instructions-load-call-macro
  // ddnameList is the current last entry so set the high order bit to indicate the last
  unsigned int parm_list[2];
  parm_list[0] = (unsigned int)(uintptr_t)&options;
  parm_list[1] = (unsigned int)(uintptr_t)&ddNameList | 0x80000000U;
  rc = idcams(parm_list[0], parm_list[1]);
  delete_module(programName);

  return rc;
}
