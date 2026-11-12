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

#include <sstream>
#include <string>
#include <string_view>
#include <cstring>
#include <vector>
#include <cctype>
#include <iomanip>
#include <cstdio>
#include <ctime>
#include <utility>
#include <unistd.h>
#include <stdio.h>
#include "iazbtokp.h"
#include "iefzb4d0.h"
#include "iefzb4d2.h"
#include "zds.hpp"
#include "zjb.hpp"
#include "zjbtime.hpp"
#include "zjbm.h"
#include "zssitype.h"
#include "ztype.h"
#include "zut.hpp"
#include "zutm.h"
#include "zjbtype.h"
#include "zdstype.h"
#include "zdyn.h"
#include "dcbd.h"
#include "ihapsa.h"
#include "cvt.h"
#include "zdbg.h"

typedef struct iazbtokp IAZBTOKP;

void zjb_build_job_response(ZJB_JOB_INFO *PTR64, int, std::vector<ZJob> &);

#define BTOKLEN (293 - 254) // 293 is the full length, minus the max optional buffer area for logs (less 254)

// NOTE(Kelosky): see struct __S99struc via 'showinc' compiler option in <stdio.h>
// NOTE(Kelosky): In the future, to allocate the logical SYSLOG concatenation for a system specify the following data set name (in DALDSNAM).
// https://www.ibm.com/docs/en/zos/3.1.0?topic=allocation-specifying-data-set-name-daldsnam
int zjb_read_job_content_by_key(ZJB *zjb, const std::string &jobid, int key, std::string &response)
{
  int rc = 0;
  std::string job_dsn;

  rc = zjb_get_job_dsn_by_key(zjb, jobid, key, job_dsn);
  if (0 != rc)
    return rc;

  return zjb_read_job_content_by_dsn(zjb, job_dsn, response);
}

int zjb_get_job_dsn_by_key(ZJB *zjb, const std::string &jobid, int key, std::string &job_dsn)
{
  int rc = 0;

  std::vector<ZJobDD> list;

  rc = zjb_list_dds(zjb, jobid, list);
  if (0 != rc)
    return rc;

  rc = RTNCD_FAILURE; // assume failure

  for (const auto &dd : list)
  {
    if (key == dd.key)
    {
      job_dsn = dd.dsn;
      memcpy(zjb->token, dd.token, sizeof(dd.token));
      rc = 0;
      break;
    }
  }

  if (0 != rc)
  {
    ZDIAG_SET_MSG(&zjb->diag, "Could not locate data set key '%d' on job '%s'", key, jobid.c_str());
    zjb->diag.detail_rc = ZJB_RTNCD_JOB_DSN_KEY_NOT_FOUND;
    return RTNCD_WARNING;
  }

  return RTNCD_SUCCESS;
}

static int zjb_parse_dsn_for_key(std::string_view dsn_str, std::string &jobid, int &key)
{
  // dsn format: userid.jobname.jobid.Ddskey[.dsname]
  // https://www.ibm.com/docs/en/zos/3.1.0?topic=allocation-specifying-data-set-name-daldsnam

  size_t pos_after_userid = dsn_str.find('.');
  if (pos_after_userid == std::string_view::npos)
    return ZJB_RTNCD_PARSE_ERROR_SIZE;

  size_t pos_after_jobname = dsn_str.find('.', pos_after_userid + 1);
  if (pos_after_jobname == std::string_view::npos)
    return ZJB_RTNCD_PARSE_ERROR_SIZE;

  size_t pos_after_jobid = dsn_str.find('.', pos_after_jobname + 1);
  if (pos_after_jobid == std::string_view::npos)
    return ZJB_RTNCD_PARSE_ERROR_SIZE;

  // get jobid
  jobid = dsn_str.substr(pos_after_jobname + 1, pos_after_jobid - pos_after_jobname - 1);

  // get ddskey
  size_t pos_after_ddskey = dsn_str.find('.', pos_after_jobid + 1);
  std::string_view ddskey = (pos_after_ddskey != std::string_view::npos)
                                ? dsn_str.substr(pos_after_jobid + 1, pos_after_ddskey - pos_after_jobid - 1)
                                : dsn_str.substr(pos_after_jobid + 1);

  // validate key starts with 'D' and has digits after
  if (ddskey.empty() || ddskey[0] != 'D')
  {
    return ZJB_RTNCD_PARSE_ERROR_MALFORMED;
  }

  // get key after 'D' (skip first character)
  std::string_view key_str = ddskey.substr(1);

  // validate 7 chars max
  if (key_str.empty() || key_str.length() > 7)
  {
    return ZJB_RTNCD_PARSE_ERROR_NUMBER_LEN;
  }

  for (char c : key_str)
  {
    if (!std::isdigit(c))
    {
      return ZJB_RTNCD_PARSE_ERROR_NUMBER_CHAR;
    }
  }

  try
  {
    key = std::stoi(std::string(key_str));
  }
  catch (const std::exception &)
  {
    return ZJB_RTNCD_PARSE_ERROR_NUMBER_CONVERSION;
  }

  return 0;
}

