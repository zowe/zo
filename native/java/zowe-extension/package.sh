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
# usage: package.sh <zo-java-app.jar> <libzo_java.so> <libzowe-commons-secur.so> <out.pax>
set -e
[ $# -eq 4 ] || { echo "usage: $0 <jar> <libzo_java.so> <libzowe-commons-secur.so> <out.pax>" >&2; exit 1; }

here=$(cd "$(dirname "$0")" && pwd)
stage="${here}/.stage.$$"
trap 'rm -rf "${stage}"' EXIT
mkdir -p "${stage}/bin" "${stage}/config" "${stage}/schemas" "${stage}/lib"

cp "${here}/manifest.yaml" "${stage}/"
cp "${here}"/bin/*.sh "${stage}/bin/"
cp "${here}"/config/*.yml "${stage}/config/"
cp "${here}"/schemas/*.json "${stage}/schemas/"
cp "$1" "${stage}/lib/zo-java-app.jar"
cp "$2" "${stage}/lib/libzo_java.so"
cp "$3" "${stage}/lib/libzowe-commons-secur.so"

# Shell scripts and the manifest are read by z/OS tools (EBCDIC); files read by the JVM
# (Spring config) must be UTF-8.
for yml in "${stage}"/config/*.yml; do
  iconv -f IBM-1047 -t UTF-8 "${yml}" > "${yml}.utf8" && mv "${yml}.utf8" "${yml}"
  chtag -tc UTF-8 "${yml}"
done
find "${stage}" -type f ! -name '*.jar' ! -name '*.so' ! -path '*/config/*.yml' -exec chtag -tc IBM-1047 {} \;
chtag -b "${stage}"/lib/*
chmod 755 "${stage}"/bin/*.sh "${stage}"/lib/*.so
extattr +p "${stage}"/lib/*.so 2>/dev/null || echo "note: extattr +p not permitted here; bin/install.sh sets it at install time"

out=$(cd "$(dirname "$4")" && pwd)/$(basename "$4")
(cd "${stage}" && pax -wf "${out}" -x pax -o saveext .)
echo "Created ${out}"
