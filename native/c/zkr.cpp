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

#define _XOPEN_SOURCE_EXTENDED 1

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <sys/stat.h>
#include <gskcms.h>

#include "zkr.hpp"
#include "zkrtype.h"
#include "ztype.h"

// R_datalib SAF callable service. On z/OS this uses OS linkage; the guard keeps
// the file parseable off-platform. Signature matches the 14-word parameter list
// documented for IRRSDL00/IRRSDL64 (see zowe/keyring-utilities).
#if defined(__cplusplus) && defined(__MVS__)
extern "OS"
{
#elif defined(__cplusplus)
extern "C"
{
#endif
  void IRRSDL64(int *num_parms, void *work_area,
                int *saf_rc_alet, int *saf_rc,
                int *racf_rc_alet, int *racf_rc,
                int *racf_rsn_alet, int *racf_rsn,
                char *function_code, int *attributes,
                char *racf_userid_len, char *ring_name_len,
                int *parm_list_version, void *parmlist);
#if defined(__cplusplus)
}
#endif

namespace
{

void invoke_r_datalib(R_datalib_parm_list_64 *p)
{
  IRRSDL64(&p->num_parms, &p->workarea,
           &p->saf_rc_ALET, &p->return_code,
           &p->racf_rc_ALET, &p->RACF_return_code,
           &p->racf_rsn_ALET, &p->RACF_reason_code,
           &p->function_code, &p->attributes,
           &p->RACF_userid_len, &p->ring_name_len,
           &p->parm_list_version, p->parmlist);
}

void set_up_r_datalib_parameters(R_datalib_parm_list_64 *p, const R_datalib_function *fn,
                                 const std::string &owner, const std::string &ring)
{
  memset(p, 0, sizeof(*p));
  p->num_parms = 14;
  p->function_code = fn->code;
  p->attributes = fn->default_attributes;

  size_t ulen = owner.size();
  if (ulen > ZKR_MAX_USERID_LEN)
    ulen = ZKR_MAX_USERID_LEN;
  p->RACF_userid_len = static_cast<char>(ulen);
  memcpy(p->RACF_userid, owner.data(), ulen);

  size_t rlen = ring.size();
  if (rlen > ZKR_MAX_KEYRING_LEN)
    rlen = ZKR_MAX_KEYRING_LEN;
  p->ring_name_len = static_cast<char>(rlen);
  memcpy(p->ring_name, ring.data(), rlen);

  p->parm_list_version = fn->parm_list_version;
  p->parmlist = fn->parmlist;
}

// R_datalib calls follow the SAF convention where a return code of 0 is
// success and 4 is "success with a warning/informational condition" (e.g. the
// certificate already exists in the ESM database). Only 8 and above are hard
// failures. keyring-util never fails on nonzero rc at all; we treat >= 8 as an
// error and let warnings (rc 4) succeed.
bool saf_failed(const R_datalib_parm_list_64 *p)
{
  return p->return_code >= 8;
}

int record_saf_error(ZKR *zkr, const R_datalib_parm_list_64 *p, const std::string &service)
{
  zkr->diag.function_code = static_cast<unsigned char>(p->function_code);
  zkr->diag.saf_rc = p->return_code;
  zkr->diag.esm_rc = p->RACF_return_code;
  zkr->diag.esm_rsn = p->RACF_reason_code;
  zkr->diag.gsk_rc = 0;
  zkr->diag.service = service;

  char buf[160];
  snprintf(buf, sizeof(buf), "%s failed: SAF rc: %d, ESM rc: %d, ESM rsn: %d",
           service.c_str(), p->return_code, p->RACF_return_code, p->RACF_reason_code);
  zkr->diag.e_msg = buf;
  return RTNCD_FAILURE;
}

// Record a non-fatal SAF warning (return_code == 4). Leaves a human-readable
// note in zkr->diag.warning while the operation still counts as success.
void note_saf_warning(ZKR *zkr, const R_datalib_parm_list_64 *p,
                      const std::string &service, const std::string &friendly)
{
  zkr->diag.function_code = static_cast<unsigned char>(p->function_code);
  zkr->diag.saf_rc = p->return_code;
  zkr->diag.esm_rc = p->RACF_return_code;
  zkr->diag.esm_rsn = p->RACF_reason_code;

  char buf[256];
  snprintf(buf, sizeof(buf), "%s (%s: SAF rc %d, ESM rc %d, reason %d)",
           friendly.c_str(), service.c_str(), p->return_code, p->RACF_return_code, p->RACF_reason_code);
  zkr->diag.warning = buf;
}

int record_gsk_error(ZKR *zkr, int rc, const std::string &service)
{
  zkr->diag.gsk_rc = rc;
  zkr->diag.service = service;

  char buf[160];
  snprintf(buf, sizeof(buf), "%s failed: GSK rc = %d (0x%X)", service.c_str(), rc, rc);
  zkr->diag.e_msg = buf;
  return RTNCD_FAILURE;
}

int record_message(ZKR *zkr, const std::string &service, const std::string &message)
{
  zkr->diag.service = service;
  zkr->diag.e_msg = message;
  return RTNCD_FAILURE;
}

// Human-readable text for an R_datalib SAF-return-code-4 warning, per the exact
// reason codes documented in z/OS Security Server RACF Callable Services
// (SA23-2293v3r1), Tables 79 (DataPut) and 80 (DataRemove).
std::string datalib_rc4_message(char function_code, int reason)
{
  if (function_code == ZKR_IMPORT_CODE) // DataPut (Table 79)
  {
    switch (reason)
    {
    case 0:
      return "the certificate was imported, but its status is NOTRUST";
    case 4:
      return "the certificate was imported, but the DIGTCERT class must be refreshed for the update to "
             "take effect (run 'system cert refresh')";
    case 8:
      return "the certificate already exists in the ESM database; the supplied label was ignored and the "
             "existing certificate was connected to the ring";
    case 12:
      return "the certificate already exists in the ESM database (the supplied label was ignored) and its "
             "status is NOTRUST";
    case 16:
      return "the certificate already exists in the ESM database (the supplied label was ignored); the "
             "DIGTCERT class must be refreshed for the update to take effect (run 'system cert refresh')";
    default:
      return "the certificate was imported with a warning";
    }
  }
  if (function_code == ZKR_DELCERT_CODE) // DataRemove (Table 80)
  {
    switch (reason)
    {
    case 0:
      return "the certificate was removed from the ring but was not deleted from the ESM database because "
             "it is connected to other rings";
    case 4:
      return "the certificate was removed from the ring but was not deleted from the ESM database because "
             "of an unexpected error";
    case 8:
      return "the certificate was removed from the ring but was not deleted from the ESM database because "
             "of insufficient authority";
    case 16:
      return "the certificate was removed from the ring but was not deleted from the ESM database because "
             "it has been used to generate a request";
    default:
      return "the operation completed with a warning";
    }
  }
  return "the operation completed with a warning";
}

// A ring-scoped R_datalib call with no function-specific parameter block
// (NEWRING / DELRING / REFRESH).
int simple_action(ZKR *zkr, char code, const std::string &owner,
                  const std::string &ring, const std::string &service)
{
  R_datalib_parm_list_64 p;
  R_datalib_function fn = {code, 0x00000000, 0, NULL};
  set_up_r_datalib_parameters(&p, &fn, owner, ring);
  invoke_r_datalib(&p);
  if (saf_failed(&p))
    return record_saf_error(zkr, &p, service);
  if (p.return_code == 4)
    note_saf_warning(zkr, &p, service, "the operation completed with a warning");
  return RTNCD_SUCCESS;
}

int len_without_trailing_blanks(const char *s, int maxlen)
{
  const char *end = s + maxlen - 1;
  while (end >= s && *end == 0x40) // EBCDIC space
    end--;
  return static_cast<int>(end - s + 1);
}

void reset_get_parm(R_datalib_data_get *g)
{
  g->certificate_len = ZKR_MAX_CERTIFICATE_LEN;
  g->private_key_len = ZKR_MAX_PRIVATE_KEY_LEN;
  g->label_len = ZKR_MAX_LABEL_LEN;
  g->subjects_DN_length = ZKR_MAX_SUBJECT_DN_LEN;
  g->record_ID_length = ZKR_MAX_RECORD_ID_LEN;
  g->cert_userid_len = 0x08;
}

void add_cert_item(std::vector<ZKRCertInfo> &certs, const R_datalib_data_get *g)
{
  ZKRCertInfo info;

  if (g->label_ptr != NULL && g->label_len > 0)
    info.label.assign(g->label_ptr, g->label_len);

  int ulen = len_without_trailing_blanks(g->cert_userid, sizeof(g->cert_userid));
  if (ulen < 0)
    ulen = 0;
  info.owner.assign(g->cert_userid, ulen);

  switch (g->certificate_usage)
  {
  case ZKR_USAGE_PERSONAL:
    info.usage = "PERSONAL";
    break;
  case ZKR_USAGE_CERTAUTH:
    info.usage = "CERTAUTH";
    break;
  default:
    info.usage = "OTHER";
    break;
  }

  unsigned int status = static_cast<unsigned int>(g->certificate_status);
  if (status == ZKR_STATUS_TRUST)
    info.status = "TRUST";
  else if (status == ZKR_STATUS_HIGHTRUST)
    info.status = "HIGHTRUST";
  else if (status == ZKR_STATUS_NOTRUST)
    info.status = "NOTRUST";
  else
    info.status = "UNKNOWN";

  info.is_default = g->Default != 0;
  certs.push_back(info);
}

int load_pkcs12_file(gsk_buffer *buff_in, const std::string &filename, ZKR *zkr)
{
  struct stat info;
  if (stat(filename.c_str(), &info) != 0)
    return record_message(zkr, "IMPORT", "Could not stat PKCS#12 file: " + filename);
  if (!S_ISREG(info.st_mode))
    return record_message(zkr, "IMPORT", "PKCS#12 path is not a regular file: " + filename);
  if (info.st_size == 0)
    return record_message(zkr, "IMPORT", "PKCS#12 file is empty: " + filename);

  FILE *stream = fopen(filename.c_str(), "rb");
  if (stream == NULL)
    return record_message(zkr, "IMPORT", "Could not open PKCS#12 file: " + filename);

  char *buffer = static_cast<char *>(malloc(info.st_size));
  if (buffer == NULL)
  {
    fclose(stream);
    return record_message(zkr, "IMPORT", "Out of memory reading PKCS#12 file");
  }

  size_t numread = fread(buffer, 1, info.st_size, stream);
  fclose(stream);
  if (numread != static_cast<size_t>(info.st_size))
  {
    free(buffer);
    return record_message(zkr, "IMPORT", "Error reading PKCS#12 file: " + filename);
  }

  buff_in->data = buffer;
  buff_in->length = numread;
  return RTNCD_SUCCESS;
}

int usage_to_code(const std::string &usage)
{
  if (usage == "PERSONAL" || usage == "personal")
    return ZKR_USAGE_PERSONAL;
  if (usage == "CERTAUTH" || usage == "certauth")
    return ZKR_USAGE_CERTAUTH;
  return 0;
}

std::string to_hex(const char *data, size_t len)
{
  static const char digits[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i)
  {
    unsigned char b = static_cast<unsigned char>(data[i]);
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0x0F]);
  }
  return out;
}

