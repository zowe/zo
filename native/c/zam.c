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

#include "zam.h"
#include "dcbd.h"
#include "zam24.h"
#include "zamtypes.h"
#include "zdstype.h"
#include "zstorage.h"
#include "ztype.h"
#include "zenq.h"
#include "ihapsa.h"
#include "ikjtcb.h"
#include "ieftiot1.h"
#include "zutm31.h"
#include "ztime.h"
#include "zio.h"

register IO_CTRL *gioc ASMREG("r8");

typedef struct
{
  short int len;
  short int unused;
} BDW;

typedef struct
{
  short int len;
  short int unused;
} RDW;

static IO_CTRL *PTR32 new_write_io_ctrl(char *PTR32 ddname, int lrecl, int blkSize, unsigned char recfm)
{
  IO_CTRL *PTR32 ioc = new_io_ctrl();
  IHADCB *dcb = &ioc->dcb;
  memcpy(dcb, &dcb_write_model, sizeof(IHADCB));
  set_dcb_info(dcb, ddname, lrecl, blkSize, recfm);
  return ioc;
}

static IO_CTRL *PTR32 new_read_io_ctrl(char *PTR32 ddname, int lrecl, int blkSize, unsigned char recfm)
{
  IO_CTRL *ioc = new_io_ctrl();
  IHADCB *dcb = &ioc->dcb;
  memcpy(dcb, &dcb_read_model, sizeof(IHADCB));
  set_dcb_info(dcb, ddname, lrecl, blkSize, recfm);
  return ioc;
}

static int handle_dcb_abend(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, const char *PTR32 operation)
{
  if (ioc->dcb_abend)
  {
    if (0 == diag->e_msg_len)
    {
      strcpy(diag->service_name, operation);
      ZDIAG_SET_MSG(diag, "DCB abend during %.16s for %8.8s data set: %44.44s",
                    operation, ioc->ddname, ioc->jfcb.jfcbdsnm);
      diag->detail_rc = ZDS_RTNCD_DCB_ABEND_ERROR;
    }
    return RTNCD_FAILURE;
  }
  return 0;
}

static int validate_jfcb_attributes(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  // Legacy MVS password protection: OPEN on a password-protected data set
  // issues a WTOR to the operator console and waits for a reply. zo server
  // is a long-lived RPC server with no console, so that OPEN would hang the
  // request forever instead of returning an error. Fail fast, pre-OPEN.
  if (ioc->jfcb.jfcbind2 & (jfcsecur | jfcbrwpw))
  {
    ZDIAG_SET_MSG(diag, "DDname: %8.8s data set: %44.44s is password protected; copy it to an unprotected data set first",
                  ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm);
    diag->detail_rc = ZDS_RTNCD_UNSUPPORTED_DATA_SET;
    return RTNCD_FAILURE;
  }

  if (ioc->jfcb.jfcbind1 != jfcpds)
  {
    ZDIAG_SET_MSG(diag, "DDname: %8.8s data set: %44.44s is not a PDS: %X", ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm, ioc->jfcb.jfcbind1);
    diag->detail_rc = ZDS_RTNCD_UNSUPPORTED_DATA_SET;
    return RTNCD_FAILURE;
  }

  // ensure member name (e.g. is a partitioned data set)
  if (ioc->jfcb.jfcbelnm[0] == ' ')
  {
    ZDIAG_SET_MSG(diag, "DDname: %8.8s data set: %44.44s is not a partitioned data set: %.8s", ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm, ioc->jfcb.jfcbelnm);
    diag->detail_rc = ZDS_RTNCD_UNSUPPORTED_DSORG;
    return RTNCD_FAILURE;
  }

  return rc;
}

static int enq_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  QNAME qname = {0};
  RNAME rname = {0};
  memcpy(qname.value, "SPFEDIT ", sizeof(qname.value));
  rname.rlen = sprintf(rname.value, "%.*s%.*s", sizeof(ioc->jfcb.jfcbdsnm), ioc->jfcb.jfcbdsnm, sizeof(ioc->jfcb.jfcbelnm), ioc->jfcb.jfcbelnm);
  rc = enq(&qname, &rname);

  if (0 != rc)
  {
    diag->service_rc = rc;
    strcpy(diag->service_name, "ENQ");
    ZDIAG_SET_MSG(diag, "Failed to ENQ ddname: %8.8s data set: %44.44s rc was: %d", ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm, rc);
    diag->detail_rc = ZDS_RTNCD_ENQ_ERROR;
    return RTNCD_FAILURE;
  }
  ioc->has_enq = 1;

  return rc;
}

static int get_ucb(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  typedef struct tiot TIOT;
  PSA *psa = (PSA *)0;
  TCB *PTR32 tcb = psa->psatold;
  TIOT *PTR32 tiot = tcb->tcbtio;

  unsigned char tiot_entry_len = tiot->tioelngh;

  unsigned char *PTR32 tiot_entry = (unsigned char *PTR32)tiot;

  while (tiot_entry_len > 0)
  {
    if (0 == strncmp(tiot->tioeddnm, ioc->dcb.dcbddnam, sizeof(tiot->tioeddnm)))
    {
      unsigned char *PTR32 tioesttb = (unsigned char *PTR32) & tiot->tioesttb;
      memcpy(&ioc->ucb, &tiot->tioesttb, sizeof(unsigned int));
      break;
    }

    tiot_entry += tiot_entry_len;
    tiot = (TIOT * PTR32) tiot_entry;
    tiot_entry_len = tiot->tioelngh;
  }

#define MASK_24_BITS 0x00FFFFFF

  ioc->ucb = (ioc->ucb & MASK_24_BITS);
  if (0 == ioc->ucb)
  {
    diag->detail_rc = ZDS_RTNCD_UCB_ERROR;
    ZDIAG_SET_MSG(diag, "Failed to get UCB for data set: %44.44s", ioc->jfcb.jfcbdsnm);
    return RTNCD_FAILURE;
  }

  UCB *PTR32 ucb = (UCB * PTR32) ioc->ucb;
  return RTNCD_SUCCESS;
}

