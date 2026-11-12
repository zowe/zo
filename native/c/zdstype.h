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

#ifndef ZDSTYPE_H
#define ZDSTYPE_H

#include <stdint.h>
#include "ztype.h"

// RTNCD_CODE_SUCCESS ztype.h         -1
#define ZDS_RTNCD_SERVICE_FAILURE -2
#define ZDS_RTNCD_MAX_JOBS_REACHED -3
#define ZDS_RTNCD_INSUFFICIENT_BUFFER -4
#define ZDS_RTNCD_NOT_FOUND -5
#define ZDS_RTNCD_CATALOG_ERROR -6
#define ZDS_RTNCD_ENTRY_ERROR -7
#define ZDS_RTNCD_UNSUPPORTED_ERROR -8
#define ZDS_RTNCD_UNEXPECTED_ERROR -9
#define ZDS_RTNCD_PARSING_ERROR -10
#define ZDS_RTNCD_UNSUPPORTED_DATA_SET -11
#define ZDS_RTNCD_UNSUPPORTED_DSORG -12
#define ZDS_RTNCD_UNSUPPORTED_RECFM -13
#define ZDS_RTNCD_ENQ_ERROR -14
#define ZDS_RTNCD_RESERVE_ERROR -15
#define ZDS_RTNCD_UCB_ERROR -16
#define ZDS_RTNCD_OPEN_ERROR -17
#define ZDS_RTNCD_NOT_OPEN_ERROR -18
#define ZDS_RTNCD_UNSUPPORTED_BLOCK_SIZE -19
#define ZDS_RTNCD_INVALID_BLOCK_SIZE -20
#define ZDS_RTNCD_CLOSE_ERROR -21
#define ZDS_RTNCD_DEQ_ERROR -22
#define ZDS_RTNCD_DEQ_RESERVE_ERROR -23
#define ZDS_RTNCD_BLDL_ERROR -24
#define ZDS_RTNCD_STOW_ERROR -25
#define ZDS_RTNCD_INVALID_DATA_LENGTH -26
#define ZDS_RTNCD_NOTE_ERROR -27
#define ZDS_RTNCD_DCB_ABEND_ERROR -28

#define ZDS_RSNCD_MAXED_ENTRIES_REACHED -1
#define ZDS_RSNCD_NOT_FOUND -2
#define ZDS_RSNCD_TRUNCATION_WARNING -3

#define ZDS_RSNCD_NO_IO_CTRL -1
#define ZDS_RSNCD_NOT_DYNALLOC -2

#define ZDS_DEFAULT_BUFFER_SIZE 8096
#define ZDS_DEFAULT_MAX_ENTRIES 1000
#define ZDS_DEFAULT_MAX_MEMBER_ENTRIES 5000

#define ZDS_VOLSER_VSAM "*VSAM*"
#define ZDS_VOLSER_AIX "*AIX *"
#define ZDS_VOLSER_PATH "*PATH*"
#define ZDS_VOLSER_ALIAS "*ALIAS"
#define ZDS_VOLSER_GDG "??????"
#define ZDS_VOLSER_UNKNOWN "------"

#define ZDS_DSORG_UNKNOWN "--" // Unknown
#define ZDS_DSORG_PDSE "PO-E"  // Library
#define ZDS_DSORG_VSAM "VS"    // VSAM
#define ZDS_DSORG_PS "PS"      // Sequential
#define ZDS_DSORG_PO "PO"      // Partitioned
#define ZDS_DSORG_POU "POU"    // Partitioned Unmovable
#define ZDS_DSORG_DA "DA"      // Direct Access

#define ZDS_MAX_DSNAME_LENGTH 44
#define ZDS_MAX_VOLSER_LENGTH 6

// DSNTYPE values
#define ZDS_DSNTYPE_PDS "PDS"         // Partitioned Data Set
#define ZDS_DSNTYPE_LIBRARY "LIBRARY" // PDSE (Partitioned Data Set Extended)