// Format a struct tm (System SSL gsk_timeval is a struct tm) as ISO-8601 UTC.
std::string fmt_tm(const struct tm &t)
{
  struct tm tmp = t;
  char buf[32];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmp) == 0)
    return "";
  return buf;
}

// Read a 1-byte-length-prefixed string from a GetRingInfo result buffer,
// advancing the cursor. Bounds-checked against end.
const char *read_lp_string(const char *cur, const char *end, std::string &out)
{
  out.clear();
  if (cur >= end)
    return end;
  unsigned char len = static_cast<unsigned char>(*cur);
  cur += 1;
  if (cur + len > end)
    len = static_cast<unsigned char>(end - cur);
  out.assign(cur, len);
  return cur + len;
}

} // namespace

int zkr_new_ring(ZKR *zkr, const std::string &owner, const std::string &ring)
{
  return simple_action(zkr, ZKR_NEWRING_CODE, owner, ring, "IRRSDL64 NEWRING");
}

int zkr_del_ring(ZKR *zkr, const std::string &owner, const std::string &ring)
{
  return simple_action(zkr, ZKR_DELRING_CODE, owner, ring, "IRRSDL64 DELRING");
}

int zkr_refresh(ZKR *zkr)
{
  return simple_action(zkr, ZKR_REFRESH_CODE, "", "", "IRRSDL64 REFRESH");
}