static int reserve_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  QNAME qname_reserve = {0};
  RNAME rname_reserve = {0};
  memcpy(qname_reserve.value, "SPFEDIT ", sizeof(qname_reserve.value));
  rname_reserve.rlen = sprintf(rname_reserve.value, "%.*s", sizeof(ioc->jfcb.jfcbdsnm), ioc->jfcb.jfcbdsnm);
  rc = reserve(&qname_reserve, &rname_reserve, (UCB * PTR32) ioc->ucb);
  if (0 != rc)
  {
    diag->service_rc = rc;
    strcpy(diag->service_name, "RESERVE");
    ZDIAG_SET_MSG(diag, "Failed to RESERVE ddname: %8.8s data set: %44.44s rc was: %d", ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm, rc);
    diag->detail_rc = ZDS_RTNCD_RESERVE_ERROR;
    return RTNCD_FAILURE;
  }

  ioc->has_reserve = 1;
  return rc;
}

static int open_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  rc = open_output_dcb(&ioc->dcb);
  if (0 != rc)
  {
    diag->service_rc = rc;
    strcpy(diag->service_name, "OPEN");
    ZDIAG_SET_MSG(diag, "Failed to open ddname: %8.8s for data set: %44.44s rc was: %d", ioc->dcb.dcbddnam, ioc->jfcb.jfcbdsnm, rc);
    diag->detail_rc = ZDS_RTNCD_OPEN_ERROR;
    return RTNCD_FAILURE;
  }

  if (handle_dcb_abend(diag, ioc, "OPEN"))
  {
    return RTNCD_FAILURE;
  }

  if (!(ioc->dcb.dcboflgs & dcbofopn))
  {
    ZDIAG_SET_MSG(diag, "Data set is not open: %44.44s", ioc->jfcb.jfcbdsnm);
    diag->detail_rc = ZDS_RTNCD_NOT_OPEN_ERROR;
    return RTNCD_FAILURE;
  }

  return rc;
}

static int validate_dcb_attributes(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  if (!(ioc->dcb.dcbrecfm & (dcbrecf | dcbrecv)))
  {
    ZDIAG_SET_MSG(diag, "Data set is not a fixed or variable record format: %X", ioc->dcb.dcbrecfm);
    diag->detail_rc = ZDS_RTNCD_UNSUPPORTED_RECFM;
    return RTNCD_FAILURE;
  }

  int block_size = ioc->dcb.dcbblksi;

  if (block_size < 1)
  {
    ZDIAG_SET_MSG(diag, "Data set has less than 1 block size: %X", block_size);
    diag->detail_rc = ZDS_RTNCD_UNSUPPORTED_BLOCK_SIZE;
    return RTNCD_FAILURE;
  }

  // For variable-length records, block size just needs to accommodate at least one record (LRECL + BDW)
  // For fixed-length records, block size must be a multiple of LRECL
  if (ioc->dcb.dcbrecfm & dcbrecf)
  {
    // Fixed-length record validation
    if (block_size % ioc->dcb.dcblrecl != 0)
    {
      ZDIAG_SET_MSG(diag, "Data set block size is not a multiple of the record length: %d not evenly divisible by %d", ioc->dcb.dcbblksi, ioc->dcb.dcblrecl);
      diag->detail_rc = ZDS_RTNCD_INVALID_BLOCK_SIZE;
      return RTNCD_FAILURE;
    }
  }
  else if (ioc->dcb.dcbrecfm & dcbrecv)
  {
    // Variable-length record validation: block size must be >= LRECL + 4 (for BDW)
    if (block_size < ioc->dcb.dcblrecl + sizeof(BDW))
    {
      ZDIAG_SET_MSG(diag, "Data set block size is too small for variable records: %d < %d (LRECL + 4)", block_size, ioc->dcb.dcblrecl + sizeof(BDW));
      diag->detail_rc = ZDS_RTNCD_INVALID_BLOCK_SIZE;
      return RTNCD_FAILURE;
    }
  }

  return rc;
}