int zjb_read_job_jcl(ZJB *zjb, const std::string &jobid, std::string &response)
{
  int rc = 0;

  std::vector<ZJobDD> list;

  rc = zjb_list_dds(zjb, jobid, list);
  if (0 != rc)
  {
    return rc;
  }

  rc = RTNCD_FAILURE; // assume failure

  std::istringstream iss(list[0].dsn);
  std::vector<std::string> args;
  std::string arg;

  while (std::getline(iss, arg, '.'))
  {
    args.push_back(arg);
  }

#define MIN_SIZE 3 // HLQ + next + next

  if (args.size() < MIN_SIZE)
  {
    ZDIAG_SET_MSG(&zjb->diag, "Unexpected data set name '%s' for jobid %s", list[0].dsn.c_str(), jobid.c_str());
    zjb->diag.detail_rc = ZJB_RTNCD_UNEXPECTED_ERROR;
    return RTNCD_FAILURE;
  }

  std::string jcl_dsn = args[0] + "." + args[1] + "." + args[2] + ".JCL";

  return zjb_read_job_content_by_dsn(zjb, jcl_dsn, response);
}

#define NUM_TEXT_UNITS 5

// Disable optimization for this function since `ibm-clang` -O2 removes required assignments.
__attribute__((optnone)) static int zjb_read_job_dynamic_allocation(ZJB *zjb, std::string jobdsn, std::string &ddname)
{
  int rc = 0;

  unsigned char *p = nullptr;

  unsigned char token[80] = {0};
  bool use_stvsctkn = false;
  if (0 != memcmp(zjb->token, token, sizeof(token)))
  {
    use_stvsctkn = true;
  }

  // calculate total size needed, obtain, & clear
  int total_size_needed = sizeof(IAZBTOKP) + (sizeof(S99TUNIT_X) * NUM_TEXT_UNITS) + (sizeof(S99TUPL) * NUM_TEXT_UNITS) + sizeof(__S99parms) + sizeof(__S99rbx_t) + sizeof(token);
  unsigned char *parms = (unsigned char *)__malloc31(total_size_needed);
  if (parms == nullptr)
  {
    ZDIAG_SET_MSG(&zjb->diag, "Failed to allocate 31-bit memory for job parms when reading %s", jobdsn.c_str());
    return RTNCD_FAILURE;
  }
  memset(parms, 0x00, total_size_needed);

  // carve up storage to needed structs
  IAZBTOKP *PTR32 iazbtokp = (IAZBTOKP * PTR32) parms;
  S99TUNIT_X *PTR32 s99tunit_x = (S99TUNIT_X * PTR32)(parms + sizeof(IAZBTOKP));
  S99TUPL *PTR32 s99tupl = (S99TUPL * PTR32)(parms + sizeof(IAZBTOKP) + (sizeof(S99TUNIT_X) * NUM_TEXT_UNITS));
  __S99parms *PTR32 s99parms = (__S99parms * PTR32)(parms + sizeof(IAZBTOKP) + (sizeof(S99TUNIT_X) * NUM_TEXT_UNITS) + (sizeof(S99TUPL) * NUM_TEXT_UNITS));
  __S99rbx_t *PTR32 s99parmsx = (__S99rbx_t * PTR32)(parms + sizeof(IAZBTOKP) + (sizeof(S99TUNIT_X) * NUM_TEXT_UNITS) + (sizeof(S99TUPL) * NUM_TEXT_UNITS) + sizeof(__S99parms));
  unsigned char *PTR32 token31 = (unsigned char *PTR32)(parms + sizeof(IAZBTOKP) + (sizeof(S99TUNIT_X) * NUM_TEXT_UNITS) + (sizeof(S99TUPL) * NUM_TEXT_UNITS) + sizeof(__S99parms) + sizeof(__S99rbx_t));

  if (use_stvsctkn)
  {
    memcpy(token31, zjb->token, sizeof(zjb->token));
  }

  // https://www.ibm.com/docs/en/zos/3.1.0?topic=allocation-building-browse-token-dalbrtkn
  short int len = sizeof(iazbtokp->btokid);
  memcpy(iazbtokp->btokpl1, &len, sizeof(len));
  memcpy(iazbtokp->btokid, "BTOK", sizeof(iazbtokp->btokid));
  len = sizeof(iazbtokp->btokver);
  memcpy(iazbtokp->btokpl2, &len, sizeof(len));

  if (use_stvsctkn)
  {
    iazbtokp->btoktype = btokstkn;
  }
  else
  {
    iazbtokp->btoktype = btokbrws;
  }
  iazbtokp->btokvers = btokvrnm;
  len = sizeof(iazbtokp->btokiotp);

  if (use_stvsctkn)
  {
    memcpy(&iazbtokp->btokiotp[0], &token31, sizeof(token31));
  }

  memcpy(iazbtokp->btokpl3, &len, sizeof(len));
  len = sizeof(iazbtokp->btokjkey);
  memcpy(iazbtokp->btokpl4, &len, sizeof(len));
  len = sizeof(iazbtokp->btokasid);
  memcpy(iazbtokp->btokpl5, &len, sizeof(len));
  memset(iazbtokp->btokasid, 0xFF, sizeof(iazbtokp->btokasid));
  len = sizeof(iazbtokp->btokrcid);
  memcpy(iazbtokp->btokpl6, &len, sizeof(len));
  len = sizeof(iazbtokp->btoklogs);
  memcpy(iazbtokp->btokpl7, &len, sizeof(len));

  // --- set text units

  short int plen = 0;
  short int dynkey = 0;
  short int numparms = 1;
  int i = -1;

  i++;
  dynkey = daldsnam;
  numparms = 1;
  plen = strlen(jobdsn.c_str());
  memcpy(s99tunit_x[i].s99tunit.s99tukey, &dynkey, sizeof(dynkey));
  memcpy(s99tunit_x[i].s99tunit.s99tunum, &numparms, sizeof(numparms));
  memcpy(s99tunit_x[i].s99tunit.s99tulng, &plen, sizeof(plen));
  memcpy(&s99tunit_x[i].s99tunit.s99tupar, jobdsn.c_str(), plen);

  i++;
  dynkey = dalstats;
  numparms = 1;
  plen = 1;
  memcpy(s99tunit_x[i].s99tunit.s99tukey, &dynkey, sizeof(dynkey));
  memcpy(s99tunit_x[i].s99tunit.s99tunum, &numparms, sizeof(numparms));
  memcpy(s99tunit_x[i].s99tunit.s99tulng, &plen, sizeof(plen));
  s99tunit_x[i].s99tunit.s99tupar = DISP_SHR;

  i++;
  dynkey = dalbrtkn;
  numparms = 7; // "# must be 7" // https://www.ibm.com/docs/en/zos/3.1.0?topic=njdaf-spool-data-set-browse-token-specification-key-006e
  plen = BTOKLEN;
  memcpy(s99tunit_x[i].s99tunit.s99tukey, &dynkey, sizeof(dynkey));
  memcpy(s99tunit_x[i].s99tunit.s99tunum, &numparms, sizeof(numparms));
  memcpy(s99tunit_x[i].s99tunit.s99tulng, &plen, sizeof(plen)); //
  memcpy(s99tunit_x[i].s99tunit.s99tulng, iazbtokp, BTOKLEN);   // NOTE(Kelosky): not using s99tupar for data, iazbtokp starts with half word of the "FIRST" parm.

  i++;
  dynkey = daluassr;
  numparms = 0;
  plen = 0;
  memcpy(s99tunit_x[i].s99tunit.s99tukey, &dynkey, sizeof(dynkey));
  memcpy(s99tunit_x[i].s99tunit.s99tunum, &numparms, sizeof(numparms));
  memcpy(s99tunit_x[i].s99tunit.s99tulng, &plen, sizeof(plen));

  i++;
  dynkey = dalrtddn;
  numparms = 1;
  plen = 8;
  memcpy(s99tunit_x[i].s99tunit.s99tukey, &dynkey, sizeof(dynkey));
  memcpy(s99tunit_x[i].s99tunit.s99tunum, &numparms, sizeof(numparms));
  memcpy(s99tunit_x[i].s99tunit.s99tulng, &plen, sizeof(plen));

  // these need to be contiguous and can point to non contiguous storage
  for (int j = 0; j <= i; j++)
  {
    s99tupl[j].s99tuptr = (void *PTR32) & s99tunit_x[j];
  }

  // set high bit in last entry
  p = (unsigned char *PTR32) & s99tupl[i].s99tuptr;
  *p = *p | s99tupln;

  memcpy(s99parmsx->__S99EID, "S99RBX", sizeof(s99parmsx->__S99EID));
  s99parmsx->__S99EVER = s99rbxvr;
  s99parmsx->__S99EOPTS = s99parmsx->__S99EOPTS | s99ermsg; // IEFDB476 can free message blocks
  s99parmsx->__S99ESUBP = 0;
  s99parmsx->__S99EMGSV = s99parmsx->__S99EMGSV | s99xinfo;

  // --- set rb

  s99parms->__S99RBLN = sizeof(__S99parms);
  s99parms->__S99VERB = s99vrbal; // allocation
  s99parms->__S99FLAG1 = 0x4000;  // s99nocnv;
  s99parms->__S99TXTPP = s99tupl;

  // s99parms->__S99S99X = s99parmsx; // TODO(Kelosky): reenable when we look at s99parmsx->__S99ENMSG and free

  // https://www.ibm.com/docs/en/zos/3.1.0?topic=list-coding-dynamic-allocation-request
  // https://www.ibm.com/docs/en/zos/3.1.0?topic=guide-dynamic-allocation
  rc = svc99(s99parms);

  // TODO(Kelosky): parse s99parmsx->__S99ENMSG and free
  if (0 != rc && 0 != s99parms->__S99ERROR)
  {
    strcpy(zjb->diag.service_name, "svc99");
    zjb->diag.service_rc = rc;
    ZDIAG_SET_MSG(&zjb->diag, "Could not allocate job spool file '%s', rc: '%d' s99error: '%d' s99info: '%d'", jobdsn.c_str(), rc, s99parms->__S99ERROR, s99parms->__S99INFO);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    zjb->diag.service_rsn = s99parms->__S99ERROR;
    zjb->diag.service_rsn_secondary = s99parms->__S99INFO;
    free(parms);
    return RTNCD_FAILURE;
  }

  short ddnamelen = 0;
  memcpy(&ddnamelen, s99tunit_x[4].s99tunit.s99tulng, sizeof(ddnamelen));

  char cddname[8 + 1] = {0};
  memcpy(cddname, &s99tunit_x[4].s99tunit.s99tupar, ddnamelen);
  ddname = std::string(cddname);

  free(parms);

  return rc;
}