int zkr_del_cert(ZKR *zkr, const std::string &owner, const std::string &ring,
                 const std::string &label, bool skip_refresh)
{
  if (label.empty())
    return record_message(zkr, "DELCERT", "Certificate label is required");

  R_datalib_data_remove rem;
  memset(&rem, 0, sizeof(rem));
  rem.label_len = static_cast<int>(label.size());
  rem.label_addr = const_cast<char *>(label.c_str());

  size_t ulen = owner.size();
  if (ulen > ZKR_MAX_USERID_LEN)
    ulen = ZKR_MAX_USERID_LEN;
  rem.CERT_userid_len = static_cast<char>(ulen);
  memset(rem.CERT_userid, ' ', ZKR_MAX_USERID_LEN); // EBCDIC blanks
  memcpy(rem.CERT_userid, owner.data(), ulen);

  // A database delete (ring "*") should remove the certificate entirely, like
  // RACDCERT DELETE (or the ESM equivalent). Set DEL_CERT_TOO + DEL_ALLRINGS so
  // it is removed from the ESM database even when still connected to other
  // rings; otherwise the ESM returns 8/8/44 ("connected to other rings"). A
  // real-ring delete uses no attributes (disconnect from that ring only).
  int attributes = 0x00000000;
  if (ring == "*")
    attributes = static_cast<int>(ZKR_ATTR_DEL_CERT_TOO | ZKR_ATTR_DEL_ALLRINGS);

  R_datalib_parm_list_64 p;
  R_datalib_function fn = {ZKR_DELCERT_CODE, attributes, 0, &rem};
  set_up_r_datalib_parameters(&p, &fn, owner, ring);
  invoke_r_datalib(&p);

  if (saf_failed(&p))
    return record_saf_error(zkr, &p, "IRRSDL64 DELCERT");

  // The ESM signals (4/4/12) that the DIGTCERT class must be refreshed for the
  // disconnect to take effect (SA23-2293 Table 80). Match keyring-util and
  // refresh automatically, unless the caller asked to skip it. The delete itself
  // already succeeded at this point, so a failed refresh is downgraded to a
  // warning rather than failing the whole call.
  if (p.return_code == 4 && p.RACF_return_code == 4 && p.RACF_reason_code == 12)
  {
    if (skip_refresh)
    {
      note_saf_warning(zkr, &p, "IRRSDL64 DELCERT",
                       "certificate removed, but the DIGTCERT class must be refreshed for the "
                       "change to take effect; refresh skipped per --skip-refresh (run "
                       "'system cert refresh' to apply)");
      return RTNCD_SUCCESS;
    }

    // Auto-refresh, matching the Import behavior. Use a separate handle so the
    // delete's SAF codes are not clobbered by the refresh call.
    ZKR refresh_zkr;
    if (zkr_refresh(&refresh_zkr) != RTNCD_SUCCESS)
    {
      note_saf_warning(zkr, &p, "IRRSDL64 DELCERT",
                       "certificate removed, but the automatic DIGTCERT refresh failed (" +
                           refresh_zkr.diag.e_msg + "); run 'system cert refresh' manually");
    }
    return RTNCD_SUCCESS;
  }

  // Other rc-4 reason codes report that the certificate was disconnected from the
  // ring but not deleted from the ESM database (Table 80).
  if (p.return_code == 4)
    note_saf_warning(zkr, &p, "IRRSDL64 DELCERT",
                     datalib_rc4_message(ZKR_DELCERT_CODE, p.RACF_reason_code));
  return RTNCD_SUCCESS;
}

