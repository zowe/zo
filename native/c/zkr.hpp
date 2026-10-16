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

#ifndef ZKR_HPP
#define ZKR_HPP

#include <string>
#include <vector>

// zkr -- ESM (RACF or equivalent) key ring / certificate services built on the
// R_datalib (IRRSDL64) SAF callable service and the System SSL (GSKCMS)
// certificate APIs. This mirrors the service-layer pattern used by zds/zjb/zcn:
// plain result structs plus a ZKR handle carrying diagnostics; the command layer
// (commands/certificates.cpp) formats the results.

// SAF/ESM (and, where relevant, GSK) diagnostic details for the last call.
struct ZKRDiag
{
  int function_code;   // R_datalib function code that failed (0 when GSK failed)
  int saf_rc;          // SAF return code (R_datalib return_code)
  int esm_rc;          // ESM (e.g. RACF) return code
  int esm_rsn;         // ESM (e.g. RACF) reason code
  int gsk_rc;          // System SSL / GSKCMS status (0 when not applicable)
  std::string service; // human name of the failing service, e.g. "IRRSDL64 DELCERT"
  std::string e_msg;   // human-readable message (populated on failure)
  std::string warning; // human-readable note for a non-fatal SAF warning (rc == 4)

  ZKRDiag() : function_code(0), saf_rc(0), esm_rc(0), esm_rsn(0), gsk_rc(0)
  {
  }
};

// Service handle. Carries diagnostics for the most recent operation.
struct ZKR
{
  ZKRDiag diag;
};

// A single certificate connected to a key ring, as surfaced by LISTRING.
struct ZKRCertInfo
{
  std::string label;
  std::string owner;
  std::string usage;  // PERSONAL | CERTAUTH | OTHER
  std::string status; // TRUST | HIGHTRUST | NOTRUST | UNKNOWN
  bool is_default;

  ZKRCertInfo() : is_default(false)
  {
  }
};

// Detailed view of a single certificate (zkr_show_cert). The label/owner/usage/
// status/default and subject DN come straight from R_datalib DataGetFirst; the
// serial number and validity dates come from decoding the DER with System SSL.
struct ZKRCertDetail
{
  std::string label;
  std::string owner;
  std::string usage;
  std::string status;
  bool is_default;
  std::string subject;       // subject distinguished name (from R_datalib)
  std::string record_id;     // ESM record ID, hex-encoded (from R_datalib)
  int key_type;              // private-key type code (R_datalib), -1 if absent
  int key_size;              // private-key bit size (R_datalib), 0 if absent
  std::string serial_number; // certificate serial number, hex (from GSK decode)
  std::string not_before;    // validity start, ISO-8601 (from GSK decode)
  std::string not_after;     // validity end, ISO-8601 (from GSK decode)
  bool has_validity;         // true when the GSK decode produced validity dates

  ZKRCertDetail() : is_default(false), key_type(-1), key_size(0), has_validity(false)
  {
  }
};

// A certificate connected to a ring, as returned by GetRingInfo.
struct ZKRRingCert
{
  std::string owner;
  std::string label;
};

// A key ring and the certificates connected to it (zkr_list_rings).
struct ZKRRingEntry
{
  std::string owner;
  std::string name;
  std::vector<ZKRRingCert> certs;
};

// Options for connecting a certificate to a key ring. R_datalib DataPut requires
// the certificate's bytes, so the certificate is read from `from_ring` -- a ring
// it is already connected to, or the owner's virtual key ring "*" (which also
// covers certificates not connected to any ring) -- and connected to `ring`.
struct ZKRConnectOptions
{
  std::string owner;     // certificate owner (userid), or CERTAUTH/SITE pseudo-ids
  std::string ring;      // target key ring name
  std::string from_ring; // source ring to read the bytes from; "*" = virtual key ring (--from-database)
  std::string label;     // certificate label
  std::string usage;     // PERSONAL | CERTAUTH; empty = keep the certificate's current usage
  bool make_default;     // set this certificate as the target ring default

