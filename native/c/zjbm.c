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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "zjblkup.h"
#include "iazjproc.h"
#include "zrecovery.h"
#include "zssitype.h"
#include "zjbm.h"
#include "zssi.h"
#include "zjbm31.h"
#include "zstorage.h"
#include "zjsytype.h"
#include "zjbtype.h"
#include "ihapsa.h"
#include "cvt.h"
#include "iefjesct.h"
#include "ztype.h"

// TODO(Kelosky):
// https://www.ibm.com/docs/en/zos/3.1.0?topic=79-putget-requests
// read system log

#define SYMBOL_ENTRIES 3
typedef struct
{
  JSYTABLE jsymbolTable;
  JSYENTRY jsymbolEntry[SYMBOL_ENTRIES];
  unsigned char buffer[SYMBOL_ENTRIES * 16];
} JSYMBOLO;

typedef struct cvt CVT;
typedef struct jesct JESCT;

static int get_ssibssnm(SSIB *ssib)
{
  PSA *psa_a = (PSA *)0;
  CVT *cvt_a = psa_a->flccvt;
  JESCT *jesct_a = cvt_a->cvtjesct;

  // This might be overkill, but just making sure we have an SS name
  if (NULL == jesct_a->jespjesn)
  {
    return RTNCD_FAILURE;
  }
  // We do not support JES3 (yet)
  if (0 != jesct_a->jesjesfg & jes3actv)
  {
    return ZJB_RTNCD_JES3_NOT_SUPPORTED;
  }

  memcpy(ssib->ssibssnm, jesct_a->jespjesn, sizeof(ssib->ssibssnm));
  return RTNCD_SUCCESS;
}

static int init_ssib(SSIB *ssib)
{
  memcpy(ssib->ssibid, "SSIB", sizeof(ssib->ssibid));
  ssib->ssiblen = sizeof(SSIB);
  return get_ssibssnm(ssib);
}

static void init_ssob(SSOB *PTR32 ssob, SSIB *PTR32 ssib, void *PTR32 function_depenent_area, int function)
{
  memcpy(ssob->ssobid, "SSOB", sizeof(ssob->ssobid));
  ssob->ssoblen = sizeof(SSOB);
  ssob->ssobssib = ssib;
  ssob->ssobindv = (int)function_depenent_area;
  ssob->ssobfunc = function;
}

static void init_stat(STAT *stat)
{
  memcpy(stat->stateye, "STAT", sizeof(stat->stateye));
  stat->statverl = statcvrl;
  stat->statverm = statcvrm;
  stat->statlen = statsize;
}

static void zjb_recovery_diag(ZJB *zjb, const char *service_name, const ZRCVY_ENV *zenv)
{
  memset(zjb->diag.service_name, 0, sizeof(zjb->diag.service_name));
  strncpy(zjb->diag.service_name, service_name, sizeof(zjb->diag.service_name) - 1);
  zjb->diag.detail_rc = ZJB_RTNCD_UNEXPECTED_ERROR;
  zjb->diag.service_rc = zenv->abend_rc;
  zjb->diag.service_rsn = zenv->abend_rsn;
  ZDIAG_SET_MSG(&zjb->diag, "Unexpected abend occurred during %.16s (rc:%X, rsn:%X)",
                service_name, zenv->abend_rc, zenv->abend_rsn);
}