int zkr_list_ring(ZKR *zkr, const std::string &owner, const std::string &ring,
                  std::vector<ZKRCertInfo> &certs, size_t max_entries, bool *more_available)
{
  if (more_available != nullptr)
    *more_available = false;

  std::unique_ptr<Data_get_buffers> buffers(new Data_get_buffers());
  memset(buffers.get(), 0, sizeof(Data_get_buffers));

  R_datalib_parm_list_64 p;
  R_datalib_data_get get_parm;
  R_datalib_result_handle handle;
  R_datalib_data_abort data_abort;

  memset(&get_parm, 0, sizeof(get_parm));
  memset(&handle, 0, sizeof(handle));

  get_parm.handle = &handle;
  get_parm.certificate_ptr = buffers->certificate;
  get_parm.private_key_ptr = buffers->private_key;
  get_parm.label_ptr = buffers->label;
  get_parm.subjects_DN_ptr = buffers->subject_DN;
  get_parm.record_ID_ptr = buffers->record_id;
  get_parm.certificate_status = ZKR_STATUS_ANY;

  R_datalib_function get_first = {ZKR_GETCERT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &get_parm};
  R_datalib_function get_next = {ZKR_GETNEXT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &get_parm};
  R_datalib_function abort_fn = {ZKR_DATA_ABORT_CODE, 0x00000000, 0, &data_abort};

  reset_get_parm(&get_parm);
  set_up_r_datalib_parameters(&p, &get_first, owner, ring);
  invoke_r_datalib(&p);
  if (p.return_code != 0)
  {
    // 8/8/44 on the first fetch means the ring exists but holds no
    // certificates -- surface that as an empty list rather than an error.
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      return RTNCD_SUCCESS;
    return record_saf_error(zkr, &p, "IRRSDL64 GETCERT");
  }

  add_cert_item(certs, &get_parm);

  int result = RTNCD_SUCCESS;
  while (true)
  {
    reset_get_parm(&get_parm);
    set_up_r_datalib_parameters(&p, &get_next, owner, ring);
    invoke_r_datalib(&p);

    // 8/8/44 == no more certificates in the ring (end of enumeration)
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      break;
    if (p.return_code != 0)
    {
      result = record_saf_error(zkr, &p, "IRRSDL64 GETNEXT");
      break;
    }

    // We fetched a valid certificate. If we are already at the cap, this one is
    // proof that more exist beyond the limit -- flag it and stop without adding.
    if (max_entries != 0 && certs.size() >= max_entries)
    {
      if (more_available != nullptr)
        *more_available = true;
      break;
    }
    add_cert_item(certs, &get_parm);
  }

  // Always release the enumeration handle (best effort).
  data_abort.handle = &handle;
  set_up_r_datalib_parameters(&p, &abort_fn, owner, ring);
  invoke_r_datalib(&p);

  return result;
}

void zkr_filter_certs_into(const std::vector<ZKRCertInfo> &certs, const std::string &label,
                           const std::string &usage, size_t max_entries, bool *more_available,
                           std::vector<ZKRCertInfo> &out)
{
  if (more_available != nullptr)
    *more_available = false;

  for (std::vector<ZKRCertInfo>::const_iterator it = certs.begin(); it != certs.end(); ++it)
  {
    // RACDCERT LABEL semantics: exact and case-sensitive, no wildcards.
    if (!label.empty() && it->label != label)
      continue;
    if (!usage.empty() && it->usage != usage)
      continue;
    // This entry matches. If the cap is already full, it proves more matches
    // exist beyond the cap -- flag that and stop without adding.
    if (max_entries != 0 && out.size() >= max_entries)
    {
      if (more_available != nullptr)
        *more_available = true;
      break;
    }
    out.push_back(*it);
  }
}

std::vector<ZKRCertInfo> zkr_filter_certs(const std::vector<ZKRCertInfo> &certs,
                                          const std::string &label,
                                          const std::string &usage,
                                          size_t max_entries, bool *more_available)
{
  std::vector<ZKRCertInfo> out;
  zkr_filter_certs_into(certs, label, usage, max_entries, more_available, out);
  return out;
}

int zkr_export_cert(ZKR *zkr, const ZKRExportOptions &opts, std::string &data)
{
  if (opts.label.empty())
    return record_message(zkr, "EXPORT", "Certificate label is required");

  const bool want_p12 = (opts.format == "p12" || opts.format == "P12");
  if (want_p12 && opts.password.empty())
    return record_message(zkr, "EXPORT", "A PKCS#12 passphrase is required (gsk_export_key rejects an empty password)");

  gsk_handle handle = NULL;
  int num_records = 0;
  std::string db_name = opts.owner + "/" + opts.ring;

  gsk_status rc = gsk_open_keyring(const_cast<char *>(db_name.c_str()), &handle, &num_records);
  if (rc != 0)
    return record_gsk_error(zkr, rc, "gsk_open_keyring(" + db_name + ")");

  int result = RTNCD_SUCCESS;
  data.clear();

  if (!want_p12)
  {
    gsk_buffer der = {0, 0};
    rc = gsk_export_certificate(handle, const_cast<char *>(opts.label.c_str()),
                                gskdb_export_der_binary, &der);
    if (rc != 0)
    {
      result = record_gsk_error(zkr, rc, "gsk_export_certificate(" + opts.label + ")");
    }
    else
    {
      gsk_buffer b64 = {0, 0};
      rc = gsk_encode_base64(&der, &b64);
      if (rc != 0)
      {
        result = record_gsk_error(zkr, rc, "gsk_encode_base64");
      }
      else
      {
        data += "-----BEGIN CERTIFICATE-----\n";
        data.append(static_cast<char *>(b64.data), b64.length);
        data += "-----END CERTIFICATE-----\n";
        gsk_free_buffer(&b64);
      }
      gsk_free_buffer(&der);
    }
  }
  else
  {
    gsk_buffer key_stream = {0, 0};
    rc = gsk_export_key(handle, const_cast<char *>(opts.label.c_str()),
                        gskdb_export_pkcs12v3_binary, x509_alg_pbeWithSha1And128BitRc4,
                        const_cast<char *>(opts.password.c_str()), &key_stream);
    if (rc != 0)
    {
      result = record_gsk_error(zkr, rc, "gsk_export_key(" + opts.label + ")");
    }
    else
    {
      data.assign(static_cast<char *>(key_stream.data), key_stream.length);
      gsk_free_buffer(&key_stream);
    }
  }

  gsk_close_database(&handle);
  return result;
}

