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

#ifndef ZKRTYPE_H
#define ZKRTYPE_H

// R_datalib (IRRSDL64) parameter-list and result structures plus the SAF/RACF
// function, usage and status constants. Ported from zowe/keyring-utilities
// `keyring_types.h`; `_Packed` is expressed with `#pragma pack` so the layouts
// compile cleanly under ibm-clang++ (C++), and the CLI-only helper structs are
// dropped because zo owns argument parsing.

#define ZKR_MAX_USAGE_LEN 20
#define ZKR_MAX_USERID_LEN 8
#define ZKR_MAX_KEYRING_LEN 236
#define ZKR_MAX_LABEL_LEN 32
#define ZKR_MAX_CERTIFICATE_LEN (64 * 1024)
#define ZKR_MAX_PRIVATE_KEY_LEN (8 * 1024)
#define ZKR_MAX_SUBJECT_DN_LEN (2 * 1024)
#define ZKR_MAX_RECORD_ID_LEN 246

// R_datalib function codes
#define ZKR_GETCERT_CODE 0x01
#define ZKR_GETNEXT_CODE 0x02
#define ZKR_DATA_ABORT_CODE 0x03
#define ZKR_NEWRING_CODE 0x07
#define ZKR_IMPORT_CODE 0x08
#define ZKR_DELCERT_CODE 0x09
#define ZKR_DELRING_CODE 0x0A
#define ZKR_REFRESH_CODE 0x0B
#define ZKR_ALTER_CODE 0x0C    // DataAlter: change status and/or label
#define ZKR_RINGINFO_CODE 0x0D // GetRingInfo: enumerate a user's key rings

// R_datalib "attributes" trust flags for DataPut (X'08') and DataAlter (X'0C').
// (SA23-2293v3r1, "Attributes"). HIGHTRUST is honored only for CERTAUTH certs.
#define ZKR_ATTR_TRUST 0x80000000
#define ZKR_ATTR_HIGHTRUST 0x40000000
#define ZKR_ATTR_NOTRUST 0x20000000

// certificate_usage values (as returned/consumed by R_datalib)
#define ZKR_USAGE_PERSONAL 0x00000008
#define ZKR_USAGE_CERTAUTH 0x00000002

// certificate_status bit flags
#define ZKR_STATUS_TRUST 0x80000000
#define ZKR_STATUS_HIGHTRUST 0x40000000
#define ZKR_STATUS_NOTRUST 0x20000000
#define ZKR_STATUS_ANY 0x00000000

// R_datalib "attributes" flag for GETCERT/GETNEXT: return the DER-encoded
// certificate and open a result handle for enumeration.
#define ZKR_ATTR_GETCERT 0x80000000

// R_datalib DataRemove (X'09') attribute flags (SA23-2293v3r1). DEL_CERT_TOO
// deletes the certificate from the database after removing it from the ring (if
// not connected elsewhere); DEL_ALLRINGS forces deletion even when it is still
// connected to other rings.
#define ZKR_ATTR_DEL_CERT_TOO 0x80000000
#define ZKR_ATTR_DEL_ALLRINGS 0x40000000

// Common R_datalib parameter list. NOTE: the field order below is fixed by the
// IRRSDL64 interface -- do not reorder. This struct is intentionally NOT packed;
// it relies on natural alignment plus the double-word aligned workarea.
typedef struct _R_datalib_parm_list_64
{
  int num_parms;
  double workarea[128]; // double-word aligned, 1024-byte work area
  int saf_rc_ALET, return_code;
  int racf_rc_ALET, RACF_return_code;
  int racf_rsn_ALET, RACF_reason_code;
  char function_code;
  int attributes;
  char RACF_userid_len;                 // DO NOT change position of this field
  char RACF_userid[ZKR_MAX_USERID_LEN]; // DO NOT change position of this field
  char ring_name_len;                   // DO NOT change position of this field
  char ring_name[ZKR_MAX_KEYRING_LEN];  // DO NOT change position of this field
  int parm_list_version;
  void *parmlist;
} R_datalib_parm_list_64;