  ZKRConnectOptions() : make_default(false)
  {
  }
};

// Options for DataAlter: change a certificate's status and/or label.
struct ZKRAlterOptions
{
  std::string owner;     // certificate owner (userid)
  std::string label;     // current certificate label
  std::string new_label; // empty = do not rename
  std::string status;    // "TRUST" | "HIGHTRUST" | "NOTRUST"; empty = leave unchanged
};

// Options for exporting a certificate from a key ring.
struct ZKRExportOptions
{
  std::string owner;    // key ring owner (userid)
  std::string ring;     // key ring name
  std::string label;    // certificate label
  std::string format;   // "pem" (certificate) or "p12" (certificate + private key)
  std::string password; // PKCS#12 passphrase (used only when format == "p12")
};

// Options for importing a certificate into a key ring from a PKCS#12 file.
struct ZKRImportOptions
{
  std::string owner;         // key ring owner (userid)
  std::string ring;          // key ring name
  std::string label;         // certificate label to assign
  std::string usage;         // PERSONAL | CERTAUTH
  std::string p12_path;      // path to the source PKCS#12 file
  std::string p12_data;      // pre-loaded PKCS#12 bytes; when non-empty, used instead of p12_path
  std::string password;      // PKCS#12 passphrase
  bool skip_refresh = false; // do not auto-refresh DIGTCERT when the ESM signals it is required
};

// The bindings compile this header EBCDIC and their SWIG wrappers ASCII. libc++ uses a distinct
// inline namespace per char mode (std::__1 vs std::__1_a), so a mangled name is unresolvable
// across that boundary -- everything the bindings call needs C linkage.
#ifdef SWIG
extern "C"
{
#endif

/**
 * @brief Create a new key ring (R_datalib NEWRING).
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_new_ring(ZKR *zkr, const std::string &owner, const std::string &ring);

/**
 * @brief Delete a key ring (R_datalib DELRING).
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_del_ring(ZKR *zkr, const std::string &owner, const std::string &ring);

/**
 * @brief Refresh the DIGTCERT class (R_datalib REFRESH).
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_refresh(ZKR *zkr);

/**
 * @brief Remove/disconnect a certificate from a key ring or the ESM database
 *        (R_datalib DELCERT). A ring of "*" targets the ESM database.
 *        When the ESM signals the DIGTCERT class must be refreshed for the change
 *        to take effect (4/4/12), this automatically issues REFRESH unless
 *        skip_refresh is set (in which case a note is left in zkr->diag.warning).
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_del_cert(ZKR *zkr, const std::string &owner, const std::string &ring,
                 const std::string &label, bool skip_refresh = false);

/**
 * @brief Enumerate the certificates connected to a key ring
 *        (R_datalib GETCERT/GETNEXT loop, closed with DATA_ABORT).
 * @param certs populated list of certificate summaries
 * @param max_entries stop after this many entries (0 = unlimited)
 * @param more_available if non-null, set to true when the ring holds more
 *        certificates than were returned because max_entries was reached
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_list_ring(ZKR *zkr, const std::string &owner, const std::string &ring,
                  std::vector<ZKRCertInfo> &certs, size_t max_entries = 0,
                  bool *more_available = nullptr);

/**
 * @brief Filter a certificate list the way RACDCERT treats the LABEL keyword:
 *        exact, case-sensitive comparison, no wildcards or generics. usage is
 *        matched the same way against the PERSONAL/CERTAUTH/OTHER strings
 *        produced by zkr_list_ring. An empty label/usage means "no filter".
 * @param certs matching entries are appended here, in enumeration order
 * @param max_entries cap on the number of MATCHING entries returned (0 = all)
 * @param more_available if non-null, set to true when the cap cut off further
 *        matching entries
 */
void zkr_filter_certs_into(const std::vector<ZKRCertInfo> &certs, const std::string &label,
                           const std::string &usage, size_t max_entries, bool *more_available,
                           std::vector<ZKRCertInfo> &out);

#ifdef SWIG
}
#endif

