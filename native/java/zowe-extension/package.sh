#!/bin/sh
#
# This program and the accompanying materials are made available under the terms of the
# Eclipse Public License v2.0 which accompanies this distribution, and is available at
# https://www.eclipse.org/legal/epl-v20.html
#
# SPDX-License-Identifier: EPL-2.0
#
# Copyright Contributors to the Zowe Project.
#
# Assembles the zo-rest Zowe extension pax on z/OS (run from this directory after the
# native/java bindings and secur libraries are built). Text files are expected in EBCDIC
# (as uploaded by scripts/buildTools.ts) and are tagged IBM-1047; binaries are tagged binary.
#
# usage: package.sh <zo-java-app.jar> <libzo_java.so> <libzowe-commons-secur.so> <out.pax> [version] [commit]
set -e
[ $# -ge 4 ] || { echo "usage: $0 <jar> <libzo_java.so> <libzowe-commons-secur.so> <out.pax> [version] [commit]" >&2; exit 1; }
version="${5:-0.0.0-dev}"
commit="${6:-unknown}"

here=$(cd "$(dirname "$0")" && pwd)
stage="${here}/.stage.$$"
# Free space from earlier runs first; the jar and libraries are large
rm -rf "$4" "${here}"/.stage.*
trap 'rm -rf "${stage}"' EXIT
mkdir -p "${stage}/bin" "${stage}/config" "${stage}/schemas" "${stage}/lib"

sed "s/^version: .*/version: ${version}/" "${here}/manifest.yaml" > "${stage}/manifest.yaml"
# Build identity read by bin/start.sh (registration metadata, /zosmf/info)
printf 'version=%s\ncommit=%s\nbuilt=%s\n' "${version}" "${commit}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "${stage}/BUILD"
cp "${here}"/bin/*.sh "${stage}/bin/"
cp "${here}"/config/*.yml "${stage}/config/"
cp "${here}"/schemas/*.json "${stage}/schemas/"
# Hard links avoid duplicating the large binaries when staging on the same file system
link_or_copy() { ln "$1" "$2" 2>/dev/null || cp "$1" "$2"; }
link_or_copy "$1" "${stage}/lib/zo-java-app.jar"
link_or_copy "$2" "${stage}/lib/libzo_java.so"
link_or_copy "$3" "${stage}/lib/libzowe-commons-secur.so"

# Shell scripts and the manifest are read by z/OS tools (EBCDIC); files read by the JVM
# (Spring config) must be UTF-8, so convert and retag them after the EBCDIC pass.
find "${stage}" -type f ! -name '*.jar' ! -name '*.so' -exec chtag -tc IBM-1047 {} \;
for yml in "${stage}"/config/*.yml; do
  iconv -f IBM-1047 -t UTF-8 "${yml}" > "${yml}.utf8" && mv "${yml}.utf8" "${yml}"
  chtag -tc UTF-8 "${yml}"
done
chtag -b "${stage}"/lib/*
chmod 755 "${stage}"/bin/*.sh "${stage}"/lib/*.so
extattr +p "${stage}"/lib/*.so 2>/dev/null || echo "note: extattr +p not permitted here; bin/install.sh sets it at install time"

out=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
(cd "${stage}" && pax -wf "${out}" -x pax -o saveext .)
echo "Created ${out}"