int zkr_import_cert(ZKR *zkr, const ZKRImportOptions &opts)
{
  if (opts.label.empty())
    return record_message(zkr, "IMPORT", "Certificate label is required");
  if (opts.label.size() >= ZKR_MAX_LABEL_LEN)
    return record_message(zkr, "IMPORT", "Certificate label is too long");

  int usage_code = usage_to_code(opts.usage);
  if (usage_code == 0)
    return record_message(zkr, "IMPORT", "Invalid usage '" + opts.usage + "'. Use CERTAUTH or PERSONAL.");

  gsk_buffer buff_in = {0, 0};
  if (!opts.p12_data.empty())
  {
    // gsk_free_buffer() below frees buff_in.data with free(), so it must own a
    // malloc'd copy -- it must not point at the std::string's own storage.
    char *buffer = static_cast<char *>(malloc(opts.p12_data.size()));
    if (buffer == NULL)
      return record_message(zkr, "IMPORT", "Out of memory reading PKCS#12 data");
    memcpy(buffer, opts.p12_data.data(), opts.p12_data.size());
    buff_in.data = buffer;
    buff_in.length = opts.p12_data.size();
  }
  else if (load_pkcs12_file(&buff_in, opts.p12_path, zkr) != RTNCD_SUCCESS)
    return RTNCD_FAILURE;

  pkcs_cert_key cert_key;
  pkcs_certificates cas;
  memset(&cert_key, 0, sizeof(cert_key));
  memset(&cas, 0, sizeof(cas));

  gsk_status rc = gsk_decode_import_key(&buff_in, const_cast<char *>(opts.password.c_str()), &cert_key, &cas);
  if (rc != 0)
  {
    gsk_free_buffer(&buff_in);
    return record_gsk_error(zkr, rc, "gsk_decode_import_key");
  }

  // Private key is optional; a certificate without one still imports.
  gsk_buffer priv_key_buff = {0, 0};
  gsk_encode_private_key(&cert_key.privateKey, &priv_key_buff);

  gsk_buffer cert_buff = {0, 0};
  rc = gsk_encode_export_certificate(&cert_key.certificate, &cas, gskdb_export_der_binary, &cert_buff);
  if (rc != 0)
  {
    gsk_free_buffer(&buff_in);
    gsk_free_buffer(&priv_key_buff);
    gsk_free_certificates(&cas);
    gsk_free_private_key_info(&cert_key.privateKey);
    return record_gsk_error(zkr, rc, "gsk_encode_export_certificate");
  }

  R_datalib_data_put put;
  memset(&put, 0, sizeof(put));
  put.certificate_usage = usage_code;
  put.Default = 0x00000000;
  put.certificate_len = cert_buff.length;
  put.certificate_ptr = static_cast<char *>(cert_buff.data);
  put.private_key_len = priv_key_buff.length;
  put.private_key_ptr = static_cast<char *>(priv_key_buff.data);
  put.label_len = static_cast<int>(opts.label.size());
  put.label_ptr = const_cast<char *>(opts.label.c_str());

  size_t ulen = opts.owner.size();
  if (ulen > ZKR_MAX_USERID_LEN)
    ulen = ZKR_MAX_USERID_LEN;
  put.cert_userid_len = static_cast<char>(ulen);
  memset(put.cert_userid, ' ', ZKR_MAX_USERID_LEN); // EBCDIC blanks
  memcpy(put.cert_userid, opts.owner.data(), ulen);

  R_datalib_parm_list_64 p;
  R_datalib_function fn = {ZKR_IMPORT_CODE, 0x00000000, 0, &put};
  set_up_r_datalib_parameters(&p, &fn, opts.owner, opts.ring);
  invoke_r_datalib(&p);

  int result = RTNCD_SUCCESS;
  if (saf_failed(&p))
  {
    result = record_saf_error(zkr, &p, "IRRSDL64 IMPORT");
  }
  else if (p.return_code == 4)
  {
    // SAF rc 4 is a non-fatal warning; the exact meaning depends on the
    // reason code (SA23-2293 Table 79). Reasons 4 and 16 indicate the DIGTCERT
    // class must be refreshed for the update to take effect; reasons 8/12/16
    // indicate the certificate already existed (its label was ignored).
    const bool needs_refresh = (p.RACF_reason_code == 4 || p.RACF_reason_code == 16);
    const bool already_exists = (p.RACF_reason_code == 8 || p.RACF_reason_code == 12 || p.RACF_reason_code == 16);

    if (needs_refresh && !opts.skip_refresh)
    {
      // Auto-refresh, matching the DataRemove behavior. Use a separate handle so
      // the import's SAF codes are not clobbered by the refresh call.
      ZKR refresh_zkr;
      if (zkr_refresh(&refresh_zkr) != RTNCD_SUCCESS)
      {
        // The import succeeded but the automatic refresh failed; surface that.
        note_saf_warning(zkr, &p, "IRRSDL64 IMPORT",
                         "certificate imported, but the automatic DIGTCERT refresh failed (" +
                             refresh_zkr.diag.e_msg + "); run 'system cert refresh' manually");
      }
      else if (already_exists)
      {
        note_saf_warning(zkr, &p, "IRRSDL64 IMPORT",
                         "the certificate already exists in the ESM database (the supplied label was "
                         "ignored); the DIGTCERT class was refreshed");
      }
      // else (reason 4, a genuinely new certificate): clean success after refresh.
    }
    else
    {
      note_saf_warning(zkr, &p, "IRRSDL64 IMPORT", datalib_rc4_message(ZKR_IMPORT_CODE, p.RACF_reason_code));
    }
  }

  gsk_free_buffer(&buff_in);
  gsk_free_buffer(&priv_key_buff);
  gsk_free_buffer(&cert_buff);
  gsk_free_certificates(&cas);
  gsk_free_private_key_info(&cert_key.privateKey);
  return result;
}

