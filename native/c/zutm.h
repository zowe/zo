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

#ifndef ZUTM_H
#define ZUTM_H

#include "ztype.h"
#include "zprmtype.h"
#include "iefjsqry.h"
#include "csvapfaa.h"
#include "csvdlaa.h"

#if defined(__cplusplus) && defined(__MVS__)
extern "OS"
{
#elif defined(__cplusplus)
extern "C"
{
#endif

#define DFSMSdfp_OPT_MAX_LEN 128
#define RET_ARG_MAX_LEN 260
#define MSG_ENTRIES 25

#define ALLOC_STRING_INDEX 0
#define RTDDN_INDEX 1
#define RTDSN_INDEX 1
#define MSG_INDEX 2

#define LAST_PARAMETER_INDEX MSG_INDEX // NOTE(Kelosky): this must be set to the last parameter index

#define INPUT_PARAMETERS LAST_PARAMETER_INDEX + 1

  typedef struct jqry___header JQRY_HEADER;
  typedef struct jqry___subsys___entry JQRY_SUBSYS_ENTRY;
  typedef struct jqry___vt___entry JQRY_VT_ENTRY;

  typedef struct dlaahdr DLAAHDR;
  typedef struct dlaals DLAALS;
  typedef struct dlaads DLAADS;

  typedef struct
  {
    short len;
    char str[DFSMSdfp_OPT_MAX_LEN];
  } DFSMSdfp_OPT_LIST;

  // see https://www.ibm.com/docs/en/SSLTBW_3.1.0/pdf/idau100_v3r1.pdf "ddname List"
  typedef struct
  {

    /* DWORD alignment */
    char _padding[6];
    /* 2 byte length: measures from start of dd section to end */
    uint16_t TotalLength;

    /* --- DD LIST --- */
    /* Rows 1-4 explicitly show binary zeros */
    char _unused1[8];
    char _unused2[8];
    char _unused3[8];
    char _unused4[8];

    /* Explicit DDNAME override labels */
    char sysin[8];
    char sysprint[8];

    /* Row explicitly showing binary zeros */
    char _unused5[8];

    /* SYSUT datasets */
    char sysut1[8];
    char sysut2[8];
    char sysut3[8];
    char sysut4[8];

  } DFSMSdfp_DD_LIST;
  typedef struct
  {
    short len;
    char str[RET_ARG_MAX_LEN];
    unsigned int rtdd : 1;
    unsigned int rtdsn : 1;
  } BPXWDYN_RET_ARG;

  typedef BPXWDYN_RET_ARG BPXWDYN_PARM;

  typedef struct
  {
    unsigned int code;
    char response[RET_ARG_MAX_LEN * MSG_ENTRIES + 1];
    char ddname[9];
    char dsname[45]; // NOTE(Kelosky): not implemented yet
  } BPXWDYN_RESPONSE;

  typedef struct
  {
    char input[128];
    char output[128];
    int length;
    int reserve_1;
  } SYMBOL_DATA;

  /**
   * @brief Storage Free 64 bit areas
   * @param PTR64 The data to free
   * @return The return code from the service (always 0)
   */
  int ZUTMFR64(void *PTR64);

  /**
   * @brief Storage Release 24 or 31 bit areas
   * @param size The size of the data to release
   * @param data The data to release
   * @return The return code from the service
   */
  int ZUTMSREL(int *size, void *PTR64);

  /**
   * @brief Storage Get 64 bit areas
   * @param PTR64 The new storage area address
   * @param size The size of the data to get
   * @return The return code from the service (always 0)
   */
  int ZUTMGT64(void **PTR64, int *PTR64);

  typedef enum
  {
    ZUTMSDFP_IEBCOPY = 0,
    ZUTMSDFP_IEBGENER = 1,
  } ZUTMSDFP_UTILITY;

  /**
   * @brief Run DFSMSdfp utilities
   * @param zdiag Pointer to diagnostic structure
   * @param utility The DFSMSdfp utility to run
   * @param opt_list Pointer to DFSMSdfp option list
   * @param dd_list Pointer to DFSMSdfp DD list
   * @return The return code from the service
   */
  int ZUTMSDFP(ZDIAG *, ZUTMSDFP_UTILITY *, DFSMSdfp_OPT_LIST *, DFSMSdfp_DD_LIST *);

  int ZUTMGUSR(char[8]);
  int ZUTWDYN(BPXWDYN_PARM *, BPXWDYN_RESPONSE *);
  int ZUTEDSCT();
  int ZUTSYMBP(SYMBOL_DATA *);
  int ZUTSRCH(const char *);
  int ZUTIDCAM(const char *, const char *);
  int ZUTRUN(ZDIAG *, const char *, const char *);
  unsigned char ZUTMGKEY();

  /**
   * @brief Query the APF list
   * @param diag The diagnostic structure
   * @param answer The answer structure
   * @param answer_len The length of the answer
   * @param rsn The reason code
   * @return The return code from the service
   */
  int ZUTMAPFQ(ZDIAG *, struct apfhdr *, int *, int *);

  /**
   * @brief Query the currently active link list (LNKLST) set via CSVDYNL REQUEST=LIST
   * @param diag The diagnostic structure
   * @param answer The answer area, mapped by the CSVDLAA macro (DLAAHDR)
   * @param answer_len The length of the answer area
   * @param rsn The reason code (csvdynlrsnnotalldatareturned when buffer too small)
   * @return 0 on success, RTNCD_FAILURE otherwise
   */
  int ZUTMDYNQ(ZDIAG *, DLAAHDR *, int *, int *);

  /**
   * @brief List the PLIB datasets
   * @param diag The diagnostic structure
   * @param num_dsns The number of datasets
   * @param dsns The datasets
   * @return The return code from the service
   */
  int ZUTMLPLB(ZDIAG *, int *, PARMLIB_DSNS *);

  int ZUTSSIQ(ZDIAG *, JQRY_HEADER **, const char *filter);
  int ZUTCVTD(const char *ptr, char *time_out);

  /**
   * @brief Relinquish job step APF authorization (turn off JSCBAUTH) under
   * ESTAE-type (IEAARR) recovery. Verifies with TESTAUTH that authorization
   * was actually dropped.
   * @return RTNCD_SUCCESS if the job step is (now) unauthorized,
   * RTNCD_FAILURE if authorization could not be relinquished (callers must
   * fail closed and not run unprivileged work)
   */
  int ZUTNOAUT();

#if defined(__cplusplus)
}
#endif

#endif