#pragma prolog(ZJBSYMB, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBSYMB, " ZWEEPILG ")
int ZJBSYMB(ZJB *zjb, const char *symbol, char *value, int *value_size)
{
  int rc = 0;

  JSYMPARM jsym = {0};

  unsigned char *p = NULL;

  memcpy(jsym.jsymeye, "JSYM", sizeof(jsym.jsymeye));
  jsym.jsymlng = jsymsize;
  jsym.jsymrqop = jsymextr;

#define JSYMVRMC 0x0100;

  jsym.jsymvrm = JSYMVRMC;
  jsym.jsymsnmn = 1;
  jsym.jsymsnml = (int)strlen(symbol);
  jsym.jsymsnma = (void *PTR32)symbol;

  JSYMBOLO jsymbolOutput = {0};

  jsym.jsymouta = &jsymbolOutput;
  jsym.jsymouts = sizeof(JSYMBOLO);

  rc = iazsymbl(&jsym);

  if (0 != rc)
  {
    // TODO(Kelosky): read jsymerad for errors
    strcpy(zjb->diag.service_name, "iazsymbl");
    ZDIAG_SET_MSG(&zjb->diag, "IAZSYMBL RC was: '%d', JSYMRETN was: '%d', JSYMREAS: %d", rc, jsym.jsymretn, jsym.jsymreas);
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  p = (unsigned char *)&jsymbolOutput.jsymbolTable;                               // --> table
  JSYENTRY *jsymbolEntry = (JSYENTRY *)(p + jsymbolOutput.jsymbolTable.jsytent1); // --> first entry

  // Validate buffer size to prevent overflow
  if (jsymbolEntry->jsyevals >= *value_size)
  {
    strcpy(zjb->diag.service_name, "ZJBSYMB");
    ZDIAG_SET_MSG(&zjb->diag, "Symbol value size (%d) exceeds buffer capacity (%d) for symbol '%.32s'",
                  jsymbolEntry->jsyevals, *value_size, symbol);
    zjb->diag.detail_rc = ZJB_RTNCD_INSUFFICIENT_BUFFER;
    return RTNCD_FAILURE;
  }

  p = p + jsymbolEntry->jsyevalo;
  memcpy(value, p, jsymbolEntry->jsyevals);

  // Null-terminate the string if there's space
  if (jsymbolEntry->jsyevals < *value_size)
  {
    value[jsymbolEntry->jsyevals] = '\0';
  }

  return RTNCD_SUCCESS;
}

// purge a job
#pragma prolog(ZJBMPRG, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMPRG, " ZWEEPILG ")
int ZJBMPRG(ZJB *zjb)
{
  // purge a job in protected (ssjmpprt) mode
  return ZJBMMOD(zjb, ssjmprg, ssjmpprt);
}

// cancel a job
#pragma prolog(ZJBMCNL, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMCNL, " ZWEEPILG ")
int ZJBMCNL(ZJB *zjb, int flags)
{
  // cancel a job in protected (ssjmcprt) mode
  int options = ssjmcprt | flags;
  return ZJBMMOD(zjb, ssjmcanc, ssjmcprt);
}

// hold a job
#pragma prolog(ZJBMHLD, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMHLD, " ZWEEPILG ")
int ZJBMHLD(ZJB *zjb)
{
  // Hold a job in protected (ssjmpprt) mode
  return ZJBMMOD(zjb, ssjmhold, 0);
}

// release a job
#pragma prolog(ZJBMRLS, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMRLS, " ZWEEPILG ")
int ZJBMRLS(ZJB *zjb)
{
  // Release a job in protected (ssjmpprt) mode
  return ZJBMMOD(zjb, ssjmrls, 0);
}

// modify a job
int ZJBMMOD(ZJB *zjb, int type, int flags)
{
  int rc = 0;
  int loop_control = 0;

  // return rc
  SSOB *PTR32 ssobp = NULL;
  SSOB ssob = {0};
  SSIB ssib = {0};
  SSJM ssjm = {0};
  SSJF *ssjfp = NULL;

  ZRCVY_ENV zenv = {0};

  // https://www.ibm.com/docs/en/zos/3.1.0?topic=sfcd-modify-job-function-call-ssi-function-code-85
  init_ssob(&ssob, &ssib, &ssjm, 85);
  rc = init_ssib(&ssib);
  if (0 != rc)
  {
    strcpy(zjb->diag.service_name, "init_ssib");
    zjb->diag.detail_rc = rc;
    if (ZJB_RTNCD_JES3_NOT_SUPPORTED == rc)
    {
      ZDIAG_SET_MSG(&zjb->diag, "JES3 is not supported");
    }
    else
    {
      ZDIAG_SET_MSG(&zjb->diag, "Failed to get SSIBSSNM");
    }
    return RTNCD_FAILURE;
  }

  memcpy(ssjm.ssjmeye, "SSJMPL  ", sizeof(ssjm.ssjmeye));
  ssjm.ssjmlen = ssjmsize;
  ssjm.ssjmvrm = ssjmvrmc;

  ssjm.ssjmopt1 = ssjm.ssjmopt1 | ssjmpd64; // 64 bit storage
  ssjm.ssjmopt1 = ssjm.ssjmopt1 | ssjmpsyn; // SYNC

  ssjm.ssjmtype = type;
  if (ssjmprg == type) // purge
  {
    ssjm.ssjmpflg = ssjm.ssjmpflg | flags;
  }
  else if (ssjmcanc == type) // cancel
  {
    ssjm.ssjmcflg = ssjm.ssjmcflg | flags;
  }
  else if (ssjmhold == type) // hold
  {
    // no flags needed
  }
  else if (ssjmrls == type) // release
  {
    // no flags needed
  }
  else if (ssjmrst == type) // restart
  {                         // TODO(zFernand0): not implemented
    ssjm.ssjmeflg = ssjm.ssjmeflg | flags;
  }
  else if (ssjmspin == type) // spin
  {                          // TODO(zFernand0): not implemented
    ssjm.ssjmtsfl = ssjm.ssjmtsfl | flags;
    // ssjm.ssjmtsdn = ddname to spin
  }

  if (zjb->jobid[0] != 0x00)
  {
    ssjm.ssjmsel1 = ssjm.ssjmsel1 | ssjmsjbi; // real lookup
    memcpy(ssjm.ssjmjbil, zjb->jobid, sizeof(ssjm.ssjmjbil));
    memcpy(ssjm.ssjmjbih, zjb->jobid, sizeof(ssjm.ssjmjbih));
  }
  else
  {
    char correlator31[64] = {0};
    memcpy(correlator31, zjb->correlator, sizeof(zjb->correlator));
    ssjm.ssjmsel5 = ssjmscor;
    ssjm.ssjmjcrp = &correlator31[0];
  }

  ssjm.ssjmsel2 = ssjm.ssjmsel2 | ssjmsjob; // batch jobs
  ssjm.ssjmsel2 = ssjm.ssjmsel2 | ssjmsstc; // stcs
  ssjm.ssjmsel2 = ssjm.ssjmsel2 | ssjmstsu; // time sharing users

  ssobp = &ssob;
  ssobp = (SSOB * PTR32)((unsigned int)ssobp | 0x80000000);
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }

  disable_recovery(&zenv);

  if (0 != rc || 0 != ssob.ssobretn)
  {
    strcpy(zjb->diag.service_name, "IEFSSREQ");
    zjb->diag.service_rc = ssob.ssobretn;
    zjb->diag.service_rsn = ssjm.ssjmretn;
    zjb->diag.service_rsn_secondary = ssjm.ssjmret2;
    // Understanding reason codes from this SSOB: https://www.ibm.com/docs/en/zos/3.1.0?topic=85-output-parameters
    ZDIAG_SET_MSG(&zjb->diag, "IEFSSREQ rc was: '%d' SSOBRETN was: '%d', SSJMRETN was: '%d', SSJMRET2 was: '%d'", rc, ssob.ssobretn, ssjm.ssjmretn, ssjm.ssjmret2);
    return RTNCD_FAILURE;
  }

  ssjfp = (SSJF *)ssjm.ssjmsjf8; // NOTE(Kelosky): in the future we can return a list of SSJFs, for now, if none returned, the job was not found

  if (0 == ssjm.ssjmnsjf)
  {
    if (zjb->jobid[0] != 0x00)
    {
      zjb->diag.detail_rc = ZJB_RTNCD_JOB_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching jobid '%.8s'", zjb->jobid);
    }
    else
    {
      zjb->diag.detail_rc = ZJB_RTNCD_CORRELATOR_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching correlator '%.64s'",
                    zjb->correlator);
    }
    return RTNCD_FAILURE;
  }

  return RTNCD_SUCCESS;
}

