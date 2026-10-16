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

#include "certificates.hpp"
#include "../zkr.hpp"
#include "../zkrio.hpp"
#include "../zbase64.h"
#include "../zds.hpp"
#include "../ztype.h"
#include "../zut.hpp"
#include <cctype>
#include <string>
#include <vector>

using namespace ast;
using namespace parser;

namespace certificates
{
namespace
{

// Attach the exact SAF/ESM return, reason, and function codes (and any GSK
// status) from the last service call to a response node, so the precise codes
// remain available programmatically even though the human-readable message is
// natural language. Emitted whenever a non-zero code was recorded.
void add_saf_returns(const ast::Node &node, const ZKR &zkr)
{
  if (zkr.diag.saf_rc != 0 || zkr.diag.esm_rc != 0 || zkr.diag.esm_rsn != 0 || zkr.diag.function_code != 0)
  {
    const auto saf = obj();
    saf->set("functionCode", i64(zkr.diag.function_code));
    saf->set("safReturnCode", i64(zkr.diag.saf_rc));
    saf->set("esmReturnCode", i64(zkr.diag.esm_rc));
    saf->set("esmReasonCode", i64(zkr.diag.esm_rsn));
    node->set("safReturns", saf);
  }
  if (zkr.diag.gsk_rc != 0)
    node->set("gskReturnCode", i64(zkr.diag.gsk_rc));
}

// Build the command's return object for a failure so it is inspectable
// programmatically (RPC/SDK), including the structured safReturns section.
void set_error_object(InvocationContext &context, const ZKR &zkr)
{
  const auto err = obj();
  err->set("success", boolean(false));
  err->set("service", str(zkr.diag.service));
  err->set("message", str(zkr.diag.e_msg));
  add_saf_returns(err, zkr);
  context.set_object(err);
}

void report_error(InvocationContext &context, const ZKR &zkr)
{
  context.error_stream() << "Error: " << zkr.diag.e_msg << std::endl;
  set_error_object(context, zkr);
}

// "*" is the ESM *virtual key ring* (the whole collection of a user's
// certificates), not a real ring. Using it as the target of a ring-creating or
// ring-modifying operation is nonsensical or unsafe, so those commands reject it
// with a clear message rather than passing it to R_datalib.
bool reject_virtual_ring(InvocationContext &context, const std::string &ring, const char *op)
{
  if (ring == "*")
  {
    context.error_stream() << "Error: '*' is the virtual key ring (all of the owner's certificates), not a "
                              "real key ring, and cannot be used with "
                           << op << ". Specify a real key ring name." << std::endl;
    return true;
  }
  return false;
}

std::string to_lower(const std::string &in)
{
  std::string out(in);
  for (size_t i = 0; i < out.size(); ++i)
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
  return out;
}

std::string to_upper(const std::string &in)
{
  std::string out(in);
  for (size_t i = 0; i < out.size(); ++i)
    out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
  return out;
}

// A one-shot ring/cert action that only reports success or a SAF diagnostic.
// A non-fatal SAF warning (rc 4) is surfaced on stdout and in the response.
int run_simple(InvocationContext &context, int rc, ZKR &zkr, const std::string &message)
{
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }
  context.output_stream() << message << std::endl;
  if (!zkr.diag.warning.empty())
    context.output_stream() << "Note: " << zkr.diag.warning << std::endl;