// Describes a single R_datalib invocation (function code + attributes + the
// function-specific parameter block referenced by the common parm list).
typedef struct _R_datalib_function
{
  char code;
  int default_attributes;
  int parm_list_version;
  void *parmlist;
} R_datalib_function;

#pragma pack(push, 1)

typedef struct _R_datalib_data_remove // DO NOT change property positions
{
  int label_len;
  int reserved_1;
  char *label_addr;
  char CERT_userid_len;
  char CERT_userid[ZKR_MAX_USERID_LEN];
  char reserved_2[3];
} R_datalib_data_remove;

typedef struct _R_datalib_result_handle // DO NOT change property positions
{
  int dbToken;
  int number_predicates;
  int attribute_id;
  int attribute_length;
  char *attribute_ptr;
} R_datalib_result_handle;

typedef struct _R_datalib_data_get // DO NOT change property positions
{
  R_datalib_result_handle *handle;
  int certificate_usage;
  int Default;
  int certificate_len;
  int reserved_1;
  char *certificate_ptr;
  int private_key_len;
  int reserved_2;
  char *private_key_ptr;
  int private_key_type;
  int private_key_bitsize;
  int label_len;
  int reserved_3;
  char *label_ptr;
  char cert_userid_len;
  char cert_userid[ZKR_MAX_USERID_LEN];
  char reserved_4[3];
  int subjects_DN_length;
  char *subjects_DN_ptr;
  int record_ID_length;
  int reserved_5;
  char *record_ID_ptr;
  int certificate_status;
} R_datalib_data_get;

typedef struct _R_datalib_data_abort
{
  R_datalib_result_handle *handle;
} R_datalib_data_abort;

typedef struct _R_datalib_data_put // DO NOT change property positions
{
  int certificate_usage;
  int Default;
  int certificate_len;
  int reserved_1;
  char *certificate_ptr;
  int private_key_len;
  int reserved_2;
  char *private_key_ptr;
  int label_len;
  int reserved_3;
  char *label_ptr;
  char cert_userid_len;
  char cert_userid[ZKR_MAX_USERID_LEN];
  char reserved_4[3];
} R_datalib_data_put;

// DataAlter (X'0C') parameter list. Changes status (via the Attributes word)
// and/or label. DO NOT change property positions (SA23-2293v3r1).
typedef struct _R_datalib_data_alter
{
  int label_len;
  int reserved_1;
  char *label_addr;
  int new_label_len; // 0 = do not change the label
  int reserved_2;
  char *new_label_addr;
  char CERT_userid_len;
  char CERT_userid[ZKR_MAX_USERID_LEN];
  char reserved_3[3];
} R_datalib_data_alter;

// GetRingInfo (X'0D') parameter list. Ring_result_ptr addresses a caller
// allocated output area holding a variable-length, length-prefixed sequence of
// ring/certificate records (see zkr_list_rings for the parse). DO NOT reorder.
typedef struct _R_datalib_get_ring_info
{
  int search_type;        // 0 = search by RACF_user_ID + Ring_name
  int ring_result_length; // size of the caller-allocated output area
  char *ring_result_ptr;  // address of the output area
} R_datalib_get_ring_info;

#pragma pack(pop)

// Scratch buffers backing an R_datalib_data_get during certificate enumeration.
typedef struct _Data_get_buffers
{
  int certificate_length;
  char certificate[ZKR_MAX_CERTIFICATE_LEN];
  int private_key_length;
  char private_key[ZKR_MAX_PRIVATE_KEY_LEN];
  int label_length;
  char label[ZKR_MAX_LABEL_LEN + 1];
  int subject_DN_length;
  char subject_DN[ZKR_MAX_SUBJECT_DN_LEN];
  char record_id[ZKR_MAX_RECORD_ID_LEN];
} Data_get_buffers;

#endif // ZKRTYPE_H