// view job
#pragma prolog(ZJBMVIEW, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMVIEW, " ZWEEPILG ")
int ZJBMVIEW(ZJB *zjb, ZJB_JOB_INFO **PTR64 job_info, int *entries)
{
  STAT stat = {0};
  init_stat(&stat);

  if (zjb->jobid[0] != 0x00)
  {
    stat.statsel1 = stat.statsel1 | statsjbi; // real lookup
    memcpy(stat.statjbil, zjb->jobid, sizeof(stat.statjbil));
    memcpy(stat.statjbih, zjb->jobid, sizeof(stat.statjbih));
  }
  else
  {
    char correlator31[64] = {0};
    memcpy(correlator31, zjb->correlator, sizeof(zjb->correlator));
    stat.statsel5 = statscor;
    stat.statjcrp = &correlator31[0];
  }

  stat.stattype = statters;

  return ZJBMTCOM(zjb, &stat, job_info, entries);
}

// list jobs
#pragma prolog(ZJBMLIST, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMLIST, " ZWEEPILG ")
int ZJBMLIST(ZJB *zjb, ZJB_JOB_INFO **PTR64 job_info, int *entries)
{
  STAT stat = {0};
  init_stat(&stat);

  stat.statsel1 = statsown;
  stat.stattype = statters;
  memcpy(stat.statownr, zjb->owner_name, sizeof((stat.statownr)));

  if (0 == strcmp(zjb->prefix_name, "        "))
  {
    // do nothing
  }
  else
  {
    stat.statsel1 |= statsjbn; // add job name filter
    memcpy(stat.statjobn, zjb->prefix_name, sizeof((stat.statjobn)));
  }

  if (0 == strcmp(zjb->status_name, "ACTIVE  "))
  {
    stat.statphaz = stat___onmain;
    stat.statsel3 |= statsphz;
  }

  return ZJBMTCOM(zjb, &stat, job_info, entries);
}