static int zjb_free_job_dynamic_allocation(ZJB *zjb, std::string ddname)
{
  int rc = 0;

  // free DD
  __dyn_t ip;
  dyninit(&ip);
  ip.__ddname = (char *)ddname.c_str(); // e.g. SYS00001
  rc = dynfree(&ip);

  if (0 != rc && 0 == zjb->diag.e_msg_len) // only set error if no error message was already set
  {
    strcpy(zjb->diag.service_name, "dynfree");
    zjb->diag.service_rc = rc;
    ZDIAG_SET_MSG(&zjb->diag, "dynfree failed with %d", rc);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  return rc;
}

int zjb_read_syslog(ZJB *zjb, ZJBSyslogOptions &opts, ZJBSyslogResponse &response)
{
  int rc = 0;
  std::string ddname;
  ZDS zds = {};
  response.has_more = false;
  response.end_date.clear();
  response.end_time.clear();
  response.returned_lines = 0;

  //
  // get system name
  //
  struct psa *psa = (struct psa *)0;
  struct cvt *cvt = (struct cvt *)psa->flccvt;
  char *sysname_char = (char *)cvt->cvtsname;
  std::string sysname_str = std::string(sysname_char, sizeof(cvt->cvtsname));
  std::string dsn = zut_rtrim(sysname_str) + ".SYSLOG" + ".SYSTEM"; // https://www.ibm.com/docs/en/zos/3.1.0?topic=allocation-specifying-data-set-name-daldsnam

  //
  // get input timestamp from format HH:MM:SS.CC to binary format (low bit represents 0.01 of a second) for CONVTOD
  //
  int hh = 0, mm = 0, ss = 0, cs = 0;
  sscanf(opts.time.c_str(), "%d:%d:%d.%d", &hh, &mm, &ss, &cs);
  uint32_t ts_binary = ((uint32_t)hh * 360000) + ((uint32_t)mm * 6000) + ((uint32_t)ss * 100) + (uint32_t)cs;

  //
  // subtract to convert local time -> UTC for CONVTOD (which uses default offset=0/UTC)
  //
  int32_t tz_offset_cs = (int32_t)((double)cvt->cvttz * 1.048576 / 0.01); // documented in cvt.h
  ts_binary -= (uint32_t)tz_offset_cs;
  memcpy(&zds.ts_binary, &ts_binary, sizeof(ts_binary)); // low half is unused

  //
  // prepare date for `pack`
  //
  std::string date_compact;
  for (char c : opts.date)
  {
    if (c != '-')
      date_compact += c;
  }
  memcpy(&zds.ebcdic_date, date_compact.c_str(), sizeof(zds.ebcdic_date));
  zds.max_lines = opts.max_lines;

  rc = zjb_read_job_dynamic_allocation(zjb, dsn, ddname);
  if (0 != rc)
  {
    return rc;
  }

  zds.encoding_opts.data_type = zjb->encoding_opts.data_type;
  memcpy((void *)&zds.encoding_opts.codepage, (const void *)&zjb->encoding_opts.codepage, sizeof(zjb->encoding_opts.codepage));

  rc = zds_read_vsam(&zds, ddname, response.data);
  memcpy(&zjb->diag, &zds.diag, sizeof(ZDIAG));
  response.has_more = (zds.has_more != 0);
  response.returned_lines = zds.returned_lines;

  if (rc == 0 && !response.data.empty())
  {
    // Undo UTC->local in signed arithmetic; uint32 wraparound doesn't align to a day boundary.
    const int32_t CS_PER_DAY = 24 * 360000;
    uint32_t utc_cs = 0;
    memcpy(&utc_cs, &zds.ts_binary, sizeof(utc_cs));
    int32_t local_cs = (int32_t)utc_cs + tz_offset_cs;

    // Normalize into [0, CS_PER_DAY) and track midnight crossings for the date adjustment.
    int day_shift = 0;
    while (local_cs < 0)
    {
      local_cs += CS_PER_DAY;
      day_shift--;
    }
    while (local_cs >= CS_PER_DAY)
    {
      local_cs -= CS_PER_DAY;
      day_shift++;
    }

    unsigned end_hh = local_cs / 360000;
    unsigned end_mm = (local_cs / 6000) % 60;
    unsigned end_ss = (local_cs / 100) % 60;

    char ebcdic_date_formatted[8 + 1 + 1 + 1] = {}; // date + hyphens + null
    if (day_shift != 0)
    {
      // mktime normalizes the out-of-range day across month/year/leap-year boundaries.
      int year = 0, month = 0, day = 0;
      sscanf((const char *)zds.ebcdic_date, "%4d%2d%2d", &year, &month, &day);
      struct tm t = {};
      t.tm_year = year - 1900;
      t.tm_mon = month - 1;
      t.tm_mday = day + day_shift;
      t.tm_hour = 12; // noon keeps any DST/boundary effects away from the date
      mktime(&t);
      snprintf(ebcdic_date_formatted, sizeof(ebcdic_date_formatted), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }
    else
    {
      snprintf(ebcdic_date_formatted, sizeof(ebcdic_date_formatted), "%.4s-%.2s-%.2s", zds.ebcdic_date, 4 + zds.ebcdic_date, 6 + zds.ebcdic_date);
    }
    response.end_date = std::string(ebcdic_date_formatted);
    // NOTE(Kelosky): centiseconds is not used for the end time because it cannot be used for input
    // unsigned end_cs = local_cs % 100U;
    char end_time_buf[16] = {};
    snprintf(end_time_buf, sizeof(end_time_buf), "%02u:%02u:%02u", end_hh, end_mm, end_ss);
    response.end_time = end_time_buf;
  }

  int newrc = zjb_free_job_dynamic_allocation(zjb, ddname);
  if (0 != newrc && 0 == rc)
  {
    return newrc;
  }

  return rc;
}

int zjb_read_job_content_by_dsn(ZJB *zjb, const std::string &dsn, std::string &response)
{
  int rc = 0;
  std::string ddname;
  ZDS zds = {};

  std::string parsed_jobid;
  int parsed_key = 0;
  bool dd_is_asa = false;

  // parse dsn for jobid and key - if found, attempt to get the token for the dsn
  memset(zjb->token, 0, sizeof(zjb->token));
  int parse_rc = zjb_parse_dsn_for_key(dsn, parsed_jobid, parsed_key);
  if (0 == parse_rc)
  {
    // list all dsns on jobid
    std::vector<ZJobDD> job_dds;
    rc = zjb_list_dds(zjb, parsed_jobid, job_dds);
    if (0 != rc)
    {
      return rc;
    }

    // verify the dsn is on the jobid
    bool found = false;
    for (const auto &dd : job_dds)
    {
      std::string dd_dsn_trimmed = dd.dsn;
      std::string dsn_trimmed = dsn;
      if (zut_trim(dd_dsn_trimmed) == zut_trim(dsn_trimmed))
      {
        ddname = dd.dsn;
        memcpy(zjb->token, dd.token, sizeof(dd.token));
        dd_is_asa = dd.is_asa;
        found = true;
        break;
      }
    }

    if (!found)
    {
      strcpy(zjb->diag.service_name, "zjb_list_dds");
      zjb->diag.service_rc = ZJB_RTNCD_JOB_DSN_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "Spool read DSN not found in job '%s'", parsed_jobid.c_str());
      return RTNCD_FAILURE;
    }
  }

  rc = zjb_read_job_dynamic_allocation(zjb, dsn, ddname);
  if (0 != rc)
  {
    return rc;
  }

  zds.encoding_opts.data_type = zjb->encoding_opts.data_type;
  memcpy((void *)&zds.encoding_opts.codepage, (const void *)&zjb->encoding_opts.codepage, sizeof(zjb->encoding_opts.codepage));
  memcpy((void *)&zds.encoding_opts.source_codepage, (const void *)&zjb->encoding_opts.source_codepage, sizeof(zjb->encoding_opts.source_codepage));

  ZDSReadOpts read_opts{.zds = &zds, .ddname = ddname, .dsname = dsn, .is_asa = dd_is_asa};
  rc = zds_read(read_opts, response);
  memcpy(&zjb->diag, &zds.diag, sizeof(ZDIAG));

  int newrc = zjb_free_job_dynamic_allocation(zjb, ddname);
  if (0 != newrc && 0 == rc)
  {
    return newrc;
  }

  return rc;
}

int zjb_wait(ZJB *zjb, const std::string &status)
{
  int rc = 0;
  ZJob job{};
  std::string jobid(zjb->jobid, sizeof(zjb->jobid));
  const auto waiting_for_active = status == "ACTIVE";

  do
  {
    rc = zjb_view(zjb, jobid, job);

    sleep(1); // TODO(Kelosky): make this interval smaller

    if (0 != rc)
    {
      return RTNCD_FAILURE;
    }

    // When waiting for ACTIVE, accept OUTPUT as a valid completion state
    // (Job may complete before the waiting logic gets to it)
    if (waiting_for_active && job.status.find("OUTPUT") != std::string::npos)
    {
      return RTNCD_SUCCESS;
    }

  } while (job.status != status);
  return RTNCD_SUCCESS;
}

int zjb_delete(ZJB *zjb, const std::string &jobid)
{
  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));
  return ZJBMPRG(zjb);
}