static int note_member(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, NOTE_RESPONSE *PTR32 note_response)
{

  int rc = 0;
  int rsn = 0;

  rc = note(ioc, note_response, &rsn);
  if (0 != rc)
  {
    if (0 == diag->e_msg_len)
    {
      diag->service_rc = rc;
      strcpy(diag->service_name, "NOTE");
      ZDIAG_SET_MSG(diag, "Failed to NOTE ddname: %8.8s data set: %44.44s rc was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rc);
      diag->detail_rc = ZDS_RTNCD_NOTE_ERROR;
    }
  }

  return rc;
}

#define MAX_SYSLOG_LRECL 256
int open_input_vsam(ZDIAG *PTR32 diag, IO_CTRL * PTR32 * PTR32 ioc, const char *PTR32 ddname)
{
  int rc = 0;
  int rsn = 0;
  IO_CTRL *PTR32 ioc31 = NULL;

  //
  // Obtain IO_CTRL for the data set
  //
  IO_CTRL *PTR32 new_ioc = new_io_ctrl();
  *ioc = new_ioc;
  void *acbp = &new_ioc->ifgacb;

  ACB_MODEL(dsa_acb_model); // stack var
  memcpy(&dsa_acb_model, &acb_model, sizeof(IFGACB));

  memcpy(&new_ioc->ifgacb, &dsa_acb_model, sizeof(IFGACB));
  memcpy(new_ioc->ifgacb.acbddnm, ddname, sizeof(new_ioc->ifgacb.acbddnm));

  IFGRPL *rplp = &new_ioc->rpl;
  memcpy(rplp, &rpl_model, sizeof(IFGRPL));

  new_ioc->buffer_size = MAX_SYSLOG_LRECL; // NOTE(Kelosky): in the future for other VSAM data sets, this could be set to the actual LRECL, we could use something like stvsmlrl
  new_ioc->buffer = storage_obtain31(new_ioc->buffer_size);

  MODCB_MODEL(plist);
  MODCB(rplp, acbp, new_ioc->buffer, new_ioc->buffer_size, new_ioc->buffer_size, plist, rc, rsn);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "MODCB");
    ZDIAG_SET_MSG(diag, "MODCB failed rc was: %d rsn was: %d", rc, rsn);
    diag->service_rc = rc;
    diag->service_rsn = rsn;
    return RTNCD_FAILURE;
  }

  rc = open_input_acb(&new_ioc->ifgacb);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "OPEN");
    ZDIAG_SET_MSG(diag, "Failed to open acb rc was: %d", rc);
    diag->service_rc = rc;
    return RTNCD_FAILURE;
  }

  return rc;
}

// https://www.ibm.com/docs/en/zos/3.2.0?topic=interface-special-processing-logical-syslog-data-sets
#define FIRST_OCCURRENCE 0xFF00
#define NEXT_OCCURRENCE 0xFF01
#define PREV_OCCURRENCE 0xFF02
#define OFF_CURRENT_RECORD 0xFF03
#define ABSOLUTE_RECORD 0xFF04

// TODO(Kelosky): this should have accepted a etod_t instead of a TIME_STRUCT
int point_input_vsam(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, TIME_STRUCT *time_struct)
{
  int rc = 0;

  unsigned short request = FIRST_OCCURRENCE;

  etod_t etod = {0};

  rc = convetod(time_struct, &etod);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "CONVTOD");
    ZDIAG_SET_MSG(diag, "Failed to CONVTOD rc was: %d", rc);
    diag->service_rc = rc;
    return rc;
  }

  IFGRPL *rplp = &ioc->rpl;

  memcpy(&rplp->rplaixpc, &request, sizeof(rplp->rplaixpc));
  memcpy(&rplp->rplrbar.rplrbarx, etod, sizeof(rplp->rplrbar.rplrbarx));
  void *PTR32 larg = &rplp->rplrbar;
  memcpy(&rplp->rplarg, &larg, sizeof(rplp->rplarg));

  POINT(rplp, rc);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "POINT");
    ZDIAG_SET_MSG(diag, "Failed to POINT rc was: %d", rc);
    diag->service_rc = rc;
    return rc;
  }

  return rc;
}

int read_input_vsam(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  memcpy(ioc->dsinf.dsineye, "DSIN", sizeof(ioc->dsinf.dsineye));
  IFGRPL *rplp = &ioc->rpl;
  rplp->rplermsa = &ioc->dsinf;
  rplp->rplemlen = dsinsiz1;

  GET(rplp, rc);
  if (0 != rc)
  {
// https://www.ibm.com/docs/en/zos/3.1.0?topic=uai-return-codes
#define rpleoder 0x04
    if (rplp->rplerreg == rplloger && rplp->rplfdb3 == rpleoder)
    {
      ioc->eof = 1;
      return RTNCD_WARNING;
    }
    else
    {
      unsigned char rplrdbk[3] = {0};
      diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
      strcpy(diag->service_name, "GET");
      memcpy(rplrdbk, &rplp->rplfdbwd.rplfdbk, sizeof(rplrdbk));
      ZDIAG_SET_MSG(diag, "Failed to GET rc was: %d, RPLRDBK was: %02X%02X%02X", rc, rplrdbk[0], rplrdbk[1], rplrdbk[2]);
      diag->service_rc = rc;
      return rc;
    }
  }

  return rc;
}

int close_input_vsam(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  rc = close_acb(&ioc->ifgacb);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "CLOSE");
    ZDIAG_SET_MSG(diag, "Failed to close acb rc was: %d", rc);
    diag->service_rc = rc;
    return RTNCD_FAILURE;
  }

  if (ioc->buffer)
  {
    storage_release(ioc->buffer_size, ioc->buffer);
    ioc->buffer = NULL;
    ioc->buffer_size = 0;
  }

  if (ioc)
  {
    storage_release(sizeof(IO_CTRL), ioc);
  }

  return rc;
}