int ZJBMGJQ(ZJB *zjb, SSOB *ssobp, STAT *statp, STATJQ * PTR32 * PTR32 statjqp)
{
  int rc = 0;

  SSOB *PTR32 ssobp2 = NULL;

  ZRCVY_ENV zenv = {0};

  ssobp2 = ssobp;
  ssobp2 = (SSOB * PTR32)((unsigned int)ssobp2 | 0x80000000);
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp2);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }

  disable_recovery(&zenv);

  if (0 != rc || 0 != ssobp->ssobretn)
  {
    ZJBMEMSG(zjb, statp, ssobp, rc);
    statp->stattype = statmem; // free storage
    rc = iefssreq(&ssobp2);
    return RTNCD_FAILURE;
  }

  *statjqp = (STATJQ * PTR32) statp->statjobf;

  return RTNCD_SUCCESS;
}

int ZJBMEMSG(ZJB *zjb, STAT *PTR64 stat, SSOB *PTR64 ssobp, int rc)
{
  strcpy(zjb->diag.service_name, "IEFSSREQ");
  zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
  zjb->diag.service_rc = ssobp->ssobretn;
  zjb->diag.service_rsn = stat->statreas;
  zjb->diag.service_rsn_secondary = stat->statrea2;
#define STATLERR 8
  if (STATLERR == ssobp->ssobretn && statrojb == stat->statreas) // skip if invalid job id
  {
    ZDIAG_SET_MSG(&zjb->diag, "Job ID '%.8s' was not valid", stat->statojbi); // STATREAS contains the reason
  }
  else
  {
    ZDIAG_SET_MSG(&zjb->diag, "IEFSSREQ rc was: '%d' SSOBRETN was: '%d', STATREAS was: '%d', STATREA2 was: '%d'", rc, ssobp->ssobretn, stat->statreas, stat->statrea2); // STATREAS contains the reason
  }

  return rc;
}