/**
 * @brief Filter a certificate list the way RACDCERT treats the LABEL keyword:
 *        exact, case-sensitive comparison, no wildcards or generics. usage is
 *        matched the same way against the PERSONAL/CERTAUTH/OTHER strings
 *        produced by zkr_list_ring. An empty label/usage means "no filter".
 *        Thin by-value wrapper around zkr_filter_certs_into -- kept outside the
 *        extern "C" block below because C linkage cannot express a std::vector
 *        return by value; the bindings call zkr_filter_certs_into directly.
 * @param max_entries cap on the number of MATCHING entries returned (0 = all)
 * @param more_available if non-null, set to true when the cap cut off further
 *        matching entries
 * @return the matching entries, in enumeration order
 */
std::vector<ZKRCertInfo> zkr_filter_certs(const std::vector<ZKRCertInfo> &certs,
                                          const std::string &label,
                                          const std::string &usage,
                                          size_t max_entries = 0,
                                          bool *more_available = nullptr);

#ifdef SWIG
extern "C"
{
#endif

/**
 * @brief Export a certificate from a key ring.
 *        format "pem" returns PEM text; format "p12" returns raw PKCS#12 bytes.
 * @param data receives the exported certificate bytes
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_export_cert(ZKR *zkr, const ZKRExportOptions &opts, std::string &data);

/**
 * @brief Import a certificate (and private key, when present) from a PKCS#12
 *        file into a key ring (R_datalib IMPORT). The PKCS#12 bytes are read
 *        from opts.p12_path, unless opts.p12_data is non-empty, in which case
 *        those bytes are used directly and no file is touched -- this keeps
 *        zkr free of data-set knowledge while letting a caller (e.g. the
 *        `--dsn` data-set source) supply bytes it read itself.
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_import_cert(ZKR *zkr, const ZKRImportOptions &opts);

/**
 * @brief Retrieve detailed information for a single certificate in a key ring:
 *        R_datalib fields (label/owner/usage/status/default, subject DN, record
 *        ID, key type/size) plus GSK-decoded serial number and validity dates.
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_show_cert(ZKR *zkr, const std::string &owner, const std::string &ring,
                  const std::string &label, ZKRCertDetail &detail);

/**
 * @brief Enumerate a user's key rings and the certificates connected to each
 *        (R_datalib GetRingInfo, X'0D'). An empty ring returns all of the
 *        owner's rings; a specific ring narrows the result.
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_list_rings(ZKR *zkr, const std::string &owner, const std::string &ring,
                   std::vector<ZKRRingEntry> &rings);

/**
 * @brief Connect a certificate that already exists in the ESM database to a key
 *        ring with the given usage and optional default flag. The certificate's
 *        bytes are read from opts.from_ring (a real ring or the virtual ring
 *        "*") and re-put via R_datalib DataPut.
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_connect_cert(ZKR *zkr, const ZKRConnectOptions &opts);

/**
 * @brief Make a certificate the default certificate of a key ring (R_datalib
 *        DataPut re-connect with the default flag). The certificate's current
 *        usage is looked up and preserved.
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_set_default(ZKR *zkr, const std::string &owner, const std::string &ring, const std::string &label);

/**
 * @brief Change a certificate's trust status and/or label (R_datalib DataAlter,
 *        X'0C'). Ring is not required (DataAlter operates on the ESM database
 *        record; Ring_name and RACF_user_ID are ignored by the service).
 * @return 0 on success; non-zero otherwise (details in zkr->diag)
 */
int zkr_alter_cert(ZKR *zkr, const ZKRAlterOptions &opts);

#ifdef SWIG
}
#endif

#endif // ZKR_HPP
