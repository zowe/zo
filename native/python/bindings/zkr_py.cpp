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

#include "zkr_py.hpp"
#include "../../c/zkrio.hpp"

namespace
{

std::string get_warning(const ZKR &zkr)
{
  std::string warning = zkr.diag.warning;
  e2a_inplace(warning);
  return warning;
}

// Throws ZkrError, with the diagnostic strings converted to ASCII, when rc != 0.
void zkr_raise(const ZKR &zkr, int rc)
{
  if (rc == 0)
    return;

  std::string msg = zkr.diag.e_msg;
  e2a_inplace(msg);
  std::string service = zkr.diag.service;
  e2a_inplace(service);
  if (msg.empty())
    msg = service + " failed (SAF " + std::to_string(zkr.diag.saf_rc) + "/" + std::to_string(zkr.diag.esm_rc) +
          "/" + std::to_string(zkr.diag.esm_rsn) + ")";

  throw ZkrError(msg, zkr.diag.function_code, zkr.diag.saf_rc, zkr.diag.esm_rc, zkr.diag.esm_rsn, zkr.diag.gsk_rc,
                 service);
}

// "*" is the ESM virtual key ring (all of the owner's certificates), not a real ring;
// using it as the target of a ring-creating or ring-modifying operation is rejected,
// mirroring commands/certificates.cpp's reject_virtual_ring.
void reject_virtual_keyring(const std::string &keyring, const char *op)
{
  if (keyring == "*")
    throw std::invalid_argument(std::string("'*' is the virtual key ring (all of the owner's certificates), "
                                            "not a real key ring, and cannot be used with ") +
                                op + "; specify a real key ring name");
}

// Shared validation + conversion + call for the three import_certificate* entry points.
// opts.p12_data (bytes import / DSN import) or opts.p12_path (file import) must already
// be set by the caller; convert_path selects which one needs a2e.
std::string do_import(ZKRImportOptions opts, bool convert_path)
{
  if (opts.label.empty())
    throw std::invalid_argument("label is required");
  if (opts.usage.empty())
    throw std::invalid_argument("usage is required (PERSONAL or CERTAUTH)");
  if (opts.password.empty())
    throw std::invalid_argument("password is required (PKCS#12 passphrase)");
  reject_virtual_keyring(opts.ring, "import_certificate");

  a2e_inplace(opts.owner);
  a2e_inplace(opts.ring);
  a2e_inplace(opts.label);
  a2e_inplace(opts.usage);
  a2e_inplace(opts.password);
  if (convert_path)
    a2e_inplace(opts.p12_path);
  // opts.p12_data, when set, is raw PKCS#12 bytes -- never converted.

  ZKR zkr{};
  const int rc = zkr_import_cert(&zkr, opts);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

// Shared validation + conversion + call for export_certificate*. Returns the raw bytes
// zkr_export_cert produced (EBCDIC PEM text, or binary PKCS#12) -- callers decide whether
// to convert (returned bytes) or write them through untouched (file/DSN sinks).
std::string export_cert_raw(const std::string &owner, const std::string &keyring, const std::string &label,
                            const std::string &format, const std::string &password, bool &is_p12_out)
{
  if (label.empty())
    throw std::invalid_argument("label is required");

  const bool is_p12 = (format == "p12");
  if (is_p12 && password.empty())
    throw std::invalid_argument("password is required with format='p12'");
  is_p12_out = is_p12;

  ZKRExportOptions opts;
  opts.owner = owner;
  opts.ring = keyring;
  opts.label = label;
  opts.format = is_p12 ? "p12" : "pem";
  opts.password = password;
  a2e_inplace(opts.owner);
  a2e_inplace(opts.ring);
  a2e_inplace(opts.label);
  a2e_inplace(opts.format);
  a2e_inplace(opts.password);

  ZKR zkr{};
  std::string data;
  const int rc = zkr_export_cert(&zkr, opts, data);
  zkr_raise(zkr, rc);
  return data;
}

} // namespace

std::string create_keyring(const std::string &owner, const std::string &keyring)
{
  reject_virtual_keyring(keyring, "create_keyring");

  std::string o = owner, k = keyring;
  a2e_inplace(o);
  a2e_inplace(k);

  ZKR zkr{};
  const int rc = zkr_new_ring(&zkr, o, k);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

std::string delete_keyring(const std::string &owner, const std::string &keyring)
{
  reject_virtual_keyring(keyring, "delete_keyring");

  std::string o = owner, k = keyring;
  a2e_inplace(o);
  a2e_inplace(k);

  ZKR zkr{};
  const int rc = zkr_del_ring(&zkr, o, k);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

ZkrRingList list_rings(const std::string &owner, const std::string &keyring)
{
  std::string o = owner;
  // list_rings enumerates real key rings; "*" here just means "all rings" (an empty
  // filter), not the virtual key ring, so GetRingInfo does not reject it.
  std::string k = (keyring == "*") ? "" : keyring;
  a2e_inplace(o);
  a2e_inplace(k);

  ZKR zkr{};
  std::vector<ZKRRingEntry> rings;
  const int rc = zkr_list_rings(&zkr, o, k, rings);
  zkr_raise(zkr, rc);

  for (auto &ring : rings)
  {
    e2a_inplace(ring.owner);
    e2a_inplace(ring.name);
    for (auto &cert : ring.certs)
    {
      e2a_inplace(cert.owner);
      e2a_inplace(cert.label);
    }
  }

  ZkrRingList result;
  result.items = std::move(rings);
  result.warning = get_warning(zkr);
  return result;
}

long long count_ring(const std::string &owner, const std::string &keyring)
{
  const bool is_virtual_ring = (keyring == "*");

  std::string o = owner, k = keyring;
  a2e_inplace(o);
  a2e_inplace(k);

  ZKR zkr{};
  long long total = 0;
  if (is_virtual_ring)
  {
    // GetRingInfo does not accept the virtual key ring, so the only way to count all
    // of a user's certificates is the DataGetFirst/GetNext enumeration.
    std::vector<ZKRCertInfo> certs;
    const int rc = zkr_list_ring(&zkr, o, k, certs, 0, nullptr);
    zkr_raise(zkr, rc);
    total = static_cast<long long>(certs.size());
  }
  else
  {
    // GetRingInfo (R_datalib X'0D') returns each ring's Cert_count in one call, so a
    // real ring's count avoids the DataGetFirst/GetNext enumeration loop.
    std::vector<ZKRRingEntry> rings;
    const int rc = zkr_list_rings(&zkr, o, k, rings);
    zkr_raise(zkr, rc);
    for (const auto &ring : rings)
      total += static_cast<long long>(ring.certs.size());
  }
  return total;
}

std::string refresh_digtcert()
{
  ZKR zkr{};
  const int rc = zkr_refresh(&zkr);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

ZkrCertList list_certificates(const std::string &owner, const std::string &keyring, const std::string &label,
                              const std::string &usage, long long max_entries)
{
  if (max_entries < 0)
    throw std::invalid_argument("max_entries must be 0 (unlimited) or a positive number");

  // The filters follow RACDCERT LABEL semantics (exact, case-sensitive, no wildcards),
  // so a match may sit anywhere in the ring. With a filter active, enumerate the whole
  // ring and apply max_entries to the MATCHING rows; without one, cap the enumeration
  // itself (cheaper on enormous rings).
  const bool has_filter = !label.empty() || !usage.empty();

  std::string o = owner, k = keyring, l = label, u = usage;
  a2e_inplace(o);
  a2e_inplace(k);
  a2e_inplace(l);
  a2e_inplace(u);

  ZKR zkr{};
  std::vector<ZKRCertInfo> certs;
  bool more_available = false;
  const int rc = zkr_list_ring(&zkr, o, k, certs, has_filter ? 0 : static_cast<size_t>(max_entries), &more_available);
  zkr_raise(zkr, rc);

  // Filter before converting: label/usage are compared EBCDIC-to-EBCDIC against the
  // survivors of zkr_list_ring, which are still in native encoding at this point.
  std::vector<ZKRCertInfo> shown;
  bool filter_more = false;
  zkr_filter_certs_into(certs, l, u, static_cast<size_t>(max_entries), &filter_more, shown);
  if (has_filter)
    more_available = filter_more;

  for (auto &cert : shown)
  {
    e2a_inplace(cert.label);
    e2a_inplace(cert.owner);
    e2a_inplace(cert.usage);
    e2a_inplace(cert.status);
  }

  ZkrCertList result;
  result.items = std::move(shown);
  result.more_available = more_available;
  return result;
}

ZKRCertDetail show_certificate(const std::string &owner, const std::string &keyring, const std::string &label)
{
  if (label.empty())
    throw std::invalid_argument("label is required");

  std::string o = owner, k = keyring, l = label;
  a2e_inplace(o);
  a2e_inplace(k);
  a2e_inplace(l);

  ZKR zkr{};
  ZKRCertDetail detail;
  const int rc = zkr_show_cert(&zkr, o, k, l, detail);
  zkr_raise(zkr, rc);

  e2a_inplace(detail.label);
  e2a_inplace(detail.owner);
  e2a_inplace(detail.usage);
  e2a_inplace(detail.status);
  e2a_inplace(detail.subject);
  e2a_inplace(detail.record_id);
  e2a_inplace(detail.serial_number);
  e2a_inplace(detail.not_before);
  e2a_inplace(detail.not_after);

  return detail;
}

std::string set_default_certificate(const std::string &owner, const std::string &keyring, const std::string &label)
{
  if (label.empty())
    throw std::invalid_argument("label is required");
  reject_virtual_keyring(keyring, "set_default_certificate");

  std::string o = owner, k = keyring, l = label;
  a2e_inplace(o);
  a2e_inplace(k);
  a2e_inplace(l);

  ZKR zkr{};
  const int rc = zkr_set_default(&zkr, o, k, l);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

std::string connect_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                                const std::string &from_ring, bool from_database, const std::string &usage,
                                bool make_default)
{
  if (label.empty())
    throw std::invalid_argument("label is required");
  reject_virtual_keyring(keyring, "connect_certificate");
  if (from_ring == "*")
    throw std::invalid_argument("use from_database=True (not from_ring='*') to read a certificate from the ESM "
                                "database");
  if (from_database && !from_ring.empty())
    throw std::invalid_argument("specify either from_ring or from_database=True, not both");
  if (!from_database && from_ring.empty())
    throw std::invalid_argument("specify from_ring (a ring the certificate is already on) or from_database=True");

  ZKRConnectOptions opts;
  opts.owner = owner;
  opts.ring = keyring;
  // from_database=True reads the certificate from the owner's virtual key ring ("*").
  opts.from_ring = from_database ? "*" : from_ring;
  opts.label = label;
  opts.usage = usage;
  opts.make_default = make_default;

  a2e_inplace(opts.owner);
  a2e_inplace(opts.ring);
  a2e_inplace(opts.from_ring);
  a2e_inplace(opts.label);
  a2e_inplace(opts.usage);

  ZKR zkr{};
  const int rc = zkr_connect_cert(&zkr, opts);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

std::string delete_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                               bool database, bool skip_refresh)
{
  if (label.empty())
    throw std::invalid_argument("label is required");
  if (keyring == "*")
    throw std::invalid_argument("use database=True (not keyring='*') to delete a certificate from the ESM "
                                "database");
  if (database && !keyring.empty())
    throw std::invalid_argument("specify either keyring or database=True, not both");
  if (!database && keyring.empty())
    throw std::invalid_argument("specify a key ring to disconnect the certificate from, or database=True to "
                                "delete it from the ESM database");

  // database=True maps to the R_datalib virtual key ring ("*"), which removes the
  // certificate from the ESM database rather than a single ring.
  std::string o = owner, r = database ? "*" : keyring, l = label;
  a2e_inplace(o);
  a2e_inplace(r);
  a2e_inplace(l);

  ZKR zkr{};
  const int rc = zkr_del_cert(&zkr, o, r, l, skip_refresh);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

std::string trust_certificate(const std::string &owner, const std::string &label, const std::string &status)
{
  if (label.empty())
    throw std::invalid_argument("label is required");
  if (status.empty())
    throw std::invalid_argument("status is required (TRUST, HIGHTRUST, or NOTRUST)");

  ZKRAlterOptions opts;
  opts.owner = owner;
  opts.label = label;
  opts.status = status;
  a2e_inplace(opts.owner);
  a2e_inplace(opts.label);
  a2e_inplace(opts.status);

  ZKR zkr{};
  const int rc = zkr_alter_cert(&zkr, opts);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

std::string rename_certificate(const std::string &owner, const std::string &label, const std::string &new_label)
{
  if (label.empty())
    throw std::invalid_argument("label is required");
  if (new_label.empty())
    throw std::invalid_argument("new_label is required");

  ZKRAlterOptions opts;
  opts.owner = owner;
  opts.label = label;
  opts.new_label = new_label;
  a2e_inplace(opts.owner);
  a2e_inplace(opts.label);
  a2e_inplace(opts.new_label);

  ZKR zkr{};
  const int rc = zkr_alter_cert(&zkr, opts);
  zkr_raise(zkr, rc);
  return get_warning(zkr);
}

ZkrBytes export_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                            const std::string &format, const std::string &password)
{
  bool is_p12 = false;
  std::string data = export_cert_raw(owner, keyring, label, format, password, is_p12);
  // PEM: convert to portable ASCII for the value returned to Python (D11). PKCS#12 is
  // binary and passes through untouched.
  if (!is_p12)
    e2a_inplace(data);
  return data;
}

long long export_certificate_to_file(const std::string &owner, const std::string &keyring, const std::string &label,
                                     const std::string &file, const std::string &format,
                                     const std::string &password)
{
  bool is_p12 = false;
  std::string data = export_cert_raw(owner, keyring, label, format, password, is_p12);

  std::string path = file;
  a2e_inplace(path);
  std::string err;
  // Written before converting: the file keeps the exact bytes zkr_export_cert produced,
  // EBCDIC PEM text or binary PKCS#12 -- byte-identical to keyring-util (D11).
  if (zkrio_write_file(path, data, err) != 0)
  {
    e2a_inplace(err);
    throw std::runtime_error("could not write output file: " + file + " (" + err + ")");
  }
  return static_cast<long long>(data.size());
}

long long export_certificate_to_dsn(const std::string &owner, const std::string &keyring, const std::string &label,
                                    const std::string &dsn, const std::string &format,
                                    const std::string &password)
{
  bool is_p12 = false;
  std::string data = export_cert_raw(owner, keyring, label, format, password, is_p12);

  std::string d = dsn;
  a2e_inplace(d);
  std::string err;
  if (zkrio_write_dsn(d, data, is_p12, err) != 0)
  {
    e2a_inplace(err);
    throw std::runtime_error("could not write output data set: " + dsn + " (" + err + ")");
  }
  return static_cast<long long>(data.size());
}

std::string import_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                               const std::string &usage, const std::string &password, const ZkrBytes &data,
                               bool skip_refresh)
{
  ZKRImportOptions opts;
  opts.owner = owner;
  opts.ring = keyring;
  opts.label = label;
  opts.usage = usage;
  opts.password = password;
  opts.p12_data = data; // raw PKCS#12 bytes, never converted
  opts.skip_refresh = skip_refresh;
  return do_import(opts, /*convert_path=*/false);
}

std::string import_certificate_from_file(const std::string &owner, const std::string &keyring,
                                         const std::string &label, const std::string &usage,
                                         const std::string &password, const std::string &file, bool skip_refresh)
{
  ZKRImportOptions opts;
  opts.owner = owner;
  opts.ring = keyring;
  opts.label = label;
  opts.usage = usage;
  opts.password = password;
  opts.p12_path = file; // zkr_import_cert opens and reads this itself, in binary mode
  opts.skip_refresh = skip_refresh;
  return do_import(opts, /*convert_path=*/true);
}

std::string import_certificate_from_dsn(const std::string &owner, const std::string &keyring,
                                        const std::string &label, const std::string &usage,
                                        const std::string &password, const std::string &dsn, bool skip_refresh)
{
  std::string d = dsn;
  a2e_inplace(d);
  std::string raw, err;
  if (zkrio_read_dsn(d, raw, err) != 0)
  {
    e2a_inplace(err);
    throw std::runtime_error("could not read source data set: " + dsn + " (" + err + ")");
  }

  ZKRImportOptions opts;
  opts.owner = owner;
  opts.ring = keyring;
  opts.label = label;
  opts.usage = usage;
  opts.password = password;
  opts.p12_data = raw; // raw PKCS#12 bytes read from the data set, never converted
  opts.skip_refresh = skip_refresh;
  return do_import(opts, /*convert_path=*/false);
}