int ZJBMTCOM(ZJB *zjb, STAT *PTR64 stat, ZJB_JOB_INFO **PTR64 job_info, int *entries)
{
  int rc = 0;

  SSOB *PTR32 ssobp = NULL;
  SSOB ssob = {0};
  SSIB ssib = {0};

  ZRCVY_ENV zenv = {0};

  // https://www.ibm.com/docs/en/zos/3.1.0?topic=sfcd-extended-status-function-call-ssi-function-code-80
  init_ssob(&ssob, &ssib, stat, 80);
  if (0 != init_ssib(&ssib))
  {
    strcpy(zjb->diag.service_name, "init_ssib");
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  ssobp = &ssob;
  ssobp = (SSOB * PTR32)((unsigned int)ssobp | 0x80000000);
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }

  disable_recovery(&zenv);

  if (0 != rc || 0 != ssob.ssobretn)
  {
    ZJBMEMSG(zjb, stat, &ssob, rc);
    stat->stattype = statmem; // free storage
    rc = iefssreq(&ssobp);
    return RTNCD_FAILURE;
  }

  STATJQ *PTR32 statjqp = (STATJQ * PTR32) stat->statjobf;

  ZJB_JOB_INFO *statjqtrsp = storage_get64(zjb->buffer_size);
  *job_info = statjqtrsp;

  STATJQHD *PTR32 statjqhdp = NULL;
  STATJQTR *PTR32 statjqtrp = NULL;

  int total_size = 0;
  int loop_control = 0;

  while (statjqp)
  {
    if (loop_control >= zjb->jobs_max)
    {
      zjb->diag.detail_rc = ZJB_RSNCD_MAX_JOBS_REACHED;
      ZDIAG_SET_MSG(&zjb->diag, "Reached maximum returned jobs requested %d", zjb->jobs_max);
      stat->stattype = statmem; // free storage
      memset(&zenv, 0, sizeof(ZRCVY_ENV));
      if (0 == enable_recovery(&zenv))
      {
        rc = iefssreq(&ssobp);
      }
      else
      {
        zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
        rc = RTNCD_FAILURE;
        disable_recovery(&zenv);
        return rc;
      }
      disable_recovery(&zenv);
      return rc == RTNCD_FAILURE ? rc : RTNCD_WARNING;
    }

    total_size += (int)sizeof(ZJB_JOB_INFO);

    if (total_size <= zjb->buffer_size)
    {
      *entries = *entries + 1;

      statjqhdp = (STATJQHD * PTR32)((unsigned char *PTR32)statjqp + statjqp->stjqohdr);
      statjqtrp = (STATJQTR * PTR32)((unsigned char *PTR32)statjqhdp + sizeof(STATJQHD));

      memcpy(statjqtrsp, statjqtrp, sizeof(STATJQTR));
      int rc = iaztlkup(&ssob, statjqtrsp, zjb);
      if (0 != rc)
      {
        strcpy(zjb->diag.service_name, "iaztlkup");
        // For information about the reason code, look for `tlkretcd` in "native/c/chdsect/iaztlkdf.h"
        // https://www.ibm.com/docs/en/zos/3.1.0?topic=80-text-lookup-service-iaztlkup
        ZDIAG_SET_MSG(&zjb->diag, "IAZTLKUP RC: '%d' reason: '%d'", rc, zjb->diag.detail_rc);
        zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
        storage_free64(statjqtrsp);
        return RTNCD_FAILURE;
      }

      memset(statjqtrsp->subsystem, 0x00, sizeof(statjqtrsp->subsystem));
      strncpy(statjqtrsp->subsystem, (char *)statjqp->stjqoss, sizeof(statjqp->stjqoss));

      memset(&statjqtrsp->execution, 0, sizeof(statjqtrsp->execution));
      stat->stattrsa = statjqp;
      stat->stattype = statvrbo;
      STATJQ *PTR32 expanded_job = NULL;
      rc = ZJBMGJQ(zjb, &ssob, stat, &expanded_job);
      if (RTNCD_SUCCESS != rc)
      {
        storage_free64(*job_info);
        *job_info = NULL;
        *entries = 0;
        return rc;
      }

      STATVE *PTR32 statvep = (STATVE * PTR32) statjqp->stjqvrbo;
      if (statvep != NULL)
      {
        STATJVHD *PTR32 statjvhdp = (STATJVHD * PTR32)((unsigned char *PTR32)statvep + statvep->stveohdr);
        const size_t execution_end = offsetof(STATJQVB, stvbjusr);
        if (statjvhdp->stjvlen >= sizeof(STATJVHD) + execution_end)
        {
          STATJQVB *PTR32 statjqvbp = (STATJQVB * PTR32)((unsigned char *PTR32)statjvhdp + sizeof(STATJVHD));
          if (statjqvbp->stvblen >= execution_end && !(statjqvbp->stvbflg1 & stvb1err))
            memcpy(&statjqtrsp->execution, &statjqvbp->stvbrte, sizeof(statjqtrsp->execution));
        }
      }

      statjqtrsp++;
    }
    else
    {
      zjb->diag.detail_rc = ZJB_RTNCD_INSUFFICIENT_BUFFER;
    }

    statjqp = (STATJQ * PTR32) statjqp->stjqnext;

    loop_control++;
  }

  zjb->buffer_size_needed = total_size;

  stat->stattype = statmem; // free storage
  memset(&zenv, 0, sizeof(ZRCVY_ENV));
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }
  disable_recovery(&zenv);

  return rc;
}