int zjb_cancel(ZJB *zjb, const std::string &jobid)
{
  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));
  return ZJBMCNL(zjb, 0);
}

int zjb_hold(ZJB *zjb, const std::string &jobid)
{
  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));
  return ZJBMHLD(zjb);
}

int zjb_release(ZJB *zjb, const std::string &jobid)
{
  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));
  return ZJBMRLS(zjb);
}

int zjb_submit_dsn(ZJB *zjb, const std::string &dsn, std::string &jobid)
{
  ZDS zds{};
  ZDSReadOpts read_opts{.zds = &zds, .dsname = dsn};
  std::string contents;
  const auto rc = zds_read(read_opts, contents);
  if (0 != rc)
  {
    memcpy(&zjb->diag, &zds.diag, sizeof(ZDIAG));
    return rc;
  }

  return zjb_submit(zjb, contents, jobid);
}

int zjb_submit(ZJB *zjb, const std::string &contents, std::string &jobid)
{
  int rc = 0;
  ZDS zds = {0};

  __dyn_t ip;
  rc = dyninit(&ip);
  if (0 != rc)
  {
    strcpy(zjb->diag.service_name, "dyninit");
    zjb->diag.service_rc = rc;
    ZDIAG_SET_MSG(&zjb->diag, "dyninit failed with %d", rc);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  std::string ddname = "????????"; // system generated DD name
  ip.__ddname = (char *)ddname.c_str();
  std::string intrdr = "INTRDR  ";
  ip.__sysoutname = (char *)intrdr.c_str(); // https://www.ibm.com/docs/en/zos/3.1.0?topic=control-destination-internal-reader && https://www.ibm.com/docs/en/zos/3.1.0?topic=programming-internal-reader-facility
  ip.__lrecl = 80;
  ip.__blksize = 80;
  ip.__sysout = __DEF_CLASS;
  ip.__recfm = _FB_;

  rc = dynalloc(&ip);

  if (0 != rc)
  {
    strcpy(zjb->diag.service_name, "dynalloc");
    zjb->diag.service_rc = rc;
    ZDIAG_SET_MSG(&zjb->diag, "dynalloc failed with %d", rc);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  ZDSWriteOpts write_opts{.zds = &zds, .ddname = ddname};
  rc = zds_write(write_opts, contents);
  if (0 != rc)
  {
    memcpy(&zjb->diag, &zds.diag, sizeof(ZDIAG));
    dynfree(&ip);
    return rc;
  }

  char cjobid[8 + 1] = {0};
  // https://www.ibm.com/docs/en/zos/3.1.0?topic=iazsymbl-jes-system-symbols
  int jobid_size = sizeof(cjobid);
  rc = ZJBSYMB(zjb, "SYS_LASTJOBID", cjobid, &jobid_size);

  if (0 != rc)
  {
    dynfree(&ip);
    return rc;
  }

  jobid = std::string(cjobid);

  if (jobid == "")
  {
    rc = dynfree(&ip);
    strcpy(zjb->diag.service_name, "intrdr");
    ZDIAG_SET_MSG(&zjb->diag, "job submission failed");
    zjb->diag.detail_rc = ZJB_RTNCD_SUBMIT_ERROR;
    return RTNCD_FAILURE;
  }

  char ccorrelator[64 + 1] = {0};
  int correlator_size = sizeof(ccorrelator);
  rc = ZJBSYMB(zjb, "SYS_CORR_LASTJOB", ccorrelator, &correlator_size);

  if (0 != rc)
  {
    dynfree(&ip);
    return rc;
  }

  memcpy(zjb->correlator, ccorrelator, sizeof(zjb->correlator));

  rc = dynfree(&ip);
  if (0 != rc)
  {
    strcpy(zjb->diag.service_name, "dynfree");
    zjb->diag.service_rc = rc;
    ZDIAG_SET_MSG(&zjb->diag, "dynfree failed with %d", rc);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  return RTNCD_SUCCESS;
}

int zjb_list_dds(ZJB *zjb, const std::string &jobid, std::vector<ZJobDD> &jobDDs)
{
  int rc = 0;
  STATSEVB *PTR64 sysoutInfo = nullptr;
  int entries = 0;

  if (0 == zjb->buffer_size)
    zjb->buffer_size = ZJB_DEFAULT_BUFFER_SIZE;
  if (0 == zjb->dds_max)
    zjb->dds_max = ZJB_DEFAULT_MAX_DDS;

  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));

  rc = ZJBMLSDS(zjb, &sysoutInfo, &entries);
  if (RTNCD_SUCCESS != rc && RTNCD_WARNING != rc)
  {
    return rc;
  }

  // NOTE(Kelosky): if we didn't get any errors and we have no entries, we will look up the job status and see if it's "INPUT".  In this case,
  // the SYSOUT data sets may not be vieawable via the SSI API.  So, we'll attempt to find the JESMSGLG and JESJCL data sets as documented here:
  // https://www.ibm.com/docs/en/zos/3.1.0?topic=allocation-specifying-data-set-name-daldsnam
  if (0 == entries)
  {
    ZJob job{};
    int view_rc = zjb_view(zjb, jobid, job);
    if (RTNCD_SUCCESS == view_rc)
    {
      zut_rtrim(job.owner);
      zut_rtrim(job.jobname);

      ZJobDD jesmsglg{};
      jesmsglg.jobid = job.jobid;
      jesmsglg.ddn = "JESMSGLG";
      jesmsglg.dsn = job.owner + '.' + job.jobname + '.' + job.jobid + '.' + jesmsglg.ddn;

// NOTE(Kelosky): these keys are not documented to indiciate whether they are always set to these exact values.  However,
// since we are handling this as a special case, we will match this number that we set and reference by the DSN without the actual keys.
#define JESMSGLG_KEY 2
      jesmsglg.key = JESMSGLG_KEY;
      jobDDs.push_back(jesmsglg);
      ZJobDD jesjcl{};
      jesjcl.jobid = job.jobid;
      jesjcl.ddn = "JESJCL";
      jesjcl.dsn = job.owner + '.' + job.jobname + '.' + job.jobid + '.' + jesjcl.ddn;
#define JESJCL_KEY 3
      jesjcl.key = JESJCL_KEY;
      jobDDs.push_back(jesjcl);
      ZUTMFR64(sysoutInfo);
      return rc;
    }

    ZUTMFR64(sysoutInfo);
    ZDIAG_SET_MSG(&zjb->diag, "no output DDs found for '%s'", jobid.c_str());
    zjb->diag.detail_rc = ZJB_RTNCD_VERBOSE_INFO_NOT_FOUND;
    return RTNCD_WARNING;
  }

  STATSEVB *PTR64 sysoutInfoNext = sysoutInfo;

  for (int i = 0; i < entries; i++)
  {
    std::string ddn((char *)sysoutInfoNext[i].stvsddnd, sizeof(sysoutInfo->stvsddnd));
    std::string stepname((char *)sysoutInfoNext[i].stvsstpd, sizeof(sysoutInfo->stvsstpd));
    std::string procstep((char *)sysoutInfoNext[i].stvsprcd, sizeof(sysoutInfo->stvsprcd));
    std::string dsn((char *)sysoutInfoNext[i].stvsdsn, sizeof(sysoutInfo->stvsdsn));

    ZJobDD zjobdd{};

    zjobdd.ddn = ddn;
    memcpy(zjobdd.token, sysoutInfoNext[i].stvsctkn, sizeof(sysoutInfoNext[i].stvsctkn));
    zjobdd.stepname = stepname;
    zjobdd.procstep = procstep;
    zjobdd.dsn = dsn;
    zjobdd.jobid = std::string(jobid);
    zjobdd.key = sysoutInfoNext[i].stvsdsky;
    zjobdd.is_asa = (sysoutInfoNext[i].stvsrecf & dcbrecca) != 0;

    jobDDs.push_back(zjobdd);
  }

  ZUTMFR64(sysoutInfo);

  return rc;
}

int zjb_view(ZJB *zjb, const std::string &jobid, ZJob &job)
{
  int rc = 0;
  ZJB_JOB_INFO *PTR64 job_info = nullptr;
  int entries = 0;

  if (0 == zjb->buffer_size)
    zjb->buffer_size = ZJB_DEFAULT_BUFFER_SIZE;
  if (0 == zjb->jobs_max)
    zjb->jobs_max = ZJB_DEFAULT_MAX_JOBS;

  if (jobid.size() > sizeof(zjb->jobid))
    zut_uppercase_pad_truncate(zjb->correlator, jobid, sizeof(zjb->correlator));
  else
    zut_uppercase_pad_truncate(zjb->jobid, jobid, sizeof(zjb->jobid));

  rc = ZJBMVIEW(zjb, &job_info, &entries);
  if (0 != rc)
  {
    return rc;
  }

  if (0 == entries)
  {
    ZDIAG_SET_MSG(&zjb->diag, "Could not locate job with id '%s'", jobid.c_str());
    zjb->diag.detail_rc = ZJB_RTNCD_JOB_NOT_FOUND;
    return RTNCD_FAILURE;
  }

  // create a vector which will only have one entry
  std::vector<ZJob> jobs;
  zjb_build_job_response(job_info, entries, jobs);
  job = jobs[0];

  ZUTMFR64(job_info);

  return RTNCD_SUCCESS;
}

#ifndef SWIG // Convenience overloads are not declared for SWIG; see zjb.hpp
int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, std::vector<ZJob> &jobs)
{
  return zjb_list_by_owner(zjb, owner_name, "", "", jobs);
}

