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
# Runs zo-rest outside the Zowe launcher (e.g. from a user's home directory). The env file
# supplies the ZWE_* values the launcher normally exports, plus optional ZO_* overrides such as
# ZO_APIML_ENABLED=false for a private, unregistered instance. Also the entry point of the
# ZORSTC started task; see README section 11, "Running as a separate started task".
#
# usage: bin/run-standalone.sh [env-file]   (default: ./standalone.env)

cd "$(dirname "$0")/.." || exit 1
env_file="${1:-standalone.env}"
# Relative paths are relative to the component root; absolute paths may live anywhere
case "${env_file}" in
  /*) ;;
  *) env_file="./${env_file#./}" ;;
esac
[ -r "${env_file}" ] || { echo "ZOREST003E env file not readable: ${env_file}" >&2; exit 1; }

# The env file holds keystore passwords: refuse it if group/other can read it
case "$(ls -l "${env_file}" | cut -c5-10)" in
  ------) ;;
  *) echo "ZOREST004E ${env_file} must not be readable by group or other (chmod 600)" >&2; exit 1 ;;
esac

[ -n "${_BPXK_AUTOCVT}" ] || export _BPXK_AUTOCVT=ON
# Started tasks (BPXBATSL) get no PATH, and /bin/sh then uses a default (/bin:.) that it does not
# export, so the JVM would have none and zo could not find tsocmd. Always export one, without "."
zo_path=$(echo ":${PATH}:" | sed -e 's/:\.:/:/g' -e 's/:\.:/:/g' -e 's/::*/:/g' -e 's/^://' -e 's/:$//')
case ":${zo_path}:" in *:/bin:*) ;; *) zo_path="${zo_path:+${zo_path}:}/bin:/usr/sbin" ;; esac
export PATH="${zo_path}"
set -a
. "${env_file}"
set +a
export ZWE_zowe_extensionDirectory="${ZWE_zowe_extensionDirectory:-$(cd .. && pwd)}"
exec sh bin/start.sh