int zkr_show_cert(ZKR *zkr, const std::string &owner, const std::string &ring,
                  const std::string &label, ZKRCertDetail &detail)
{
  if (label.empty())
    return record_message(zkr, "SHOW", "Certificate label is required");

  std::unique_ptr<Data_get_buffers> buffers(new Data_get_buffers());
  memset(buffers.get(), 0, sizeof(Data_get_buffers));

  R_datalib_parm_list_64 p;
  R_datalib_data_get get_parm;
  R_datalib_result_handle handle;
  R_datalib_data_abort data_abort;
  memset(&get_parm, 0, sizeof(get_parm));
  memset(&handle, 0, sizeof(handle));

  get_parm.handle = &handle;
  get_parm.certificate_ptr = buffers->certificate;
  get_parm.private_key_ptr = buffers->private_key;
  get_parm.label_ptr = buffers->label;
  get_parm.subjects_DN_ptr = buffers->subject_DN;
  get_parm.record_ID_ptr = buffers->record_id;
  get_parm.certificate_status = ZKR_STATUS_ANY;

  R_datalib_function get_first = {ZKR_GETCERT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &get_parm};
  R_datalib_function get_next = {ZKR_GETNEXT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &get_parm};
  R_datalib_function abort_fn = {ZKR_DATA_ABORT_CODE, 0x00000000, 0, &data_abort};

  reset_get_parm(&get_parm);
  set_up_r_datalib_parameters(&p, &get_first, owner, ring);
  invoke_r_datalib(&p);
  if (p.return_code != 0)
  {
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      return record_message(zkr, "SHOW", "Certificate '" + label + "' not found in " + owner + "/" + ring);
    return record_saf_error(zkr, &p, "IRRSDL64 GETCERT");
  }

  bool found = false;
  int result = RTNCD_SUCCESS;
  while (true)
  {
    std::string current;
    if (get_parm.label_ptr != NULL && get_parm.label_len > 0)
      current.assign(get_parm.label_ptr, get_parm.label_len);

    if (current == label)
    {
      detail.label = current;
      int ulen = len_without_trailing_blanks(get_parm.cert_userid, sizeof(get_parm.cert_userid));
      if (ulen < 0)
        ulen = 0;
      detail.owner.assign(get_parm.cert_userid, ulen);

      switch (get_parm.certificate_usage)
      {
      case ZKR_USAGE_PERSONAL:
        detail.usage = "PERSONAL";
        break;
      case ZKR_USAGE_CERTAUTH:
        detail.usage = "CERTAUTH";
        break;
      default:
        detail.usage = "OTHER";
        break;
      }

      unsigned int status = static_cast<unsigned int>(get_parm.certificate_status);
      if (status == ZKR_STATUS_TRUST)
        detail.status = "TRUST";
      else if (status == ZKR_STATUS_HIGHTRUST)
        detail.status = "HIGHTRUST";
      else if (status == ZKR_STATUS_NOTRUST)
        detail.status = "NOTRUST";
      else
        detail.status = "UNKNOWN";

      detail.is_default = get_parm.Default != 0;
      // NOTE: R_datalib returns Subjects_DN in a BER-encoded (binary) form, not a
      // printable string, so it is intentionally not surfaced here. The Record_ID
      // is printable EBCDIC text (the certificate's serial + issuer identifier).
      if (get_parm.record_ID_ptr != NULL && get_parm.record_ID_length > 0)
      {
        int rlen = len_without_trailing_blanks(get_parm.record_ID_ptr, get_parm.record_ID_length);
        if (rlen < 0)
          rlen = 0;
        detail.record_id.assign(get_parm.record_ID_ptr, rlen);
      }
      detail.key_type = get_parm.private_key_type;
      detail.key_size = get_parm.private_key_bitsize;

      // Decode the DER certificate with System SSL for the serial number and
      // validity dates (fields R_datalib does not surface directly).
      if (get_parm.certificate_len > 0)
      {
        gsk_buffer der = {static_cast<gsk_size>(get_parm.certificate_len), buffers->certificate};
        x509_certificate cert;
        memset(&cert, 0, sizeof(cert));
        if (gsk_decode_certificate(&der, &cert) == 0)
        {
          detail.serial_number = to_hex(static_cast<char *>(cert.tbsCertificate.serialNumber.data),
                                        cert.tbsCertificate.serialNumber.length);
          detail.not_before = fmt_tm(cert.tbsCertificate.validity.notBefore);
          detail.not_after = fmt_tm(cert.tbsCertificate.validity.notAfter);
          detail.has_validity = true;
          gsk_free_certificate(&cert);
        }
      }

      found = true;
      break;
    }

    reset_get_parm(&get_parm);
    set_up_r_datalib_parameters(&p, &get_next, owner, ring);
    invoke_r_datalib(&p);
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      break; // end of ring, no match
    if (p.return_code != 0)
    {
      result = record_saf_error(zkr, &p, "IRRSDL64 GETNEXT");
      break;
    }
  }

  data_abort.handle = &handle;
  set_up_r_datalib_parameters(&p, &abort_fn, owner, ring);
  invoke_r_datalib(&p);

  if (result != RTNCD_SUCCESS)
    return result;
  if (!found)
    return record_message(zkr, "SHOW", "Certificate '" + label + "' not found in " + owner + "/" + ring);
  return RTNCD_SUCCESS;
}

