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

#ifndef ZKR_PY_HPP
#define ZKR_PY_HPP

#include <string>
#include <stdexcept>
#include <vector>
#include "../../c/zkr.hpp"
#include "conversion.hpp"

// PKCS#12/PEM payloads. Typemapped to/from Python `bytes` in zkr_py.i -- std_string.i's
// default `str` typemap would corrupt a PKCS#12 blob, which is not valid UTF-8.
typedef std::string ZkrBytes;

// Out-param siblings of the native result vectors (D10): zkr_list_rings returns rings
// with no cap, and list-with-a-warning is a shape the native structs don't carry alone.
struct ZkrCertList
{
  std::vector<ZKRCertInfo> items;
  bool more_available;
};

struct ZkrRingList
{
  std::vector<ZKRRingEntry> items;
  std::string warning;
};

// Raised on any non-zero SAF/ESM/GSK return, carrying the same structured codes the RPC
// layer exposes programmatically (certificates.cpp's `safReturns`).
struct ZkrError : std::runtime_error
{
  int function_code;
  int saf_rc;
  int esm_rc;
  int esm_rsn;
  int gsk_rc;
  std::string service;

  ZkrError(const std::string &message, int function_code_, int saf_rc_, int esm_rc_, int esm_rsn_,
          int gsk_rc_, std::string service_)
      : std::runtime_error(message), function_code(function_code_), saf_rc(saf_rc_), esm_rc(esm_rc_),
        esm_rsn(esm_rsn_), gsk_rc(gsk_rc_), service(std::move(service_))
  {
  }
};

// Key rings
std::string create_keyring(const std::string &owner, const std::string &keyring);
std::string delete_keyring(const std::string &owner, const std::string &keyring);
ZkrRingList list_rings(const std::string &owner, const std::string &keyring = "");
long long count_ring(const std::string &owner, const std::string &keyring);
std::string refresh_digtcert();

// Certificates in a ring
ZkrCertList list_certificates(const std::string &owner, const std::string &keyring,
                              const std::string &label = "", const std::string &usage = "",
                              long long max_entries = 10);
ZKRCertDetail show_certificate(const std::string &owner, const std::string &keyring, const std::string &label);
std::string set_default_certificate(const std::string &owner, const std::string &keyring, const std::string &label);
std::string connect_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                                const std::string &from_ring = "", bool from_database = false,
                                const std::string &usage = "", bool make_default = false);
std::string delete_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                               bool database = false, bool skip_refresh = false);

// Certificate records (DataAlter; no ring involved)
std::string trust_certificate(const std::string &owner, const std::string &label, const std::string &status);
std::string rename_certificate(const std::string &owner, const std::string &label, const std::string &new_label);

// Export -- bytes, or straight to a sink. Returned `bytes` are ISO8859-1 for PEM; bytes
// written to a file or data set stay EBCDIC, byte-identical to keyring-util (D11).
ZkrBytes export_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                            const std::string &format = "pem", const std::string &password = "");
long long export_certificate_to_file(const std::string &owner, const std::string &keyring, const std::string &label,
                                     const std::string &file, const std::string &format = "pem",
                                     const std::string &password = "");
long long export_certificate_to_dsn(const std::string &owner, const std::string &keyring, const std::string &label,
                                    const std::string &dsn, const std::string &format = "pem",
                                    const std::string &password = "");

// Import -- from bytes, or from a source
std::string import_certificate(const std::string &owner, const std::string &keyring, const std::string &label,
                               const std::string &usage, const std::string &password, const ZkrBytes &data,
                               bool skip_refresh = false);
std::string import_certificate_from_file(const std::string &owner, const std::string &keyring,
                                         const std::string &label, const std::string &usage,
                                         const std::string &password, const std::string &file,
                                         bool skip_refresh = false);
std::string import_certificate_from_dsn(const std::string &owner, const std::string &keyring,
                                        const std::string &label, const std::string &usage,
                                        const std::string &password, const std::string &dsn,
                                        bool skip_refresh = false);

#endif