int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, const std::string &prefix_name, std::vector<ZJob> &jobs)
{
  return zjb_list_by_owner(zjb, owner_name, prefix_name, "", jobs);
}
#endif

int zjb_list_by_owner(ZJB *zjb, const std::string &owner_name, const std::string &prefix_name, const std::string &status_name, std::vector<ZJob> &jobs)
{
  int rc = 0;
  ZJB_JOB_INFO *PTR64 job_info = nullptr;
  int entries = 0;

  std::string resolved_owner = owner_name;
  if (resolved_owner.empty())
  {
    zut_get_current_user(resolved_owner);
  }

  if (0 == zjb->buffer_size)
    zjb->buffer_size = ZJB_DEFAULT_BUFFER_SIZE;
  if (0 == zjb->jobs_max)
    zjb->jobs_max = ZJB_DEFAULT_MAX_JOBS;

  zut_uppercase_pad_truncate(zjb->owner_name, resolved_owner, sizeof(zjb->owner_name));
  zut_uppercase_pad_truncate(zjb->prefix_name, prefix_name, sizeof(zjb->prefix_name));
  zut_uppercase_pad_truncate(zjb->status_name, status_name, sizeof(zjb->status_name));

  rc = ZJBMLIST(zjb, &job_info, &entries);
  if (RTNCD_SUCCESS != rc && RTNCD_WARNING != rc)
  {
    return rc;
  }

  zjb_build_job_response(job_info, entries, jobs);

  ZUTMFR64(job_info);

  return rc;
}