  const auto result = obj();
  result->set("success", boolean(true));
  if (!zkr.diag.warning.empty())
    result->set("warning", str(zkr.diag.warning));
  add_saf_returns(result, zkr);
  context.set_object(result);
  return RTNCD_SUCCESS;
}

// Read a PKCS#12 blob out of a sequential data set or PDS/E member. Binary mode so
// LE concatenates record data with no RDWs and no code-page conversion --
// byte-identical to `cp -B "//'DSN'"`. Fixed-format sources are accepted:IBM's GSK
// (Global Security Kit) ignores the record padding that follows the PKCS#12 DER structure.
int read_cert_dsn(const std::string &dsn, std::string &data, std::string &err)
{
  ZDS zds{};
  // zds_dataset_exists() OPENs the data set, which would hang on a password prompt.
  if (zds_validate_no_password(&zds, dsn, false) != RTNCD_SUCCESS)
  {
    err = zds.diag.e_msg;
    return RTNCD_FAILURE;
  }
  if (!zds_dataset_exists(dsn))
  {
    err = "Could not access data set: " + dsn;
    return RTNCD_FAILURE;
  }

  zut_prepare_encoding("binary", &zds.encoding_opts);
  const int rc = zds_read(ZDSReadOpts{.zds = &zds, .dsname = dsn}, data);
  if (rc != RTNCD_SUCCESS)
  {
    err = zds.diag.e_msg;
    return RTNCD_FAILURE;
  }
  if (data.empty())
  {
    err = "PKCS#12 data set is empty: " + dsn;
    return RTNCD_FAILURE;
  }
  return RTNCD_SUCCESS;
}

// Write exported certificate bytes to a data set, creating it when absent.
// is_binary=true  (p12): raw bytes, chunked into V-format records (zds_write_binary).
// is_binary=false (pem): the EBCDIC PEM text is written as records via zds_write in
//                        text mode -- one line per record.
int write_cert_dsn(const std::string &dsn, const std::string &data, bool is_binary, std::string &err)
{
  std::string created_dsn; // set only when this call allocated the data set, so a failed write can roll it back
  ZDS check{};
  // zds_dataset_exists() OPENs the data set, which would hang on a password prompt.
  if (zds_validate_no_password(&check, dsn, true) != RTNCD_SUCCESS)
  {
    err = check.diag.e_msg;
    return RTNCD_FAILURE;
  }
  if (!zds_dataset_exists(dsn))
  {
    const auto open_paren = dsn.find('(');
    const auto close_paren = dsn.find(')');
    const bool has_member = open_paren != std::string::npos && close_paren != std::string::npos && close_paren > open_paren;
    const std::string base_dsn = has_member ? dsn.substr(0, open_paren) : dsn;

    if (has_member)
    {
      const std::string member = dsn.substr(open_paren + 1, close_paren - open_paren - 1);
      if (!zds_is_valid_member_name(member))
      {
        err = "Invalid member name: " + member;
        return RTNCD_FAILURE;
      }
    }

    // RACDCERT EXPORT parity: PS/VB/LRECL(84)/BLKSIZE(27998). Used for PEM too: the
    // 64-column Base64 lines fit, and VB keeps the text byte-exact (FB would blank-pad).
    DS_ATTRIBUTES a{}; // zero-init: zds_create_dsn preserves 0, only negative means "unset"
    a.dsorg = has_member ? "PO" : "PS";
    a.recfm = "V,B"; // mandatory -- FB pads records with blanks, which would change the byte count
    a.lrecl = 84;
    a.blksize = 27998;
    a.alcunit = "TRK";
    a.primary = 5;
    a.secondary = 5;
    if (has_member)
    {
      a.dirblk = 5;
      a.dsntype = ZDS_DSNTYPE_LIBRARY;
    }

    std::string response;
    if (zds_create_dsn(nullptr, base_dsn, a, response) != RTNCD_SUCCESS)
    {
      err = "Could not create data set '" + base_dsn + "': " + response;
      return RTNCD_FAILURE;
    }
    created_dsn = base_dsn;
  }

  ZDS zds{}; // eDataTypeText + empty codepage -> zds_use_codepage() is false -> no iconv for PEM
  const int rc = is_binary
                     ? zds_write_binary(ZDSWriteOpts{.zds = &zds, .dsname = dsn}, data)
                     : zds_write(ZDSWriteOpts{.zds = &zds, .dsname = dsn}, data);
  if (rc != RTNCD_SUCCESS)
  {
    err = zds.diag.e_msg;
    if (!created_dsn.empty())
    {
      ZDS cleanup{};
      if (zds_delete_dsn(&cleanup, created_dsn) != RTNCD_SUCCESS)
        err += "; could not remove partially written data set '" + created_dsn + "'";
    }
    return RTNCD_FAILURE;
  }
  return RTNCD_SUCCESS;
}

} // namespace

