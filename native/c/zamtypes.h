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

#ifndef ZAMTYPES_H
#define ZAMTYPES_H

#include "dcbd.h"
#include "ihadcbe.h"
#include "ifgacb.h"
#include "iazdsinf.h"
#include "ifgrpl.h"
#include "jfcb.h"
#include "ihaexlst.h"
#include "ztype.h"
#include "zecb.h"

typedef struct ihadcb IHADCB;
typedef struct dcbe DCBE;
typedef struct ifgacb IFGACB;
typedef struct ifgrpl IFGRPL;
typedef struct dsinf DSINF;

#define OPTION_BYTE 0X80

// NOTE(Kelosky): mapping for __asm(" OPEN,MODE=31,MF=L" : "DS"(plist));
typedef struct
{
  unsigned char option;
  unsigned char reserved[3];
  IHADCB *PTR32 dcb;
} OPEN_PL;

typedef OPEN_PL CLOSE_PL;

typedef struct
{
  unsigned char option;
  unsigned char reserved[3];
} RDJFCB_PL;

// the residual count is the halfword, 14 bytes from the start of the status area
typedef struct
{
  unsigned char filler[14];
  short int residualCount;

} STATUS_AREA;

// must be below 16MB (see Using Data Sets publication)
typedef struct
{
  ECB ecb;
  unsigned char typeField1;
  unsigned char typeField2;
  unsigned short length;
  IHADCB *PTR32 dcb;
  char *PTR32 area;
  STATUS_AREA *PTR32 statusArea;
} DECB;

typedef struct jfcb JFCB;

typedef DECB WRITE_PL;
typedef DECB READ_PL;

typedef struct exlst EXLIST;

// Ref: https://www.ibm.com/docs/en/zos/2.5.0?topic=routines-dcb-abend-exit
typedef struct
{
  unsigned short system_completion_code; // offset 0: system completion code in first 12 bits
  unsigned char return_code;             // offset 2: return code associated with completion code
  unsigned char option_mask;             // offset 3: option mask (input) / chosen action (output)
  // following fields are only output from exit
  IHADCB *PTR32 dcb;              // offset 4: address of DCB
  unsigned int diag_info;         // offset 8: for system diagnostic use
  void *PTR32 recovery_work_area; // offset 12: must be below 16 MB
} DCB_ABEND_PL;

#define DCB_ABEND_OPT_OK_TO_RECOVER 0x08
#define DCB_ABEND_OPT_OK_TO_IGNORE 0x04
#define DCB_ABEND_OPT_OK_TO_DELAY 0x02

#define DCB_ABEND_RC_TERMINATE 0
#define DCB_ABEND_RC_IGNORE 4
#define DCB_ABEND_RC_DELAY 8
#define DCB_ABEND_RC_IGNORE_QUIETLY 20

#define MAX_HEADER_LEN 100
typedef struct
{
  unsigned char len;
  char title[MAX_HEADER_LEN];
} SNAP_HEADER;

typedef struct
{
  unsigned char id;
  unsigned char flags;
  unsigned char flag2;
  unsigned char reserved;
  unsigned char sdataFlagsOne;
  unsigned char sdataFlagsTwo;
  unsigned char pdataFlags;
  unsigned char reserved2;
  IHADCB *PTR32 dcb;
  void *PTR32 tcb;
  void *PTR32 list;
  SNAP_HEADER *PTR32 header;
} SNAP_PLIST;

typedef struct
{
  DCBE dcbe;
  int ctrl_len;
  int buffer_len;
  // int bufferCtrl;
  unsigned int eod : 1;
  char *PTR32 buffer;
} FILE_CTRL;