int open_output_bpam(ZDIAG *PTR32 diag, IO_CTRL * PTR32 * PTR32 ioc, const char *PTR32 ddname)
{
  int rc = 0;
  IO_CTRL *PTR32 ioc31 = NULL;

  //
  // Obtain IO_CTRL for the data set
  //
  IO_CTRL *PTR32 new_ioc = new_io_ctrl();
  *ioc = new_ioc;
  memcpy(&new_ioc->dcb, &dcb_write_model, sizeof(IHADCB));
  memcpy(new_ioc->dcb.dcbddnam, ddname, sizeof(new_ioc->dcb.dcbddnam));
  memcpy(new_ioc->ddname, ddname, sizeof(new_ioc->ddname));

  gioc = new_ioc;

  //
  // Read the JFCB for the data set
  //
  rc = read_output_jfcb(new_ioc);
  if (0 != rc)
  {
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    strcpy(diag->service_name, "RDJFCB");
    ZDIAG_SET_MSG(diag, "Failed to read output JFCB rc was: %d", rc);
    diag->service_rc = rc;
    return RTNCD_FAILURE;
  }

  //
  // Validate attributes of the data set
  //
  rc = validate_jfcb_attributes(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  // ENQ data set
  //
  rc = enq_data_set(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  // Get the UCB for the data set
  //
  rc = get_ucb(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  // RESERVE the data set
  //
  rc = reserve_data_set(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  // Set attributes of the DCB
  //
  new_ioc->dcb.dcbrecfm = new_ioc->jfcb.jfcrecfm; // copy allocation attributes
  new_ioc->dcb.dcbdsrg1 = dcbdsgpo;               // DSORG=PO

  //
  // Open the data set
  //
  rc = open_data_set(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  // Validate record format of the data set
  //
  rc = validate_dcb_attributes(diag, new_ioc);
  if (0 != rc)
  {
    return rc;
  }

  //
  //  Obtain a buffer for the data set
  //
  new_ioc->buffer_size = new_ioc->dcb.dcbblksi;
  new_ioc->buffer = storage_obtain31(new_ioc->buffer_size);

  // init vars
  new_ioc->bytes_in_buffer = 0;
  new_ioc->lines_written = 0;
  new_ioc->free_location = new_ioc->buffer;

  return rc;
}

static int handle_fixed_record(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, const char *PTR32 data, int length)
{
  int rc = 0;
  int lrecl = ioc->dcb.dcblrecl;
  int blocksize = ioc->dcb.dcbblksi;

  memset(ioc->free_location, ' ', lrecl);
  memcpy(ioc->free_location, data, length);

  ioc->lines_written++;

  // track bytes in buffer and free space
  ioc->bytes_in_buffer += lrecl;
  ioc->free_location += lrecl;

  if (ioc->bytes_in_buffer >= blocksize)
  {
    rc = write_sync(ioc, ioc->buffer);

    if (handle_dcb_abend(diag, ioc, "WRITE"))
    {
      return RTNCD_FAILURE;
    }

    if (0 != rc)
    {
      ZDIAG_SET_MSG(diag, "Failed to write record rc was: %d", rc);
      diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
      diag->service_rc = rc;
      return RTNCD_FAILURE;
    }
    ioc->bytes_in_buffer = 0;
    ioc->free_location = ioc->buffer;
    // memset(ioc->buffer, ' ', ioc->buffer_size);
  }

  return rc;
}

static void init_bdw(IO_CTRL *PTR32 ioc)
{
  BDW *PTR32 bdw_ptr = (BDW * PTR32) ioc->buffer;
  bdw_ptr->unused = 0;

  if (0 == ioc->bytes_in_buffer)
  {
    ioc->free_location += sizeof(BDW);
    ioc->bytes_in_buffer = sizeof(BDW);
  }
}

static int write_variable_record(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, const char *PTR32 data, int length)
{
  int rc = 0;

  BDW *PTR32 bdw_ptr = (BDW * PTR32) ioc->buffer;

  int blocksize = ioc->dcb.dcbblksi;
  // set the bdw
  bdw_ptr->len = ioc->bytes_in_buffer;

  // write the record and reinit
  ioc->dcb.dcbblksi = ioc->bytes_in_buffer; // temporary update block size before writing

  rc = write_sync(ioc, ioc->buffer);
  ioc->dcb.dcbblksi = blocksize; // restore block size before we do anything else

  if (handle_dcb_abend(diag, ioc, "WRITE"))
  {
    return RTNCD_FAILURE;
  }

  if (0 != rc)
  {
    ZDIAG_SET_MSG(diag, "Failed to write record rc was: %d", rc);
    diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
    diag->service_rc = rc;
    return RTNCD_FAILURE;
  }

  ioc->free_location = ioc->buffer;
  ioc->free_location += sizeof(BDW);
  ioc->bytes_in_buffer = sizeof(BDW);
  bdw_ptr->len = 0;

  return rc;
}

static void copy_variable_record(IO_CTRL *PTR32 ioc, const char *PTR32 data, int length)
{
  ioc->lines_written++;
  RDW *PTR32 rdw_ptr = (RDW * PTR32) ioc->free_location;
  rdw_ptr->unused = 0;
  memcpy(ioc->free_location + sizeof(RDW), data, length);
  rdw_ptr->len = length + sizeof(RDW);
  ioc->bytes_in_buffer += rdw_ptr->len;
  ioc->free_location += rdw_ptr->len;
}

static int handle_variable_record(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, const char *PTR32 data, int length)
{
  int rc = 0;

  int blocksize = ioc->dcb.dcbblksi;
  init_bdw(ioc);
  if (!(ioc->dcb.dcbrecfm & dcbrecbr))
  {
    copy_variable_record(ioc, data, length);
    rc = write_variable_record(diag, ioc, data, length);
  }
  else
  {

    // block size minus bytes in buffer equals bytes remaining
    int bytes_remaining = blocksize - ioc->bytes_in_buffer;

    // if the remaining bytes are not enough to fit the record and rdw OR it is not blocked, we need to write the record and reinit
    // a record that exactly fills the remaining space still fits (e.g. VB LRECL 84 BLKSIZE 88 with an 80-byte record)
    if (length + (int)sizeof(RDW) > bytes_remaining || !(ioc->dcb.dcbrecfm & dcbrecbr))
    {
      rc = write_variable_record(diag, ioc, data, length);
      if (0 != rc)
      {
        return rc;
      }
    }

    // recalculate bytes remaining
    bytes_remaining = blocksize - ioc->bytes_in_buffer;

    // add this record to the buffer (which may have just been written)
    if (length + (int)sizeof(RDW) <= bytes_remaining)
    {
      copy_variable_record(ioc, data, length);
    }
  }

  return rc;
}

int write_output_bpam(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, const char *PTR32 data, int length)
{
  int rc = 0;

  int lrecl = ioc->dcb.dcblrecl;
  int blocksize = ioc->dcb.dcbblksi;

  gioc = ioc;

  if (ioc->dcb.dcbrecfm & dcbrecv)
  {
    // Truncate line to fit within LRECL (accounting for RDW)
    int max_data_len = lrecl - sizeof(RDW);
    if (length > max_data_len)
    {
      length = max_data_len;
    }
    rc = handle_variable_record(diag, ioc, data, length);
  }
  else
  {
    // Truncate line to fit within LRECL for fixed-length records
    if (length > lrecl)
    {
      length = lrecl;
    }
    rc = handle_fixed_record(diag, ioc, data, length);
  }

  return rc;
}

static int write_flush(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  int blocksize = ioc->dcb.dcbblksi;
  if (ioc->dcb.dcboflgs & dcbofopn)
  {
    if (ioc->bytes_in_buffer > 0)
    {
      // if variable blocked, set the bdw
      if (ioc->dcb.dcbrecfm & dcbrecv)
      {

        if (ioc->bytes_in_buffer <= sizeof(BDW)) // if only BDW is in the buffer, we skip writing
        {
          return rc;
        }

        BDW *PTR32 bdw_ptr = (BDW * PTR32) ioc->buffer;
        bdw_ptr->len = ioc->bytes_in_buffer;
      }

      ioc->dcb.dcbblksi = ioc->bytes_in_buffer; // temporary update block size before writing
      rc = write_sync(ioc, ioc->buffer);
      ioc->dcb.dcbblksi = blocksize; // restore block size before we do anything else

      if (handle_dcb_abend(diag, ioc, "WRITE"))
      {
        return RTNCD_FAILURE;
      }

      if (0 != rc)
      {
        ZDIAG_SET_MSG(diag, "Failed to write record rc was: %d", rc);
        diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
        diag->service_rc = rc;
        return RTNCD_FAILURE;
      }

      ioc->bytes_in_buffer = 0;
      ioc->free_location = ioc->buffer;
    }
  }
  return rc;
}

static int bldl_member(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc, BLDL_PL *PTR32 bldl_pl)
{
  int rc = 0;
  int rsn = 0;
  bldl_pl->ff = 1;                                                            // only one member in the list
  bldl_pl->ll = sizeof(bldl_pl->list);                                        // length of each entry
  memcpy(bldl_pl->list.name, ioc->jfcb.jfcbelnm, sizeof(ioc->jfcb.jfcbelnm)); // copy member name

  rc = bldl(ioc, bldl_pl, &rsn);

  if (0 != rc)
  {
    diag->service_rc = rc;
    strcpy(diag->service_name, "BLDL");
    ZDIAG_SET_MSG(diag, "Failed to BLDL ddname: %8.8s data set: %44.44s rc was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rc);
    diag->detail_rc = ZDS_RTNCD_BLDL_ERROR;
  }
  return rc;
}

static int update_ispf_statistics(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  // Skip ISPF stats for undefined record format (RECFM=U)
  if ((ioc->dcb.dcbrecfm & dcbrecu) == dcbrecu)
  {
    return RTNCD_SUCCESS;
  }

  if (ioc->dcb.dcboflgs & dcbofopn)
  {
    //
    // BLDL for the data set that exists or was just created
    //
    BLDL_PL bldl_pl = {0};
#define BLDL_WARNING 4
    rc = bldl_member(diag, ioc, &bldl_pl);
    if (0 != rc && rc != BLDL_WARNING)
    {
      return rc;
    }
    // reset warning message if it exists
    rc = RTNCD_SUCCESS;
    // memset(diag->e_msg, 0, diag->e_msg_len);
    diag->e_msg_len = 0;

    //
    // Create or update ISPF statistics
    //
    memcpy(ioc->stow_list.name, bldl_pl.list.name, sizeof(bldl_pl.list.name));
    ISPF_STATS *statsp = (ISPF_STATS *)ioc->stow_list.user_data;

    if ((bldl_pl.list.c & LEN_MASK) == 0)
    {
      ioc->stow_list.c = ISPF_STATS_MIN_LEN;

      //
      // Ensure this random spot in the user data is set to spaces :-(
      //
      memset(statsp->extended, ' ', sizeof(statsp->extended)); // NOTE(Kelosky): unclear what the 2 remaining bytes are for but if not set to spaces, stats will not be updated for a newly created member

      //
      // Set initial user
      //
      char user[8] = {0};
      rc = zutm1gur(user);
      if (0 != rc)
      {
        ZDIAG_SET_MSG(diag, "Failed to get userid rc was: %d", rc);
        diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
        diag->service_rc = rc;
        return RTNCD_FAILURE;
      }
      memcpy(statsp->userid, user, sizeof(user));

      //
      // Set initial version and level
      //
      statsp->version = 0x01;
      statsp->level = 0x00;
      statsp->flags = 0x00;

      //
      // Set initial and current number of lines
      //
      statsp->initial_number_of_lines = ioc->lines_written; // update ISPF statistics number of lines
      statsp->current_number_of_lines = ioc->lines_written; // update ISPF statistics number of lines

      //
      // Set created andmodified time
      //
      TIME_UNION timel = {0};
      unsigned int datel = 0;
      time_local(&timel.timei, &datel);

      memcpy(&statsp->created_date_century, &datel, sizeof(datel));
      memcpy(&statsp->modified_date_century, &datel, sizeof(datel));

      statsp->modified_time_hours = timel.times.HH;   // update ISPF statistics time hours
      statsp->modified_time_minutes = timel.times.MM; // update ISPF statistics time minutes
      statsp->modified_time_seconds = timel.times.SS; // update ISPF statistics time seconds
    }
    else
    {
      //
      // Initialize with existing user data / statistics
      //
      ioc->stow_list.c = bldl_pl.list.c;                                       // copy user data length
      int user_data_len = (bldl_pl.list.c & LEN_MASK) * 2;                     // isolate number of halfwords in user data
      memcpy(ioc->stow_list.user_data, bldl_pl.list.user_data, user_data_len); // copy all user data

      //
      // Set modified user
      //
      char user[8] = {0};
      rc = zutm1gur(user);
      if (0 != rc)
      {
        ZDIAG_SET_MSG(diag, "Failed to get userid rc was: %d", rc);
        diag->detail_rc = ZDS_RTNCD_SERVICE_FAILURE;
        diag->service_rc = rc;
        return RTNCD_FAILURE;
      }
      memcpy(statsp->userid, user, sizeof(user));
//
//    Increment modification level (level 0x99 is the max):
//    https://www.ibm.com/docs/en/zos/3.2.0?topic=environment-version-modification-level-numbers
//
#define MAX_LEVEL 0x99
      if (statsp->level < MAX_LEVEL)
      {
        statsp->level++; // update ISPF statistics level
      }

      //
      // Set modified number of lines and current number of lines
      //
      statsp->modified_number_of_lines = ioc->lines_written; // update ISPF statistics number of lines
      statsp->current_number_of_lines = ioc->lines_written;  // update ISPF statistics number of lines

      //
      // Set modified time
      //
      TIME_UNION timel = {0};
      unsigned int datel = 0;
      time_local(&timel.timei, &datel);

      memcpy(&statsp->modified_date_century, &datel, sizeof(datel));

      statsp->modified_time_hours = timel.times.HH;
      statsp->modified_time_minutes = timel.times.MM;
      statsp->modified_time_seconds = timel.times.SS;
    }
  }

  return rc;
}

static int stow_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  int rsn = 0;
  if (ioc->dcb.dcboflgs & dcbofopn)
  {

    rc = stow(ioc, &rsn);
    if (0 != rc)
    {
      if (0 == diag->e_msg_len)
      {
        diag->service_rc = rc;
        strcpy(diag->service_name, "STOW");
        ZDIAG_SET_MSG(diag, "Failed to STOW ISPF statistics: %8.8s data set: %44.44s rsn was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rsn);
        diag->detail_rc = ZDS_RTNCD_STOW_ERROR;
      }
    }
  }
  return rc;
}

static int close_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  if (ioc->dcb.dcboflgs & dcbofopn)
  {
    rc = close_dcb(&ioc->dcb);

    if (handle_dcb_abend(diag, ioc, "CLOSE"))
    {
      return RTNCD_FAILURE;
    }

    if (0 != rc && 0 == diag->e_msg_len) // only set error if no error message was already set
    {
      diag->service_rc = rc;
      strcpy(diag->service_name, "CLOSE");
      ZDIAG_SET_MSG(diag, "Failed to close ddname: %8.8s data set: %44.44s rc was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rc);
      diag->detail_rc = ZDS_RTNCD_CLOSE_ERROR;
    }
  }
  return rc;
}

static int deq_reserve_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  if (ioc->has_reserve)
  {
    QNAME qname_reserve = {0};
    RNAME rname_reserve = {0};
    memcpy(qname_reserve.value, "SPFEDIT ", sizeof(qname_reserve.value));
    rname_reserve.rlen = sprintf(rname_reserve.value, "%.*s", sizeof(ioc->jfcb.jfcbdsnm), ioc->jfcb.jfcbdsnm);
    rc = deq_reserve(&qname_reserve, &rname_reserve, (UCB * PTR32) ioc->ucb);
    if (0 != rc)
    {
      if (0 == diag->e_msg_len) // only set error if no error message was already set
      {
        diag->service_rc = rc;
        strcpy(diag->service_name, "DEQ RESERVE");
        ZDIAG_SET_MSG(diag, "Failed to DEQ RESERVE ddname: %8.8s data set: %44.44s rc was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rc);
        diag->detail_rc = ZDS_RTNCD_DEQ_RESERVE_ERROR;
      }
      return rc;
    }
  }
  return rc;
}

static int deq_data_set(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;

  if (ioc->has_enq)
  {
    RNAME rname = {0};
    QNAME qname = {0};
    memcpy(qname.value, "SPFEDIT ", sizeof(qname.value));
    rname.rlen = sprintf(rname.value, "%.*s%.*s", sizeof(ioc->jfcb.jfcbdsnm), ioc->jfcb.jfcbdsnm, sizeof(ioc->jfcb.jfcbelnm), ioc->jfcb.jfcbelnm);
    rc = deq(&qname, &rname);
    if (0 != rc)
    {
      if (0 == diag->e_msg_len) // only set error if no error message was already set
      {
        diag->service_rc = rc;
        strcpy(diag->service_name, "DEQ");
        ZDIAG_SET_MSG(diag, "Failed to DEQ ddname: %8.8s data set: %44.44s rc was: %d", ioc->ddname, ioc->jfcb.jfcbdsnm, rc);
        diag->detail_rc = ZDS_RTNCD_DEQ_ERROR;
      }
    }
  }
  return rc;
}

static int finalize_dcb(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  int first_rc = 0;

  //
  // Update ISPF statistics
  //
  rc = update_ispf_statistics(diag, ioc);
  if (0 != rc)
  {
    if (0 == first_rc)
      first_rc = rc;
  }

  //
  // STOW the ISPF statistics
  //
  if (0 == rc)
  {
    rc = stow_data_set(diag, ioc);
    if (0 != rc)
    {
      if (0 == first_rc)
        first_rc = rc;
    }
  }

  //
  // Close the data set
  //
  rc = close_data_set(diag, ioc);
  if (0 != rc)
  {
    if (0 == first_rc)
      first_rc = rc;
  }

  return first_rc;
}

// TODO(Kelosky): handle when each fails... continue or return?
int close_output_bpam(ZDIAG *PTR32 diag, IO_CTRL *PTR32 ioc)
{
  int rc = 0;
  int first_rc = 0;

  gioc = ioc;

  //
  // Write any remaining bytes in the buffer
  //
  rc = write_flush(diag, ioc);
  if (0 != rc)
  {
    first_rc = rc;
  }

  //
  // Per IBM docs: check whether the DCB is really open before issuing
  // any other macros using that DCB, other than CLOSE or FREEPOOL.
  // If write_flush encountered an E37 and the DCB was silently closed,
  // skip STOW and CLOSE but still perform DEQ and cleanup below.
  //
  if (ioc->dcb.dcboflgs & dcbofopn)
  {
    rc = finalize_dcb(diag, ioc);
    if (0 != rc && 0 == first_rc)
    {
      first_rc = rc;
    }
  }

  //
  // Release the buffer
  //
  if (ioc->buffer)
  {
    storage_release(ioc->buffer_size, ioc->buffer);
    ioc->buffer = NULL;
    ioc->buffer_size = 0;
  }

  //
  // DEQ the reserve data set
  //
  rc = deq_reserve_data_set(diag, ioc);
  if (0 != rc && 0 == first_rc)
  {
    first_rc = rc;
  }

  //
  // DEQ the data set
  //
  rc = deq_data_set(diag, ioc);
  if (0 != rc && 0 == first_rc)
  {
    first_rc = rc;
  }

  if (ioc->zam24)
  {
    storage_release(ioc->zam24_len, ioc->zam24);
    ioc->zam24 = NULL;
  }

  storage_release(sizeof(IO_CTRL), ioc);

  return first_rc;
}

IO_CTRL *open_output_assert(char *ddname, int lrecl, int blkSize, unsigned char recfm)
{
  IO_CTRL *ioc = new_write_io_ctrl(ddname, lrecl, blkSize, recfm);
  IHADCB *dcb = &ioc->dcb;
  int rc = 0;
  rc = open_output_dcb(dcb);
  dcb->dcbdsrg1 = dcbdsgps; // DSORG=PS
  ioc->output = 1;
  if (0 != rc)
    s0c3_abend(OPEN_OUTPUT_ASSERT_RC);
  if (!(dcbofopn & dcb->dcboflgs))
    s0c3_abend(OPEN_OUTPUT_ASSERT_FAIL);

  return ioc;
}

void close_assert(IO_CTRL *ioc)
{
  IHADCB *dcb = &ioc->dcb;
  FILE_CTRL *fc = dcb->dcbdcbe;

  if (dcb->dcboflgs & dcbofopn)
  {
    int rc = close_dcb(dcb);
    if (0 != rc)
    {
      s0c3_abend(CLOSE_ASSERT_RC);
    }
    // free DCBE / file control if obtained
    if (fc && ioc->input)
    {
      // FILE_CTRL *fc = dcb->dcbdcbe;
      storage_release(fc->ctrl_len, fc);
    }
  }

  if (ioc->zam24)
  {
    storage_release(ioc->zam24_len, ioc->zam24);
    ioc->zam24 = NULL;
  }

  storage_release(sizeof(IO_CTRL), ioc);
}

int find_member(IO_CTRL *ioc, int *rsn)
{
  int rc = 0;
  int local_rsn = 0;
  FIND(ioc->dcb, ioc->jfcb.jfcbelnm, rc, local_rsn);
  *rsn = local_rsn;
  return rc;
}

int bldl(IO_CTRL *ioc, BLDL_PL *bldl_pl, int *rsn)
{
  int rc = 0;
  int local_rsn = 0;
  BLDL(ioc->dcb, bldl_pl->ff, rc, local_rsn);
  *rsn = local_rsn;
  return rc;
}

int stow(IO_CTRL *ioc, int *rsn)
{
  int rc = 0;
  int local_rsn = 0;
  STOW(ioc->dcb, ioc->stow_list, rc, local_rsn);
  *rsn = local_rsn;
  return rc;
}

int note(IO_CTRL *ioc, NOTE_RESPONSE *PTR32 note_response, int *rsn)
{
  int rc = 0;
  int local_rsn = 0;
  NOTE(ioc->dcb, *note_response, rc, local_rsn);
  *rsn = local_rsn;
  return rc;
}

#pragma prolog(ZAMDEXIT, " ZWEPROLG NEWDSA=(YES,24),SAVE=BAKR ")
#pragma epilog(ZAMDEXIT, " ZWEEPILG ")
int ZAMDEXIT(DCB_ABEND_PL *PTR32 plist)
{
  gioc->dcb_abend = 1; // note DCBABEND

  // Some abends cannot be ignored or delayed (such as SB14). Default is worst-case scenario of following through w/ termination.
  int rc = DCB_ABEND_RC_TERMINATE;
  if (plist->option_mask & DCB_ABEND_OPT_OK_TO_IGNORE)
  {
    // If the abend is safe to ignore according to the option mask, tell the system to ignore it (e.g. SE37 out-of-space)
    // EOV abends continue to terminate and ignore this option, so ESTAEX will handle them instead.
    rc = DCB_ABEND_RC_IGNORE;
  }
  else if (plist->option_mask & DCB_ABEND_OPT_OK_TO_DELAY)
  {
    // If ignoring is not possible, we can sometimes delay the abend until all the DCBs in the same OPEN or CLOSE macro are opened or closed
    rc = DCB_ABEND_RC_DELAY;
  }

  return rc;
}

typedef int (*ZAM24Fn)();

static void setup_exit_list(IO_CTRL *ioc)
{
  // Ref: https://www.ibm.com/docs/en/zos/2.5.0?topic=routines-dcb-exit-list
  int zam24_len = ZAM24Q();
  ioc->zam24_len = zam24_len;
  ioc->zam24 = storage_obtain24(zam24_len);
  // Copy function to local variable to avoid memcpy cast error
  ZAM24Fn x = ZAM24;
  memcpy(ioc->zam24, (void *)x, zam24_len);

  // DCB abend exit that invokes ZAMDEXIT
  ioc->exlst[0].exlentrb = (unsigned int)ioc->zam24;
  ioc->exlst[0].exlcodes = exldcbab;
  // "End-of-list" entry (used as delimiter)
  ioc->exlst[1].exlentrb = (unsigned int)&ioc->jfcb;
  ioc->exlst[1].exlcodes = exllaste + exlrjfcb;

  memcpy(&ioc->rdjfcb_pl, &rdfjfcb_model, sizeof(RDJFCB_PL));

  unsigned char recfm = ioc->dcb.dcbrecfm;
  void *PTR32 exlst = &ioc->exlst;
  memcpy(&ioc->dcb.dcbexlst, &exlst, sizeof(ioc->dcb.dcbexlst));
  ioc->dcb.dcbrecfm = recfm;
}

// TODO: This function needs to be reworked to address some issues around opening a JFCB for input. Avoid using for now
int read_input_jfcb(IO_CTRL *ioc)
{
  int rc = 0;
  setup_exit_list(ioc);
  RDJFCB(ioc->dcb, ioc->rdjfcb_pl, rc, INPUT);
  return rc;
}

int read_output_jfcb(IO_CTRL *ioc)
{
  int rc = 0;
  setup_exit_list(ioc);
  RDJFCB(ioc->dcb, ioc->rdjfcb_pl, rc, OUTPUT);
  return rc;
}

int open_output_dcb(IHADCB *dcb)
{
  int rc = 0;
  OPEN_PL opl = {0};
  opl.option = OPTION_BYTE;

  OPEN(*dcb, opl, rc, OUTPUT);
  return rc;
}

int open_input_dcb(IHADCB *dcb)
{
  int rc = 0;
  OPEN_PL opl = {0};
  opl.option = OPTION_BYTE;

  OPEN(*dcb, opl, rc, INPUT);
  return rc;
}

// https://www.ibm.com/docs/en/zos/3.2.0?topic=sets-using-vsam-macros-in-programs
// https://www.ibm.com/docs/en/zos/3.2.0?topic=sets-vsam-macro-instructions
// https://www.ibm.com/docs/en/zos/3.2.0?topic=macros-use-list-execute-generate-forms-vsam
// https://www.ibm.com/docs/en/zos/3.2.0?topic=instructions-vsam-macro-descriptions-examples
// https://www.ibm.com/docs/en/zos/3.2.0?topic=interface-special-processing-logical-syslog-data-sets
// https://www.ibm.com/docs/en/zos/3.2.0?topic=interface-using-rpl-based-macros
int open_input_acb(IFGACB *acb)
{
  int rc = 0;

  OPEN_MODEL(dsa_open_model);  // stack var
  dsa_open_model = open_model; // copy model

  OPEN_ACB(*acb, dsa_open_model, rc);

  return rc;
}

int close_acb(IFGACB *acb)
{
  int rc = 0;
  CLOSE_PL cpl = {0};
  cpl.option = OPTION_BYTE;

  CLOSE_ACB(*acb, cpl, rc);
  return rc;
}

int snap(IHADCB *dcb, SNAP_HEADER *header, void *start, int len)
{
  int rc = 0;
  SNAP_PLIST plist = {0};
  unsigned char *end = (unsigned char *)start + len;
  SNAP(*dcb, *header, *(unsigned char *)start, *end, plist, rc);
  return rc;
}

int write_dcb(IHADCB *dcb, WRITE_PL *wpl, char *buffer)
{
  int rc = 0;
  memset(wpl, 0x00, sizeof(WRITE_PL));
  WRITE(*dcb, *wpl, *buffer, &rc);
  // TODO(Kelosky): rc doesn't matter for fixed length records
  rc = 0;
  return rc;
}

// NOTE(Kelosky): simple function that is non inline so that when
// it is called, NAB will be set. This is needed for the CHECK macro
// which could trigger a call to EODAD (which requires NAB to be set).
void force_nab() ATTRIBUTE(noinline);
void force_nab()
{
  return;
}

int check(DECB *cpl)
{
  int rc = 0;
  force_nab();
  CHECK(*cpl, rc)
  rc = 0;
  return rc;
}

int close_dcb(IHADCB *dcb)
{
  int rc = 0;
  CLOSE_PL cpl = {0};
  cpl.option = OPTION_BYTE;
  CLOSE(*dcb, cpl, rc);
  return rc;
}

// TODO(Kelosky): in the future, perhaps have a non-sync write?
int write_sync(IO_CTRL *ioc, char *buffer)
{
  int rc = 0;
  WRITE_PL *wpl = &ioc->decb;
  rc = write_dcb(&ioc->dcb, wpl, buffer);
  if (0 != rc)
  {
    return rc;
  }

  return check(&ioc->decb);
}