#define LOOP_MAX 100

// list data sets for a job
#pragma prolog(ZJBMLSDS, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMLSDS, " ZWEEPILG ")
int ZJBMLSDS(ZJB *PTR64 zjb, STATSEVB **PTR64 sysoutInfo, int *entries)
{
  int rc = 0;
  int loop_control = 0;

  // return rc
  SSOB *PTR32 ssobp = NULL;
  SSOB ssob = {0};
  SSIB ssib = {0};
  STAT stat = {0};

  ZRCVY_ENV zenv = {0};

  STATJQ *PTR32 statjqp = NULL;
  STATJQHD *PTR32 statjqhdp = NULL;
  STATJQTR *PTR32 statjqtrp = NULL;

  STATVO *PTR32 statvop = NULL;
  STATSVHD *PTR32 statsvhdp = NULL;
  STATSEVB *PTR32 statsevbp = NULL;

  // https://www.ibm.com/docs/en/zos/3.1.0?topic=sfcd-extended-status-function-call-ssi-function-code-80
  if (0 != init_ssib(&ssib))
  {
    strcpy(zjb->diag.service_name, "init_ssib");
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }
  init_ssob(&ssob, &ssib, &stat, 80);
  init_stat(&stat);

  stat.stattype = statters;

  if (zjb->jobid[0] != 0x00)
  {
    stat.statsel1 = stat.statsel1 | statsjbi; // real lookup
    memcpy(stat.statjbil, zjb->jobid, sizeof(stat.statjbil));
    memcpy(stat.statjbih, zjb->jobid, sizeof(stat.statjbih));
  }
  else
  {
    char correlator31[64] = {0};
    memcpy(correlator31, zjb->correlator, sizeof(zjb->correlator));
    stat.statsel5 = statscor;
    stat.statjcrp = &correlator31[0];
  }

  // NOTE(Kelosky): we first locate the STATJQ via jobid or job correlator because verbose data which containts SYSOUT info
  // cannot be obtained directly from the jobid or job correlator as documented by the JES SSI API.
  rc = ZJBMGJQ(zjb, &ssob, &stat, &statjqp);

  if (0 != rc)
  {
    return rc;
  }
  stat.statsel1 = 0;
  stat.statsel5 = 0;

  ssobp = &ssob;
  ssobp = (SSOB * PTR32)((unsigned int)ssobp | 0x80000000);

  if (NULL == statjqp)
  {

    if (zjb->jobid[0] != 0x00)
    {
      zjb->diag.detail_rc = ZJB_RTNCD_JOB_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching jobid '%.8s'", zjb->jobid);
    }
    else
    {
      zjb->diag.detail_rc = ZJB_RTNCD_CORRELATOR_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching correlator '%.64s'",
                    zjb->correlator);
    }

    stat.stattype = statmem; // free storage
    if (0 == enable_recovery(&zenv))
    {
      rc = iefssreq(&ssobp);
    }
    else
    {
      zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
      rc = RTNCD_FAILURE;
    }
    disable_recovery(&zenv);
    return rc == RTNCD_FAILURE ? rc : RTNCD_FAILURE;
  }

  stat.stattrsa = statjqp;
  stat.stattype = statoutv;

  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }
  disable_recovery(&zenv);

  if (0 != rc || 0 != ssob.ssobretn)
  {
    strcpy(zjb->diag.service_name, "IEFSSREQ");
    zjb->diag.service_rc = ssob.ssobretn;
    zjb->diag.service_rsn = stat.statreas;
    zjb->diag.service_rsn_secondary = stat.statrea2;
    ZDIAG_SET_MSG(&zjb->diag,
                  "IEFSSREQ rc was: '%d' SSOBRETN was: '%d', STATREAS was: '%d', "
                  "STATREA2 was: '%d'",
                  rc, ssob.ssobretn, stat.statreas,
                  stat.statrea2); // STATREAS contains the reason
    return RTNCD_FAILURE;
  }

  memset(&zenv, 0, sizeof(ZRCVY_ENV));

  statjqp = (STATJQ * PTR32) stat.statjobf;

  if (NULL == statjqp)
  {
    statjqp = stat.stattrsa;
  }

  if (NULL == statjqp)
  {
    if (zjb->jobid[0] != 0x00)
    {
      zjb->diag.detail_rc = ZJB_RTNCD_JOB_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching jobid '%.8s'", zjb->jobid);
    }
    else
    {
      zjb->diag.detail_rc = ZJB_RTNCD_CORRELATOR_NOT_FOUND;
      ZDIAG_SET_MSG(&zjb->diag, "No jobs found matching correlator '%.64s'",
                    zjb->correlator);
    }
    zjb->diag.detail_rc = ZJB_RTNCD_JOB_NOT_FOUND;
    stat.stattype = statmem; // free storage
    memset(&zenv, 0, sizeof(ZRCVY_ENV));
    if (0 == enable_recovery(&zenv))
    {
      rc = iefssreq(&ssobp);
    }
    else
    {
      zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
      rc = RTNCD_FAILURE;
    }
    disable_recovery(&zenv);
    return rc == RTNCD_FAILURE ? rc : RTNCD_FAILURE;
  }

  STATSEVB *statsetrsp = storage_get64(zjb->buffer_size);
  *sysoutInfo = statsetrsp;

  int total_size = 0;

  while (statjqp)
  {
    statvop = (STATVO * PTR32) statjqp->stjqsvrb;
    statjqhdp = (STATJQHD * PTR32)((unsigned char *PTR32)statjqp + statjqp->stjqohdr);
    statjqtrp = (STATJQTR * PTR32)((unsigned char *PTR32)statjqhdp + sizeof(STATJQHD));

    while (statvop)
    {

      if (loop_control >= zjb->dds_max)
      {
        stat.stattype = statmem; // free storage
        if (0 == enable_recovery(&zenv))
        {
          rc = iefssreq(&ssobp);
        }
        else
        {
          zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
          rc = RTNCD_FAILURE;
        }
        disable_recovery(&zenv);
        zjb->diag.detail_rc = ZJB_RSNCD_MAX_JOBS_REACHED;
        ZDIAG_SET_MSG(&zjb->diag, "max DDs reached '%d', results truncated", zjb->dds_max);
        return rc == RTNCD_FAILURE ? rc : RTNCD_WARNING;
      }

      total_size += (int)sizeof(STATSEVB);

      if (total_size <= zjb->buffer_size)
      {
        *entries = *entries + 1;

        statsvhdp = (STATSVHD * PTR32)((unsigned char *PTR32)statvop + statvop->stvoohdr);
        statsevbp = (STATSEVB * PTR32)((unsigned char *PTR32)statsvhdp + sizeof(STATSVHD));

        memcpy(statsetrsp, statsevbp, sizeof(STATSEVB));
        statsetrsp++;
      }
      else
      {
        zjb->diag.detail_rc = ZJB_RTNCD_INSUFFICIENT_BUFFER;
      }

      statvop = (STATVO * PTR32) statvop->stvojnxt;

      loop_control++;
    }

    statjqp = (STATJQ * PTR32) statjqp->stjqnext;
  }

  zjb->buffer_size_needed = total_size;

  stat.stattype = statmem; // free storage
  memset(&zenv, 0, sizeof(ZRCVY_ENV));
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }
  disable_recovery(&zenv);

  return rc;
}