int zjb_list_proclib(ZJB *zjb, std::vector<std::string> &proclib)
{
  int rc = 0;
  ZJB_JOB_INFO *PTR64 job_info = nullptr;
  int entries = 0;
  char buffer[MAX_DSN_ENTRY_SIZE] = {0};
  int buffer_size = sizeof(buffer);
  rc = ZJBMLPRC(zjb, buffer, &buffer_size, &entries);

  if (RTNCD_SUCCESS == rc)
  {
    for (int i = 0; i < entries; i++)
    {
      std::string dsn(buffer + (i * DSN_ENTRY_SIZE), DSN_ENTRY_SIZE);
      proclib.push_back(dsn);
    }
  }

  return rc;
}

void zjb_build_job_response(ZJB_JOB_INFO *PTR64 job_info, int entries, std::vector<ZJob> &jobs)
{
  ZJB_JOB_INFO *PTR64 job_info_next = job_info;
  jobs.reserve(entries);
  if (entries == 0)
    return;

  struct psa *psa = (struct psa *)0;
  struct cvt *cvt = (struct cvt *)psa->flccvt;
  const auto *cvt_extension = reinterpret_cast<const struct cvtxtnt2 *>(static_cast<uintptr_t>(cvt->cvtext2a));
  int64_t local_offset_tod = 0;
  memcpy(&local_offset_tod, &cvt_extension->cvtldto, sizeof(local_offset_tod));
  // Read the full TOD offset; CVTTZ alone loses subsecond precision.
  constexpr int64_t TOD_UNITS_PER_CS = 40960000;
  const int64_t utc_offset_cs = local_offset_tod / TOD_UNITS_PER_CS;

  for (int i = 0; i < entries; i++)
  {
    std::string jobname((char *)job_info_next[i].statjqtr.sttrname, sizeof(job_info->statjqtr.sttrname));
    std::string jobid((char *)job_info_next[i].statjqtr.sttrjid, sizeof(job_info->statjqtr.sttrjid));
    std::string subsystem((char *)job_info_next[i].subsystem, sizeof(job_info->subsystem));
    std::string owner((char *)job_info_next[i].statjqtr.sttrouid, sizeof(job_info->statjqtr.sttrouid));
    std::string jobclass((char *)job_info_next[i].statjqtr.sttrclas, sizeof(job_info->statjqtr.sttrclas));
    std::string correlator((char *)job_info_next[i].statjqtr.sttrjcor, sizeof(job_info->statjqtr.sttrjcor));

    ZJob zjob{};

    union cc
    {
      int full;
      unsigned char parts[4];
    } mycc = {0};
    memcpy(&mycc, &job_info_next[i].statjqtr.sttrxind, sizeof(cc));

    zjob.full_status = std::string(job_info_next[i].phase_text);
    zut_rtrim(zjob.full_status);

    zjob.retcode = ZJB_UNKNOWN_RC;

    if ("AWAIT MAIN SELECT" == zjob.full_status)
    {
      zjob.status = "INPUT";
    }
    else if ("EXECUTING" == zjob.full_status)
    {
      zjob.status = "ACTIVE";
    }
    else if ("AWAITING OUTPUT" == zjob.full_status)
    {
      zjob.status = "OUTPUT";

      if ((unsigned char)job_info_next[i].statjqtr.sttrxind & sttrxab)
      {
        // for an abend, these are the bits which contain the code
        mycc.full &= 0x00FFF000; // clear uneeded bits
        unsigned char byte1 = mycc.parts[1] >> 4;
        unsigned char byte2 = mycc.parts[1] & 0x0F;
        unsigned char byte3 = mycc.parts[2] >> 4;

        std::string result = "ABEND ";
        result.push_back(zut_get_hex_char(byte1));
        result.push_back(zut_get_hex_char(byte2));
        result.push_back(zut_get_hex_char(byte3));
        zjob.retcode = result;
      }
      else
      {
        mycc.full &= 0x00000FFF; // clear uneeded bits
        std::stringstream sscc;
        sscc << std::setw(4) << std::setfill('0') << mycc.full; // format to 4 characters
        zjob.retcode = "CC " + sscc.str();                      // make it look like z/OSMF
      }
    }
    else
    {
      // leave service text as-is
      zjob.status = zjob.full_status;
    }

    // handle special cases
    if ((unsigned char)job_info_next[i].statjqtr.sttrxind == sttrxjcl)
    {
      zjob.retcode = "JCL ERROR";
    }
    else if ((unsigned char)job_info_next[i].statjqtr.sttrxind == sttrxcan)
    {
      zjob.retcode = "CANCELED";
    }
    else if ((unsigned char)job_info_next[i].statjqtr.sttrxind == sttrxsec)
    {
      zjob.retcode = "SEC ERROR";
    }

    if ((unsigned char)job_info_next[i].statjqtr.sttrjtyp == sttrstc)
    {
      zjob.type = "STC";
    }
    else if ((unsigned char)job_info_next[i].statjqtr.sttrjtyp == sttrtsu)
    {
      zjob.type = "TSU";
    }
    else
    {
      zjob.type = "JOB";
    }

    zjob.jobname = jobname;
    zjob.jobid = jobid;
    zjob.subsystem = subsystem;
    zjob.owner = owner.at(0) == 0x00 ? "" : owner;
    zjob.jobclass = jobclass;
    zjob.correlator = correlator;
    zjob.phase = job_info_next[i].statjqtr.sttrphaz;
    const auto &execution = job_info_next[i].execution;
    if (execution.system[0] != 0)
    {
      zjob.exec_system.assign(execution.system, sizeof(execution.system));
      zut_rtrim(zjob.exec_system);
    }
    if (execution.member[0] != 0)
    {
      zjob.exec_member.assign(execution.member, sizeof(execution.member));
      zut_rtrim(zjob.exec_member);
    }
    zjob.exec_submitted = zjb_detail::format_timestamp(execution.submitted.date, execution.submitted.time, utc_offset_cs);
    zjob.exec_started = zjb_detail::format_timestamp(execution.started.date, execution.started.time, utc_offset_cs);
    zjob.exec_ended = zjb_detail::format_timestamp(execution.ended.date, execution.ended.time, utc_offset_cs);

    jobs.push_back(std::move(zjob));
  }
}
