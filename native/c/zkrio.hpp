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

#ifndef ZKRIO_HPP
#define ZKRIO_HPP

#include <string>

// zkrio -- data-set and file I/O for certificate material (PKCS#12 / PEM bytes).
// zkr.hpp is deliberately free of data-set knowledge (see ZKRImportOptions.p12_data);
// this is the shared helper both commands/certificates.cpp and the zkr_py bindings
// use to read/write that material from/to a data set or a private file, so the
// RACDCERT-parity attributes (PS/VB/84/27998) live in exactly one place.

// The bindings compile this header EBCDIC and their SWIG wrappers ASCII. libc++ uses a distinct
// inline namespace per char mode (std::__1 vs std::__1_a), so a mangled name is unresolvable
// across that boundary -- everything the bindings call needs C linkage.
#ifdef SWIG
extern "C"
{
#endif

/**
 * @brief Read a PKCS#12/PEM blob out of a sequential data set or PDS/E member, in
 *        binary mode (no code-page conversion, no RDW stripping) -- byte-identical
 *        to `cp -B "//'DSN'"`.
 * @return 0 on success; non-zero otherwise (details in err)
 */
int zkrio_read_dsn(const std::string &dsn, std::string &data, std::string &err);

/**
 * @brief Write exported certificate bytes to a data set, creating it (RACDCERT
 *        FORMAT(PKCS12DER) parity: PS/VB/LRECL(84)/BLKSIZE(27998), or PDS/E member
 *        when the dsn names one) when it does not already exist.
 *        is_binary=true  (p12): raw bytes, chunked into V-format records.
 *        is_binary=false (pem): EBCDIC PEM text, one line per record.
 * @return 0 on success; non-zero otherwise (details in err)
 */
int zkrio_write_dsn(const std::string &dsn, const std::string &data, bool is_binary, std::string &err);

/**
 * @brief Write bytes to a file, created (or truncated) owner-read/write only,
 *        independent of the process umask -- for certificate material that may
 *        contain a private key.
 * @return 0 on success; non-zero otherwise (details in err)
 */
int zkrio_write_file(const std::string &path, const std::string &data, std::string &err);

#ifdef SWIG
}
#endif

#endif // ZKRIO_HPP