#define ISPF_STATS_MIN_LEN 15 // halfwords
#define ISPF_STATS_MAX_LEN 20 // halfwords
// https://www.ibm.com/docs/en/zos/3.2.0?topic=di-ispf-statistics-entry-in-pds-directory
typedef struct
{
  unsigned char version;               // byte1: 0x01 thru 0x99
  unsigned char level;                 // byte2: 0x00 thru 0x99
  unsigned char flags;                 // byte3: bit1=sclm indicator, bit2=reserved, bit3=stats exist, bit4-7=reserved, bit8=reserved
  unsigned char modified_time_seconds; // byte4: packed decimal
  unsigned char created_date_century;  // byte5: 0x00 = 1900 0x01=2000
  unsigned char created_date_year;     // byte6: packed decimal
  unsigned char created_date_day[2];   // byte7-8: packed decimal
  unsigned char modified_date_century; // byte9: 0x00 = 1900 0x01=2000
  unsigned char modified_date_year;    // byte10: packed decimal
  unsigned char modified_date_day[2];  // byte11-12: packed decimal
  unsigned char modified_time_hours;   // byte13: packed decimal
  unsigned char modified_time_minutes; // byte14: packed decimal
  short int current_number_of_lines;   // byte15-16: hexadecimal
  short int initial_number_of_lines;   // byte17-18: hexadecimal
  short int modified_number_of_lines;  // byte19-20: hexadecimal
  char userid[8];                      // byte21-28: padded with blanks
  char extended[2];                    // byte29-30: first part of extended stats
  // TODO(Kelosky): conditional data based on byte3 flags

} ISPF_STATS;

#define MAX_USER_DATA_LEN 62
#define LEN_MASK 0x1F
typedef struct
{
  char name[8];         // padded with blanks
  unsigned char ttr[3]; // TT=track, R=record
  unsigned char k;      // concatenation
  unsigned char z;      // where found, 0=private, 1=link, 2=job, task, step, 3-16=job, task, step of parent
  unsigned char c;      // name type bit0=0member, bit0=1alias, bit1-2=number of TTRN in user data (max 3), bit3-7 number of halfwords in the user data
  unsigned char user_data[MAX_USER_DATA_LEN];
} BLDL_LIST;

typedef struct
{
  unsigned char prefix[8]; // you must provide a prefix of 8 bytes immediately precedes the list of member names; listadd most point to FF field
  unsigned short int ff;   // number of entries in the list
  unsigned short int ll;   // length of each entry
  BLDL_LIST list;
} BLDL_PL;

typedef struct
{
  char name[8];         // padded with blanks
  unsigned char ttr[3]; // TT=track, R=record
  unsigned char c;
  unsigned char user_data[MAX_USER_DATA_LEN];
} STOW_LIST;

typedef struct
{
  unsigned char ttr[3]; // TT=track, R=record
  unsigned char z;
} NOTE_RESPONSE;

#define NUM_EXLIST_ENTRIES 2 // dcbabend and jfcb
#define EYE "IO_CTRL "
typedef struct
{
  char eye[8];
  unsigned long long int work;
  unsigned int output : 1; // TODO(Kelosky): remove this flag
  unsigned int input : 1;  // TODO(Kelosky): remove this flag
  unsigned int has_enq : 1;
  unsigned int has_reserve : 1; // not reserved space... indicates RESERVE is outstanding
  unsigned int eof : 1;
  unsigned int dcb_abend : 1;
  unsigned int skip_finalize : 1; // do not update ISPF stats or STOW on close (member was deleted)
  unsigned int ucb;
  int buffer_size;
  char *PTR32 buffer;
  void *PTR32 zam24;
  int zam24_len;
  int lines_written;
  char *PTR32 free_location;
  int bytes_in_buffer;
  char ddname[8];
  IHADCB dcb;
  IFGACB ifgacb;
  DECB decb;
  JFCB jfcb;
  EXLIST exlst[NUM_EXLIST_ENTRIES];
  RDJFCB_PL rdjfcb_pl;
  OPEN_PL opl;
  STOW_LIST stow_list;
  IFGRPL rpl;
  DSINF dsinf;
} IO_CTRL;

#endif