int zkr_list_rings(ZKR *zkr, const std::string &owner, const std::string &ring,
                   std::vector<ZKRRingEntry> &rings)
{
  const int kBufSize = 256 * 1024;
  std::vector<char> buf(kBufSize, 0);

  R_datalib_get_ring_info gri;
  memset(&gri, 0, sizeof(gri));
  gri.search_type = 0;
  gri.ring_result_length = kBufSize;
  gri.ring_result_ptr = buf.data();

  R_datalib_parm_list_64 p;
  R_datalib_function fn = {ZKR_RINGINFO_CODE, 0x00000000, 0, &gri};
  set_up_r_datalib_parameters(&p, &fn, owner, ring);
  invoke_r_datalib(&p);
  if (saf_failed(&p))
    return record_saf_error(zkr, &p, "IRRSDL64 GETRINGINFO");

  // 4/4/0 means the output area could not hold every ring (SA23-2293 Table 84).
  const bool truncated = (p.return_code == 4 && p.RACF_return_code == 4 && p.RACF_reason_code == 0);

  const char *cur = buf.data();
  const char *end = buf.data() + kBufSize;
  if (cur + 4 > end)
    return RTNCD_SUCCESS;

  int ring_count = 0;
  memcpy(&ring_count, cur, 4);
  cur += 4;

  for (int i = 0; i < ring_count && cur < end; ++i)
  {
    ZKRRingEntry entry;
    cur = read_lp_string(cur, end, entry.owner);
    cur = read_lp_string(cur, end, entry.name);

    if (cur + 4 > end)
      break;
    int cert_count = 0;
    memcpy(&cert_count, cur, 4);
    cur += 4;

    for (int j = 0; j < cert_count && cur < end; ++j)
    {
      ZKRRingCert c;
      cur = read_lp_string(cur, end, c.owner);
      cur = read_lp_string(cur, end, c.label);
      entry.certs.push_back(c);
    }
    rings.push_back(entry);
  }

  if (truncated)
    note_saf_warning(zkr, &p, "IRRSDL64 GETRINGINFO",
                     "the output area could not hold every key ring; the list is truncated");
  return RTNCD_SUCCESS;
}

namespace
{

// Enumerate `ring` and return the DER bytes and usage code of the certificate
// with the given label. R_datalib DataPut needs the certificate's bytes, and
// DataGetFirst/GetNext is how they are read: from a real ring the certificate
// is connected to, or from the owner's virtual key ring "*", which also covers
// certificates not connected to any ring (verified on z/OS; this is the path
// behind `cert connect --from-database`).
int fetch_cert_from_ring(ZKR *zkr, const std::string &service, const std::string &owner,
                         const std::string &ring, const std::string &label,
                         std::string &der, int &usage_code)
{
  std::unique_ptr<Data_get_buffers> buffers(new Data_get_buffers());
  memset(buffers.get(), 0, sizeof(Data_get_buffers));

  R_datalib_parm_list_64 p;
  R_datalib_data_get g;
  R_datalib_result_handle h;
  R_datalib_data_abort ab;
  memset(&g, 0, sizeof(g));
  memset(&h, 0, sizeof(h));
  g.handle = &h;
  g.certificate_ptr = buffers->certificate;
  g.private_key_ptr = buffers->private_key;
  g.label_ptr = buffers->label;
  g.subjects_DN_ptr = buffers->subject_DN;
  g.record_ID_ptr = buffers->record_id;
  g.certificate_status = ZKR_STATUS_ANY;

  R_datalib_function first = {ZKR_GETCERT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &g};
  R_datalib_function next = {ZKR_GETNEXT_CODE, static_cast<int>(ZKR_ATTR_GETCERT), 1, &g};
  R_datalib_function abrt = {ZKR_DATA_ABORT_CODE, 0x00000000, 0, &ab};

  reset_get_parm(&g);
  set_up_r_datalib_parameters(&p, &first, owner, ring);
  invoke_r_datalib(&p);
  if (p.return_code != 0)
  {
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      return record_message(zkr, service, "Certificate '" + label + "' not found in " + owner + "/" + ring);
    return record_saf_error(zkr, &p, "IRRSDL64 GETCERT");
  }

  bool found = false;
  int result = RTNCD_SUCCESS;
  while (true)
  {
    std::string cur;
    if (g.label_ptr != NULL && g.label_len > 0)
      cur.assign(g.label_ptr, g.label_len);
    if (cur == label)
    {
      if (g.certificate_len > 0)
        der.assign(buffers->certificate, g.certificate_len);
      usage_code = g.certificate_usage;
      found = true;
      break;
    }
    reset_get_parm(&g);
    set_up_r_datalib_parameters(&p, &next, owner, ring);
    invoke_r_datalib(&p);
    if (p.return_code == 8 && p.RACF_return_code == 8 && p.RACF_reason_code == 44)
      break;
    if (p.return_code != 0)
    {
      result = record_saf_error(zkr, &p, "IRRSDL64 GETNEXT");
      break;
    }
  }

  ab.handle = &h;
  set_up_r_datalib_parameters(&p, &abrt, owner, ring);
  invoke_r_datalib(&p);

  if (result != RTNCD_SUCCESS)
    return result;
  if (!found)
    return record_message(zkr, service, "Certificate '" + label + "' not found in " + owner + "/" + ring);
  if (der.empty())
    return record_message(zkr, service, "Certificate '" + label + "' has no retrievable certificate data");
  return RTNCD_SUCCESS;
}

// DataPut a certificate (identified by its DER bytes) to a ring, which connects
// or reconnects it with the given usage and default flag. The certificate must
// already exist in the ESM database (it does, since the bytes were read from a
// ring), so this reconnects rather than re-adding.
int data_put_connect(ZKR *zkr, const std::string &service, const std::string &owner,
                     const std::string &ring, const std::string &label,
                     const std::string &der, int usage_code, bool make_default)
{
  R_datalib_data_put put;
  memset(&put, 0, sizeof(put));
  put.certificate_usage = usage_code;
  put.Default = make_default ? 1 : 0;
  put.certificate_len = static_cast<int>(der.size());
  put.certificate_ptr = const_cast<char *>(der.data());
  put.private_key_len = 0;
  put.private_key_ptr = NULL;

  // Label_ptr must address a 32-byte area (R_datalib updates it on return).
  char label_buf[33];
  memset(label_buf, ' ', 32);
  label_buf[32] = '\0';
  size_t llen = label.size();
  if (llen > ZKR_MAX_LABEL_LEN)
    llen = ZKR_MAX_LABEL_LEN;
  memcpy(label_buf, label.data(), llen);
  put.label_len = static_cast<int>(llen);
  put.label_ptr = label_buf;

  size_t ulen = owner.size();
  if (ulen > ZKR_MAX_USERID_LEN)
    ulen = ZKR_MAX_USERID_LEN;
  put.cert_userid_len = static_cast<char>(ulen);
  memset(put.cert_userid, ' ', ZKR_MAX_USERID_LEN);
  memcpy(put.cert_userid, owner.data(), ulen);

  R_datalib_parm_list_64 p;
  R_datalib_function fn = {ZKR_IMPORT_CODE, 0x00000000, 0, &put};
  set_up_r_datalib_parameters(&p, &fn, owner, ring);
  invoke_r_datalib(&p);

  if (saf_failed(&p))
    return record_saf_error(zkr, &p, service);
  if (p.return_code == 4)
    note_saf_warning(zkr, &p, service, datalib_rc4_message(ZKR_IMPORT_CODE, p.RACF_reason_code));
  return RTNCD_SUCCESS;
}

} // namespace