int handle_cert_list(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  const std::string label_filter = context.get<std::string>("label", "");
  const std::string usage_filter = context.get<std::string>("usage", "");
  const bool label_only = context.get<bool>("label-only", false);
  const bool owner_only = context.get<bool>("owner-only", false);
  // Default to a bounded page so an enormous ring can't flood the caller or the
  // LPAR; --max-entries 0 explicitly requests the full (unbounded) enumeration.
  const long long max_entries = context.get<long long>("max-entries", 10);
  if (max_entries < 0)
  {
    context.error_stream() << "Error: --max-entries must be 0 (unlimited) or a positive number" << std::endl;
    return RTNCD_FAILURE;
  }

  // The filters follow RACDCERT LABEL semantics (exact, case-sensitive, no
  // wildcards), so a match may sit anywhere in the ring. With a filter active,
  // enumerate the whole ring and apply --max-entries to the MATCHING rows;
  // without one, cap the enumeration itself (cheaper on enormous rings).
  const bool has_filter = !label_filter.empty() || !usage_filter.empty();

  ZKR zkr{};
  std::vector<ZKRCertInfo> certs;
  bool more_available = false;
  const int rc = zkr_list_ring(&zkr, owner, ring, certs,
                               has_filter ? 0 : static_cast<size_t>(max_entries), &more_available);
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }

  bool filter_more = false;
  const std::vector<ZKRCertInfo> shown_certs =
      zkr_filter_certs(certs, label_filter, usage_filter, static_cast<size_t>(max_entries), &filter_more);
  if (has_filter)
    more_available = filter_more;

  const auto items = arr();
  for (std::vector<ZKRCertInfo>::const_iterator it = shown_certs.begin(); it != shown_certs.end(); ++it)
  {
    if (label_only)
    {
      context.output_stream() << it->label << std::endl;
    }
    else if (owner_only)
    {
      context.output_stream() << it->owner << std::endl;
    }
    else
    {
      context.output_stream() << "Certificate: " << it->label << std::endl;
      context.output_stream() << "Owner: " << it->owner << std::endl;
      context.output_stream() << "Usage: " << it->usage << std::endl;
      context.output_stream() << "Status: " << it->status << std::endl;
      context.output_stream() << "Default: " << (it->is_default ? "YES" : "NO") << std::endl;
    }

    const auto item = obj();
    item->set("label", str(it->label));
    item->set("owner", str(it->owner));
    item->set("usage", str(it->usage));
    item->set("status", str(it->status));
    item->set("default", boolean(it->is_default));
    items->push(item);
  }

  if (more_available)
  {
    context.output_stream() << "Note: more " << (has_filter ? "matching " : "")
                            << "certificates are available; showing the first "
                            << max_entries << ". Use --max-entries 0 for all, or a higher limit."
                            << std::endl;
  }

  const auto result = obj();
  result->set("items", items);
  result->set("returnedRows", i64(static_cast<long long>(shown_certs.size())));
  result->set("moreAvailable", boolean(more_available));
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_cert_export(InvocationContext &context)
{
  ZKRExportOptions opts;
  opts.owner = context.get<std::string>("owner", "");
  opts.ring = context.get<std::string>("keyring", "");
  opts.label = context.get<std::string>("label", "");
  opts.password = context.get<std::string>("password", "");
  const std::string file = context.get<std::string>("file", "");
  const std::string dsn = to_upper(context.get<std::string>("dsn", ""));

  if (opts.label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (!file.empty() && !dsn.empty())
  {
    context.error_stream() << "Error: specify --file or --dsn, not both" << std::endl;
    return RTNCD_FAILURE;
  }

  const bool is_p12 = (to_lower(context.get<std::string>("format", "pem")) == "p12");
  opts.format = is_p12 ? "p12" : "pem";

  if (is_p12 && opts.password.empty())
  {
    context.error_stream() << "Error: --password is required with --format p12" << std::endl;
    return RTNCD_FAILURE;
  }
  if (is_p12 && file.empty() && dsn.empty())
  {
    context.error_stream() << "Error: PKCS#12 output is binary; specify --file or --dsn" << std::endl;
    return RTNCD_FAILURE;
  }

  ZKR zkr{};
  std::string data;
  const int rc = zkr_export_cert(&zkr, opts, data);
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }

  const auto result = obj();
  result->set("label", str(opts.label));
  result->set("owner", str(opts.owner));
  result->set("keyring", str(opts.ring));
  result->set("format", str(opts.format));

  if (!file.empty())
  {
    // The exported bytes may be a PKCS#12 bundle containing a private key, so
    // the file is created (or truncated) owner-read/write only, independent of
    // the process umask.
    std::string err;
    if (zkrio_write_file(file, data, err) != RTNCD_SUCCESS)
    {
      context.error_stream() << "Error: could not write output file: " << file << " (" << err << ")" << std::endl;
      return RTNCD_FAILURE;
    }
    context.output_stream() << "Certificate written to " << file
                            << " (" << data.size() << " bytes)" << std::endl;
    result->set("file", str(file));
    result->set("bytesWritten", i64(static_cast<long long>(data.size())));
  }
  else if (!dsn.empty())
  {
    std::string err;
    if (zkrio_write_dsn(dsn, data, is_p12, err) != RTNCD_SUCCESS)
    {
      context.error_stream() << "Error: could not write output data set: " << dsn << " (" << err << ")" << std::endl;
      return RTNCD_FAILURE;
    }
    context.output_stream() << "Certificate written to data set " << dsn
                            << " (" << data.size() << " bytes)" << std::endl;
    result->set("dsn", str(dsn));
    result->set("bytesWritten", i64(static_cast<long long>(data.size())));
  }
  else
  {
    // Neither --file nor --dsn: only reachable for PEM (p12 was rejected above).
    context.output_stream() << data; // PEM text already terminates with a newline
  }

  // Expose the certificate bytes (base64) for programmatic consumers. The CLI
  // file/stdout output above stays in the native EBCDIC code page (byte-identical
  // to keyring-util), but PEM is text, so convert it to ISO8859-1 here to hand
  // off-platform SDK consumers portable ASCII PEM. PKCS#12 is binary -- pass it
  // through untouched.
  std::string portable = data;
  if (!is_p12)
  {
    ZDIAG diag = {0};
    const std::string converted = zut_encode(data, "IBM-1047", "ISO8859-1", diag);
    if (!converted.empty())
      portable = converted;
  }
  result->set("data", str(zbase64::encode(portable)));
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_cert_import(InvocationContext &context)
{
  ZKRImportOptions opts;
  opts.owner = context.get<std::string>("owner", "");
  opts.ring = context.get<std::string>("keyring", "");
  opts.label = context.get<std::string>("label", "");
  opts.usage = context.get<std::string>("usage", "");
  opts.p12_path = context.get<std::string>("file", "");
  const std::string dsn = to_upper(context.get<std::string>("dsn", ""));
  opts.password = context.get<std::string>("password", "");
  opts.skip_refresh = context.get<bool>("skip-refresh", false);

  if (opts.label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (opts.usage.empty())
  {
    context.error_stream() << "Error: --usage is required (PERSONAL or CERTAUTH)" << std::endl;
    return RTNCD_FAILURE;
  }
  if (!opts.p12_path.empty() && !dsn.empty())
  {
    context.error_stream() << "Error: specify --file or --dsn, not both" << std::endl;
    return RTNCD_FAILURE;
  }
  if (opts.p12_path.empty() && dsn.empty())
  {
    context.error_stream() << "Error: --file or --dsn is required (source PKCS#12)" << std::endl;
    return RTNCD_FAILURE;
  }
  if (opts.password.empty())
  {
    context.error_stream() << "Error: --password is required (PKCS#12 passphrase)" << std::endl;
    return RTNCD_FAILURE;
  }
  if (reject_virtual_ring(context, opts.ring, "cert import"))
    return RTNCD_FAILURE;

  if (!dsn.empty())
  {
    std::string err;
    if (zkrio_read_dsn(dsn, opts.p12_data, err) != RTNCD_SUCCESS)
    {
      context.error_stream() << "Error: could not read source data set: " << dsn << " (" << err << ")" << std::endl;
      return RTNCD_FAILURE;
    }
  }

  ZKR zkr{};
  const int rc = zkr_import_cert(&zkr, opts);
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }

  context.output_stream() << "Certificate '" << opts.label << "' imported into "
                          << opts.owner << "/" << opts.ring << std::endl;
  if (!zkr.diag.warning.empty())
    context.output_stream() << "Note: " << zkr.diag.warning << std::endl;

  const auto result = obj();
  result->set("success", boolean(true));
  result->set("label", str(opts.label));
  result->set("owner", str(opts.owner));
  result->set("keyring", str(opts.ring));
  if (!zkr.diag.warning.empty())
    result->set("warning", str(zkr.diag.warning));
  add_saf_returns(result, zkr);
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_cert_delete(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  const std::string label = context.get<std::string>("label", "");
  const bool skip_refresh = context.get<bool>("skip-refresh", false);
  const bool database = context.get<bool>("database", false);

  if (label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (ring == "*")
  {
    context.error_stream() << "Error: use --database (not '*') to delete a certificate from the ESM database"
                           << std::endl;
    return RTNCD_FAILURE;
  }
  if (database && !ring.empty())
  {
    context.error_stream() << "Error: specify either a key ring or --database, not both" << std::endl;
    return RTNCD_FAILURE;
  }
  if (!database && ring.empty())
  {
    context.error_stream() << "Error: specify a key ring to disconnect the certificate from, or --database to "
                              "delete it from the ESM database"
                           << std::endl;
    return RTNCD_FAILURE;
  }

  // --database maps to the R_datalib virtual key ring ("*"), which removes the
  // certificate from the ESM database rather than a single ring.
  const std::string effective_ring = database ? "*" : ring;
  ZKR zkr{};
  const int rc = zkr_del_cert(&zkr, owner, effective_ring, label, skip_refresh);
  const std::string where = database ? "the ESM database" : (owner + "/" + ring);
  return run_simple(context, rc, zkr, "Certificate '" + label + "' removed from " + where);
}

int handle_create_ring(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  if (reject_virtual_ring(context, ring, "keyring create"))
    return RTNCD_FAILURE;
  ZKR zkr{};
  const int rc = zkr_new_ring(&zkr, owner, ring);
  return run_simple(context, rc, zkr, "Key ring created: " + owner + "/" + ring);
}

int handle_delete_ring(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  if (reject_virtual_ring(context, ring, "keyring delete"))
    return RTNCD_FAILURE;
  ZKR zkr{};
  const int rc = zkr_del_ring(&zkr, owner, ring);
  return run_simple(context, rc, zkr, "Key ring deleted: " + owner + "/" + ring);
}

int handle_ring_count(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");

  ZKR zkr{};
  long long total = 0;

  if (ring == "*")
  {
    // GetRingInfo does not accept the virtual key ring, so the only way to count
    // all of a user's certificates is the DataGetFirst/GetNext enumeration.
    std::vector<ZKRCertInfo> certs;
    const int rc = zkr_list_ring(&zkr, owner, ring, certs, 0, nullptr);
    if (rc != RTNCD_SUCCESS)
    {
      report_error(context, zkr);
      return RTNCD_FAILURE;
    }
    total = static_cast<long long>(certs.size());
  }
  else
  {
    // GetRingInfo (R_datalib X'0D') returns each ring's Cert_count in one call,
    // so a real ring's count avoids the DataGetFirst/GetNext enumeration loop.
    std::vector<ZKRRingEntry> rings;
    const int rc = zkr_list_rings(&zkr, owner, ring, rings);
    if (rc != RTNCD_SUCCESS)
    {
      report_error(context, zkr);
      return RTNCD_FAILURE;
    }
    for (std::vector<ZKRRingEntry>::const_iterator it = rings.begin(); it != rings.end(); ++it)
      total += static_cast<long long>(it->certs.size());
  }

  context.output_stream() << total << " certificate(s) in " << owner << "/" << ring << std::endl;
  const auto result = obj();
  result->set("count", i64(total));
  result->set("owner", str(owner));
  result->set("keyring", str(ring));
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_refresh(InvocationContext &context)
{
  ZKR zkr{};
  const int rc = zkr_refresh(&zkr);
  return run_simple(context, rc, zkr, "DIGTCERT class refreshed");
}

int handle_cert_show(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  const std::string label = context.get<std::string>("label", "");
  if (label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }

  ZKR zkr{};
  ZKRCertDetail d;
  const int rc = zkr_show_cert(&zkr, owner, ring, label, d);
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }

  std::ostream &out = context.output_stream();
  out << "Certificate:   " << d.label << std::endl;
  out << "Owner:         " << d.owner << std::endl;
  out << "Usage:         " << d.usage << std::endl;
  out << "Status:        " << d.status << std::endl;
  out << "Default:       " << (d.is_default ? "YES" : "NO") << std::endl;
  if (!d.subject.empty())
    out << "Subject:       " << d.subject << std::endl;
  if (d.has_validity)
  {
    out << "Not before:    " << d.not_before << std::endl;
    out << "Not after:     " << d.not_after << std::endl;
  }
  if (!d.serial_number.empty())
    out << "Serial number: " << d.serial_number << std::endl;
  if (d.key_size > 0)
    out << "Key size:      " << d.key_size << " bits" << std::endl;
  if (!d.record_id.empty())
    out << "Record ID:     " << d.record_id << std::endl;

  const auto result = obj();
  result->set("label", str(d.label));
  result->set("owner", str(d.owner));
  result->set("usage", str(d.usage));
  result->set("status", str(d.status));
  result->set("default", boolean(d.is_default));
  result->set("keyType", i64(d.key_type));
  result->set("keySize", i64(d.key_size));
  if (!d.subject.empty())
    result->set("subject", str(d.subject));
  if (d.has_validity)
  {
    result->set("notBefore", str(d.not_before));
    result->set("notAfter", str(d.not_after));
  }
  if (!d.serial_number.empty())
    result->set("serialNumber", str(d.serial_number));
  if (!d.record_id.empty())
    result->set("recordId", str(d.record_id));
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_list_rings(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  std::string ring = context.get<std::string>("keyring", ""); // optional filter
  // list-rings enumerates real key rings; "*" here just means "all rings" (an
  // empty filter), not the virtual key ring, so GetRingInfo does not reject it.
  if (ring == "*")
    ring = "";

  ZKR zkr{};
  std::vector<ZKRRingEntry> rings;
  const int rc = zkr_list_rings(&zkr, owner, ring, rings);
  if (rc != RTNCD_SUCCESS)
  {
    report_error(context, zkr);
    return RTNCD_FAILURE;
  }

  const auto items = arr();
  for (std::vector<ZKRRingEntry>::const_iterator it = rings.begin(); it != rings.end(); ++it)
  {
    context.output_stream() << "Key ring: " << it->owner << "/" << it->name
                            << " (" << it->certs.size() << " certificate(s))" << std::endl;
    const auto ring_obj = obj();
    ring_obj->set("owner", str(it->owner));
    ring_obj->set("name", str(it->name));
    const auto certs = arr();
    for (std::vector<ZKRRingCert>::const_iterator c = it->certs.begin(); c != it->certs.end(); ++c)
    {
      context.output_stream() << "  - " << c->label << " (" << c->owner << ")" << std::endl;
      const auto co = obj();
      co->set("owner", str(c->owner));
      co->set("label", str(c->label));
      certs->push(co);
    }
    ring_obj->set("certificates", certs);
    items->push(ring_obj);
  }

  if (!zkr.diag.warning.empty())
    context.output_stream() << "Note: " << zkr.diag.warning << std::endl;

  const auto result = obj();
  result->set("items", items);
  result->set("returnedRows", i64(static_cast<long long>(rings.size())));
  if (!zkr.diag.warning.empty())
    result->set("warning", str(zkr.diag.warning));
  add_saf_returns(result, zkr);
  context.set_object(result);
  return RTNCD_SUCCESS;
}

int handle_cert_connect(InvocationContext &context)
{
  ZKRConnectOptions opts;
  opts.owner = context.get<std::string>("owner", "");
  opts.ring = context.get<std::string>("keyring", "");
  const std::string from_ring = context.get<std::string>("from-ring", "");
  const bool from_database = context.get<bool>("from-database", false);
  opts.label = context.get<std::string>("label", "");
  opts.usage = context.get<std::string>("usage", "");
  opts.make_default = context.get<bool>("default", false);

  if (opts.label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (reject_virtual_ring(context, opts.ring, "cert connect (target ring)"))
    return RTNCD_FAILURE;
  if (from_ring == "*")
  {
    context.error_stream() << "Error: use --from-database (not '*') to read a certificate from the ESM database"
                           << std::endl;
    return RTNCD_FAILURE;
  }
  if (from_database && !from_ring.empty())
  {
    context.error_stream() << "Error: specify either --from-ring or --from-database, not both" << std::endl;
    return RTNCD_FAILURE;
  }
  if (!from_database && from_ring.empty())
  {
    context.error_stream() << "Error: specify --from-ring (a ring the certificate is already on) or --from-database"
                           << std::endl;
    return RTNCD_FAILURE;
  }
  // --from-database reads the certificate from the owner's virtual key ring ("*").
  opts.from_ring = from_database ? "*" : from_ring;

  ZKR zkr{};
  const int rc = zkr_connect_cert(&zkr, opts);
  return run_simple(context, rc, zkr,
                    "Certificate '" + opts.label + "' connected to " + opts.owner + "/" + opts.ring);
}

int handle_cert_set_default(InvocationContext &context)
{
  const std::string owner = context.get<std::string>("owner", "");
  const std::string ring = context.get<std::string>("keyring", "");
  const std::string label = context.get<std::string>("label", "");
  if (label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (reject_virtual_ring(context, ring, "cert set-default"))
    return RTNCD_FAILURE;

  ZKR zkr{};
  const int rc = zkr_set_default(&zkr, owner, ring, label);
  return run_simple(context, rc, zkr,
                    "Certificate '" + label + "' set as the default certificate in " + owner + "/" + ring);
}

int handle_cert_trust(InvocationContext &context)
{
  ZKRAlterOptions opts;
  opts.owner = context.get<std::string>("owner", "");
  opts.label = context.get<std::string>("label", "");
  opts.status = context.get<std::string>("status", "");
  if (opts.label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (opts.status.empty())
  {
    context.error_stream() << "Error: --status is required (TRUST, HIGHTRUST, or NOTRUST)" << std::endl;
    return RTNCD_FAILURE;
  }

  ZKR zkr{};
  const int rc = zkr_alter_cert(&zkr, opts);
  return run_simple(context, rc, zkr,
                    "Certificate '" + opts.label + "' status changed to " + opts.status);
}

int handle_cert_rename(InvocationContext &context)
{
  ZKRAlterOptions opts;
  opts.owner = context.get<std::string>("owner", "");
  opts.label = context.get<std::string>("label", "");
  opts.new_label = context.get<std::string>("new-label", "");
  if (opts.label.empty())
  {
    context.error_stream() << "Error: --label is required" << std::endl;
    return RTNCD_FAILURE;
  }
  if (opts.new_label.empty())
  {
    context.error_stream() << "Error: --new-label is required" << std::endl;
    return RTNCD_FAILURE;
  }

  ZKR zkr{};
  const int rc = zkr_alter_cert(&zkr, opts);
  return run_simple(context, rc, zkr,
                    "Certificate '" + opts.label + "' renamed to '" + opts.new_label + "'");
}

// Registers the `keyring` and `cert` command groups as siblings under the given
// parent (the `system` group).
void register_commands(parser::Command &parent)
{
  //
  // <parent> keyring <verb> -- key ring operations
  //
  auto keyring_cmd = command_ptr(new Command("keyring", "z/OS ESM key ring operations"));
  keyring_cmd->add_alias("ring");

  // keyring create <owner> <keyring>
  auto ring_create_cmd = command_ptr(new Command("create", "create a new key ring"));
  ring_create_cmd->add_alias("cre");
  ring_create_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  ring_create_cmd->add_positional_arg("keyring", "key ring name (case-sensitive)", ArgType_Single, true);
  ring_create_cmd->set_handler(handle_create_ring);
  ring_create_cmd->add_example("Create a key ring", "zo system keyring create USER01 RING02");
  keyring_cmd->add_command(ring_create_cmd);

  // keyring delete <owner> <keyring>
  auto ring_delete_cmd = command_ptr(new Command("delete", "delete a key ring"));
  ring_delete_cmd->add_alias("del");
  ring_delete_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  ring_delete_cmd->add_positional_arg("keyring", "key ring name (case-sensitive)", ArgType_Single, true);
  ring_delete_cmd->set_handler(handle_delete_ring);
  ring_delete_cmd->add_example("Delete a key ring", "zo system keyring delete USER01 RING02");
  keyring_cmd->add_command(ring_delete_cmd);

  // keyring list <owner> <keyring>
  auto ring_list_cmd = command_ptr(new Command("list", "list certificates connected to a key ring"));
  ring_list_cmd->add_alias("ls");
  ring_list_cmd->add_positional_arg("owner", "key ring owner (userid; case-sensitive, normally uppercase)", ArgType_Single, true);
  ring_list_cmd->add_positional_arg("keyring", "key ring name (case-sensitive), or '*' for the owner's virtual key ring (all their certificates)", ArgType_Single, true);
  ring_list_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "filter by certificate label (exact, case-sensitive)", ArgType_Single, false);
  ring_list_cmd->add_keyword_arg("usage", make_aliases("--usage", "-u"), "filter by usage (exact, case-sensitive: PERSONAL, CERTAUTH, or OTHER)", ArgType_Single, false);
  ring_list_cmd->add_keyword_arg("label-only", make_aliases("--label-only"), "print only certificate labels", ArgType_Flag, false, ArgValue(false));
  ring_list_cmd->add_keyword_arg("owner-only", make_aliases("--owner-only"), "print only certificate owners", ArgType_Flag, false, ArgValue(false));
  ring_list_cmd->add_keyword_arg("max-entries", make_aliases("--max-entries", "--me"), "maximum certificates (matching certificates, when filtering) to return; default 10, use 0 for all", ArgType_Single, false);
  ring_list_cmd->set_handler(handle_cert_list);
  ring_list_cmd->add_example("List certificates in a ring", "zo system keyring list USER01 RING02");
  ring_list_cmd->add_example("List all certificates the user owns", "zo system keyring list USER01 '*' --max-entries 0");
  keyring_cmd->add_command(ring_list_cmd);

  // keyring list-rings <owner> [keyring]
  auto list_rings_cmd = command_ptr(new Command("list-rings", "list a user's key rings and their certificates"));
  list_rings_cmd->add_alias("lr");
  list_rings_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  list_rings_cmd->add_positional_arg("keyring", "optional key ring name to narrow the result (omit or '*' for all rings)", ArgType_Single, false);
  list_rings_cmd->set_handler(handle_list_rings);
  list_rings_cmd->add_example("List all of a user's key rings", "zo system keyring list-rings USER01");
  keyring_cmd->add_command(list_rings_cmd);

  // keyring count <owner> <keyring>
  auto count_cmd = command_ptr(new Command("count", "count the certificates connected to a key ring"));
  count_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  count_cmd->add_positional_arg("keyring", "key ring name, or '*' for the owner's virtual key ring", ArgType_Single, true);
  count_cmd->set_handler(handle_ring_count);
  count_cmd->add_example("Count certificates in a ring", "zo system keyring count USER01 RING02");
  keyring_cmd->add_command(count_cmd);

  //
  // <parent> cert <verb> -- certificate operations
  //
  auto certops_cmd = command_ptr(new Command("cert", "z/OS ESM certificate operations"));
  certops_cmd->add_alias("certificate");

  // cert export <owner> <keyring> --label L
  auto export_cmd = command_ptr(new Command("export", "export a certificate from a key ring (PEM or PKCS#12) to a file or a data set"));
  export_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  export_cmd->add_positional_arg("keyring", "key ring name, or '*' for the owner's virtual key ring", ArgType_Single, true);
  export_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  export_cmd->add_keyword_arg("format", make_aliases("--format", "-F"), "export format: pem (certificate) or p12 (certificate + private key)", ArgType_Single, false, ArgValue(std::string("pem")));
  export_cmd->add_keyword_arg("file", make_aliases("--file", "-f"), "output file path (p12 requires --file or --dsn; PEM prints to stdout when neither is supplied); mutually exclusive with --dsn", ArgType_Single, false);
  export_cmd->add_keyword_arg("dsn", make_aliases("--dsn"),
                              "output data set (sequential or PDS/E member), created if absent; mutually exclusive with --file. "
                              "Protection is the RACF DATASET profile, not file permissions.",
                              ArgType_Single, false);
  export_cmd->add_keyword_arg("password", make_aliases("--password", "-p"), "PKCS#12 passphrase (required with --format p12)", ArgType_Single, false);
  export_cmd->set_handler(handle_cert_export);
  export_cmd->add_example("Export a certificate as PEM", "zo system cert export USER01 RING02 -l CERT03 -f ./CERT03.pem");
  export_cmd->add_example("Export a certificate + key as PKCS#12", "zo system cert export USER01 RING02 -l CERT03 -F p12 -f ./CERT03.p12 -p secret");
  export_cmd->add_example("Export a certificate + key as PKCS#12 to a data set", "zo system cert export USER01 RING02 -l CERT03 -F p12 -p secret --dsn USER01.CERT03.P12");
  certops_cmd->add_command(export_cmd);

  // cert import <owner> <keyring> --label L --usage U --file F --password P
  auto import_cmd = command_ptr(new Command("import", "import a certificate into a key ring from a PKCS#12 file or data set"));
  import_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  import_cmd->add_positional_arg("keyring", "key ring name (case-sensitive)", ArgType_Single, true);
  import_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label to assign (a new certificate only; an existing record keeps its own label)", ArgType_Single, true);
  import_cmd->add_keyword_arg("usage", make_aliases("--usage", "-u"), "certificate usage: PERSONAL or CERTAUTH", ArgType_Single, true);
  import_cmd->add_keyword_arg("file", make_aliases("--file", "-f"), "path to the source PKCS#12 file; mutually exclusive with --dsn", ArgType_Single, false);
  import_cmd->add_keyword_arg("dsn", make_aliases("--dsn"),
                              "source PKCS#12 data set (sequential or PDS/E member); mutually exclusive with --file",
                              ArgType_Single, false);
  import_cmd->add_keyword_arg("password", make_aliases("--password", "-p"), "PKCS#12 passphrase", ArgType_Single, true);
  import_cmd->add_keyword_arg("skip-refresh", make_aliases("--skip-refresh"), "do not automatically REFRESH the DIGTCERT class if the ESM reports it is required (by default the refresh is issued so the change takes effect)", ArgType_Flag, false, ArgValue(false));
  import_cmd->set_handler(handle_cert_import);
  import_cmd->add_example("Import a personal certificate", "zo system cert import USER01 RING02 -l CERT03 -u PERSONAL -f ./file.p12 -p secret");
  import_cmd->add_example("Import a personal certificate from a data set", "zo system cert import USER01 RING02 -l CERT03 -u PERSONAL -p secret --dsn USER01.CERT03.P12");
  certops_cmd->add_command(import_cmd);

  // cert delete <owner> [keyring] --label L [--database]
  auto delete_cmd = command_ptr(new Command("delete", "disconnect a certificate from a key ring, or delete it from the ESM database"));
  delete_cmd->add_alias("del");
  delete_cmd->add_positional_arg("owner", "certificate owner (userid)", ArgType_Single, true);
  delete_cmd->add_positional_arg("keyring", "key ring to disconnect the certificate from (omit and use --database to delete from the ESM database)", ArgType_Single, false);
  delete_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  delete_cmd->add_keyword_arg("database", make_aliases("--database", "--db"), "delete the certificate from the ESM database (removes it entirely, not just from one ring)", ArgType_Flag, false, ArgValue(false));
  delete_cmd->add_keyword_arg("skip-refresh", make_aliases("--skip-refresh"), "do not automatically REFRESH the DIGTCERT class if the ESM reports it is required (by default the refresh is issued so the change takes effect)", ArgType_Flag, false, ArgValue(false));
  delete_cmd->set_handler(handle_cert_delete);
  delete_cmd->add_example("Disconnect a certificate from a ring", "zo system cert delete USER01 RING02 -l CERT03");
  delete_cmd->add_example("Delete a certificate from the ESM database", "zo system cert delete USER01 -l CERT03 --database");
  certops_cmd->add_command(delete_cmd);

  // cert show <owner> <keyring> --label L
  auto show_cmd = command_ptr(new Command("show", "show detailed information for a certificate in a key ring"));
  show_cmd->add_positional_arg("owner", "key ring owner (userid)", ArgType_Single, true);
  show_cmd->add_positional_arg("keyring", "key ring name, or '*' for the owner's virtual key ring", ArgType_Single, true);
  show_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  show_cmd->set_handler(handle_cert_show);
  show_cmd->add_example("Show certificate detail", "zo system cert show USER01 RING02 -l CERT03");
  certops_cmd->add_command(show_cmd);

  // cert connect <owner> <keyring> --label L (--from-ring R | --from-database)
  auto connect_cmd = command_ptr(new Command("connect", "connect a certificate to a key ring (read from another ring or the database)"));
  connect_cmd->add_positional_arg("owner", "certificate owner (userid)", ArgType_Single, true);
  connect_cmd->add_positional_arg("keyring", "target key ring name", ArgType_Single, true);
  connect_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  connect_cmd->add_keyword_arg("from-ring", make_aliases("--from-ring"), "source key ring the certificate is already on (R_datalib DataPut needs the certificate bytes, which are read from here)", ArgType_Single, false);
  connect_cmd->add_keyword_arg("from-database", make_aliases("--from-database", "--from-db"), "read the certificate from the ESM database instead of a specific ring (use when it is not connected to any ring)", ArgType_Flag, false, ArgValue(false));
  connect_cmd->add_keyword_arg("usage", make_aliases("--usage", "-u"), "certificate usage: PERSONAL or CERTAUTH (default: the certificate's current usage)", ArgType_Single, false);
  connect_cmd->add_keyword_arg("default", make_aliases("--default"), "set this certificate as the target ring's default", ArgType_Flag, false, ArgValue(false));
  connect_cmd->set_handler(handle_cert_connect);
  connect_cmd->add_example("Copy a certificate from one ring to another", "zo system cert connect USER01 RING02 -l CACERT --from-ring RING01");
  connect_cmd->add_example("Connect a certificate that is only in the database", "zo system cert connect USER01 RING02 -l CACERT --from-database");
  certops_cmd->add_command(connect_cmd);

  // cert set-default <owner> <keyring> --label L
  auto set_default_cmd = command_ptr(new Command("set-default", "make a certificate the key ring's default"));
  set_default_cmd->add_positional_arg("owner", "certificate owner (userid)", ArgType_Single, true);
  set_default_cmd->add_positional_arg("keyring", "key ring name", ArgType_Single, true);
  set_default_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  set_default_cmd->set_handler(handle_cert_set_default);
  set_default_cmd->add_example("Set the ring default certificate", "zo system cert set-default USER01 RING02 -l CERT03");
  certops_cmd->add_command(set_default_cmd);

  // cert trust <owner> --label L --status S  (DataAlter; ring not required)
  auto trust_cmd = command_ptr(new Command("trust", "change a certificate's trust status (ESM DataAlter)"));
  trust_cmd->add_positional_arg("owner", "certificate owner (userid)", ArgType_Single, true);
  trust_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "certificate label", ArgType_Single, true);
  trust_cmd->add_keyword_arg("status", make_aliases("--status", "-s"), "new trust status: TRUST, HIGHTRUST, or NOTRUST", ArgType_Single, true);
  trust_cmd->set_handler(handle_cert_trust);
  trust_cmd->add_example("Mark a certificate NOTRUST", "zo system cert trust USER01 -l CERT03 -s NOTRUST");
  certops_cmd->add_command(trust_cmd);

  // cert rename <owner> --label L --new-label N  (DataAlter; ring not required)
  auto rename_cmd = command_ptr(new Command("rename", "change a certificate's label (ESM DataAlter)"));
  rename_cmd->add_positional_arg("owner", "certificate owner (userid)", ArgType_Single, true);
  rename_cmd->add_keyword_arg("label", make_aliases("--label", "-l"), "current certificate label", ArgType_Single, true);
  rename_cmd->add_keyword_arg("new-label", make_aliases("--new-label", "-n"), "new certificate label", ArgType_Single, true);
  rename_cmd->set_handler(handle_cert_rename);
  rename_cmd->add_example("Rename a certificate", "zo system cert rename USER01 -l OLDLABEL -n NEWLABEL");
  certops_cmd->add_command(rename_cmd);

  // cert refresh -- refresh the DIGTCERT class
  auto refresh_cmd = command_ptr(new Command("refresh", "refresh the DIGTCERT class"));
  refresh_cmd->set_handler(handle_refresh);
  refresh_cmd->add_example("Refresh the DIGTCERT class", "zo system cert refresh");
  certops_cmd->add_command(refresh_cmd);

  parent.add_command(keyring_cmd);
  parent.add_command(certops_cmd);
}
} // namespace certificates