#pragma prolog(ZJBMLPRC, " ZWEPROLG NEWDSA=(YES,128) ")
#pragma epilog(ZJBMLPRC, " ZWEEPILG ")
int ZJBMLPRC(ZJB *zjb, char *buffer, int *buffer_size, int *entries)
{
  int rc = 0;

  SSOB *PTR32 ssobp = NULL;

  SSOB ssob = {0};
  SSIB ssib = {0};
  SSJP ssjp = {0};
  JPROC jproc = {0};

  ZRCVY_ENV zenv = {0};

  *entries = 0;

  if (NULL == buffer || *buffer_size < MAX_DSN_ENTRY_SIZE)
  {
    zjb->diag.detail_rc = ZJB_RTNCD_INSUFFICIENT_BUFFER;
    ZDIAG_SET_MSG(&zjb->diag, "buffer is NULL or buffer size is less than '%d'", MAX_DSN_ENTRY_SIZE);
    return RTNCD_FAILURE;
  }

  if (0 != init_ssib(&ssib))
  {
    strcpy(zjb->diag.service_name, "init_ssib");
    zjb->diag.detail_rc = ZJB_RTNCD_SERVICE_FAILURE;
    return RTNCD_FAILURE;
  }

  // https://www.ibm.com/docs/en/zos/3.2.0?topic=descriptions-jes-properties-ssi-function-code-82
  init_ssob(&ssob, &ssib, &ssjp, 82);

  memcpy(ssjp.ssjpid, "SSJP", sizeof(ssjp.ssjpid));
  ssjp.ssjplen = sizeof(SSJP);
  ssjp.ssjpver = ssjpverc;
  ssjp.ssjpfreq = ssjpprod; // ssjpprrs to release
  ssjp.ssjpuser = &jproc;   // IAZJPROC

  memcpy(jproc.jprcid, "JESPROCI", sizeof(jproc.jprcid));
  jproc.jprclen = jprcsze;
  jproc.jprcverl = jprccvrl;
  jproc.jprcverm = jprccvrm;

  ssobp = &ssob;
  ssobp = (SSOB * PTR32)((unsigned int)ssobp | 0x80000000);
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }
  disable_recovery(&zenv);

  if (0 != rc || 0 != ssob.ssobretn)
  {
    strncpy(zjb->diag.service_name, "IEFSSREQ", sizeof(zjb->diag.service_name) - 1);
    zjb->diag.service_name[sizeof(zjb->diag.service_name) - 1] = '\0';
    zjb->diag.service_rc = ssob.ssobretn;
    zjb->diag.service_rsn = ssjp.ssjpretn;
    ZDIAG_SET_MSG(&zjb->diag, "IEFSSREQ rc was: '%d' SSOBRETN was: '%d', SSJPRETN was: '%d'", rc, ssob.ssobretn, ssjp.ssjpretn);
    return RTNCD_FAILURE;
  }

  JPRHDR *PTR32 jprhdr = (JPRHDR * PTR32) jproc.jprclptr; // --> first PROCLIB
  char *dsn_buffer = buffer;

  while (jprhdr)
  {
    JPRPREF *PTR32 jprpref = (JPRPREF * PTR32)((unsigned int)jprhdr + jprhdr->jproprf);     // --> Prefix Section
    JPRGENI *PTR32 jprgeni = (JPRGENI * PTR32)((unsigned int)jprpref + jprprsz);            // --> General Section
    JPRDSETS *PTR32 jprdsets = (JPRDSETS * PTR32)((unsigned int)jprgeni + jprgeni->jprgln); // --> Data Set Section
    JPRDSINF *PTR32 jprdsinf = (JPRDSINF * PTR32)((unsigned int)jprdsets + jprdsize);       // --> Data Set Info Section

    for (int i = 0; i < jprgeni->jprdscnt; i++)
    {
      memcpy(dsn_buffer, jprdsinf->jprddsn, DSN_ENTRY_SIZE);
      dsn_buffer += DSN_ENTRY_SIZE;
      *entries = *entries + 1;
      if (*entries > MAX_DSN_ENTRIES)
      {
        zjb->diag.detail_rc = ZJB_RTNCD_INSUFFICIENT_BUFFER;
        ZDIAG_SET_MSG(&zjb->diag, "max DSN entries reached '%d', results truncated", MAX_DSN_ENTRIES);
        rc = RTNCD_FAILURE;
        break;
      }
      jprdsinf = (JPRDSINF * PTR32)((unsigned int)jprdsinf + jprdslen);
    }

    if (rc != RTNCD_SUCCESS)
    {
      break;
    }
    jprhdr = (JPRHDR * PTR32) jprhdr->jprnxtp;
  }

  ssjp.ssjpfreq = ssjpprrs; // free storage
  memset(&zenv, 0, sizeof(ZRCVY_ENV));
  if (0 == enable_recovery(&zenv))
  {
    rc = iefssreq(&ssobp);
  }
  else
  {
    zjb_recovery_diag(zjb, "IEFSSREQ", &zenv);
    rc = RTNCD_FAILURE;
  }
  disable_recovery(&zenv);

  return rc;
}