int zkr_connect_cert(ZKR *zkr, const ZKRConnectOptions &opts)
{
  if (opts.label.empty())
    return record_message(zkr, "CONNECT", "Certificate label is required");
  if (opts.from_ring.empty())
    return record_message(zkr, "CONNECT",
                          "--from-ring or --from-database is required: R_datalib DataPut needs the "
                          "certificate bytes, which are read from a ring the certificate is connected "
                          "to or from the owner's virtual key ring");

  std::string der;
  int current_usage = 0;
  int rc = fetch_cert_from_ring(zkr, "IRRSDL64 CONNECT", opts.owner, opts.from_ring, opts.label, der, current_usage);
  if (rc != RTNCD_SUCCESS)
    return rc;

  int usage_code = current_usage;
  if (!opts.usage.empty())
  {
    usage_code = usage_to_code(opts.usage);
    if (usage_code == 0)
      return record_message(zkr, "CONNECT", "Invalid usage '" + opts.usage + "'. Use CERTAUTH or PERSONAL.");
  }

  return data_put_connect(zkr, "IRRSDL64 CONNECT", opts.owner, opts.ring, opts.label, der, usage_code,
                          opts.make_default);
}

int zkr_set_default(ZKR *zkr, const std::string &owner, const std::string &ring, const std::string &label)
{
  // Read the certificate from the ring it is already in, then re-connect it to
  // the same ring keeping its usage but with the default flag on.
  std::string der;
  int current_usage = 0;
  int rc = fetch_cert_from_ring(zkr, "IRRSDL64 SETDEFAULT", owner, ring, label, der, current_usage);
  if (rc != RTNCD_SUCCESS)
    return rc;
  return data_put_connect(zkr, "IRRSDL64 SETDEFAULT", owner, ring, label, der, current_usage, true);
}

int zkr_alter_cert(ZKR *zkr, const ZKRAlterOptions &opts)
{
  if (opts.label.empty())
    return record_message(zkr, "DATAALTER", "Certificate label is required");

  int attributes = 0;
  if (!opts.status.empty())
  {
    if (opts.status == "TRUST" || opts.status == "trust")
      attributes = static_cast<int>(ZKR_ATTR_TRUST);
    else if (opts.status == "HIGHTRUST" || opts.status == "hightrust")
      attributes = static_cast<int>(ZKR_ATTR_HIGHTRUST);
    else if (opts.status == "NOTRUST" || opts.status == "notrust")
      attributes = static_cast<int>(ZKR_ATTR_NOTRUST);
    else
      return record_message(zkr, "DATAALTER",
                            "Invalid status '" + opts.status + "'. Use TRUST, HIGHTRUST, or NOTRUST.");
  }
  if (opts.status.empty() && opts.new_label.empty())
    return record_message(zkr, "DATAALTER", "Nothing to change: specify a status and/or a new label");
  if (opts.new_label.size() > ZKR_MAX_LABEL_LEN)
    return record_message(zkr, "DATAALTER", "New label is too long");

  R_datalib_data_alter alter;
  memset(&alter, 0, sizeof(alter));
  alter.label_len = static_cast<int>(opts.label.size());
  alter.label_addr = const_cast<char *>(opts.label.c_str());
  if (!opts.new_label.empty())
  {
    alter.new_label_len = static_cast<int>(opts.new_label.size());
    alter.new_label_addr = const_cast<char *>(opts.new_label.c_str());
  }

  size_t ulen = opts.owner.size();
  if (ulen > ZKR_MAX_USERID_LEN)
    ulen = ZKR_MAX_USERID_LEN;
  alter.CERT_userid_len = static_cast<char>(ulen);
  memset(alter.CERT_userid, ' ', ZKR_MAX_USERID_LEN);
  memcpy(alter.CERT_userid, opts.owner.data(), ulen);

  // DataAlter ignores Ring_name and RACF_user_ID in the common parm list; the
  // certificate is identified by CERT_userid + label in the alter parm block.
  R_datalib_parm_list_64 p;
  R_datalib_function fn = {ZKR_ALTER_CODE, attributes, 0, &alter};
  set_up_r_datalib_parameters(&p, &fn, "", "");
  invoke_r_datalib(&p);

  if (saf_failed(&p))
    return record_saf_error(zkr, &p, "IRRSDL64 DATAALTER");
  if (p.return_code == 4)
    note_saf_warning(zkr, &p, "IRRSDL64 DATAALTER",
                     "the change succeeded, but the DIGTCERT class must be refreshed for it to take "
                     "effect (run 'system cert refresh')");
  return RTNCD_SUCCESS;
}