// Record format constants (not exhaustive)
#define ZDS_RECFM_FB "FB"   // Fixed Blocked
#define ZDS_RECFM_F "F"     // Fixed
#define ZDS_RECFM_VB "VB"   // Variable Blocked
#define ZDS_RECFM_V "V"     // Variable
#define ZDS_RECFM_U "U"     // Undefined
#define ZDS_RECFM_A "A"     // American National Standard (ASA) Control
#define ZDS_RECFM_S "S"     // Standard
#define ZDS_RECFM_FBS "FBS" // Fixed Blocked Standard
#define ZDS_RECFM_VBS "VBS" // Variable Blocked Spanned

// Device type codes (last byte of UCB device type)
#define ZDS_DEVTYPE_3380 0x0E
#define ZDS_DEVTYPE_3390 0x0F
#define ZDS_DEVTYPE_9345 0x04

ZNP_PACK_ON

// NOTE(Kelosky): struct is padded to nearest double word boundary; ensure proper alignment for fields
typedef struct
{
  char eye[3];              // future use
  unsigned char version[1]; // future use
  int32_t len;              // future use

  ZEncode encoding_opts;
  char etag[9]; // 8 characters + null terminator
  unsigned char _etag_ddname_pad[7];
  char ddname[8];

  int32_t max_entries;
  int32_t buffer_size;

  void *PTR64 csi;

  unsigned int dynalloc : 1; // indicates that the data set was dynamically allocated
  unsigned int has_more : 1;
  unsigned int delete_only : 1; // open for member delete only; skip record format validation
  unsigned char reserve[12];

  unsigned char ebcdic_date[8];

  uint64_t ts_binary;
  uint32_t date;
  int max_lines;
  int returned_lines;

  ZDIAG diag;

} ZDS;

ZNP_PACK_OFF

ZNP_PACK_ON

// https://www.ibm.com/docs/en/SSLTBW_2.2.0/pdf/dgt3s310.pdf, page 26
typedef struct DSCBFormat1
{
  char ds1fmtid;     // Format Identifier (0x2C)
  char ds1dssn[6];   // Data set serial number (0x2D)
  uint16_t ds1volsq; // Volume sequence number (0x33)
  char ds1credt[3];  // Creation date (0x35)
  char ds1expdt[3];  // Expiration date (0x38)
  uint8_t ds1noepv;  // Number of extents on volume (0x3B)
  uint8_t ds1nobdb;  // Number of bytes used in last directory block (0x3C)
  uint8_t ds1flag1;  // Flags byte (0x3D)
  char ds1syscd[13]; // System code (0x3E)
  char ds1refd[3];   // Date last referenced (0x4B)
  uint8_t ds1smsfg;  // System managed storage indicators (0x4E)
  char ds1scext[3];  // Secondary space extension (0x4F)
  uint16_t ds1dsorg; // Data set organization (0x52)
  uint8_t ds1recfm;  // Record format (0x54)
  char ds1optcd;     // Option Code (0x55)
  char ds1blkl[2];   // Block length (Type F unblocked records), or maximum block size (F blocked, U or V records) (0x56)
  char ds1lrecl[2];  // Logical record length (0x58)
  char ds1keyl;      // Key length (0 to 255) (0x5A)
  char ds1rkp[2];    // Relative key position (0x5B)
  char ds1dsind;     // Data set indicators (0x5D)
  char ds1scalo[4];  // Secondary allocation space parameters (0x5E)
  char ds1lstar[3];  // Last used track and block on track (TTR) (0x62)
  char ds1trbal[2];  // If not extended format, value from TRKCALC indicating space remaining on last track used (0x65)
  char _filler1;     // Reserved (0x66)
  char ds1ttthi;     // High order byte of track number on DS1LSTAR, valid if ds1large is on (0x68)
  char ds1exnts[30]; // Three extent fields (0x69)
  char ds1ptrds[5];  // Pointer (CCHHR) to a format-2 or format-3 DSCB, or zero - if this DSCB is a format-8 DSCB, its always the CCHHR of a format-9 DSCB (0x87)
  char ds1end;       // End of DSCB-1 (0x8C)
} DSCBFormat1;

typedef struct IndexableDSCBFormat1
{
  char ds1dsnam[ZDS_MAX_DSNAME_LENGTH]; // Data set name (used as key)
  DSCBFormat1 dscb1;                    // Contents of DSCB-1
} IndexableDSCBFormat1;

ZNP_PACK_OFF

#endif