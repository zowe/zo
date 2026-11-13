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
# Zowe launcher entry point for the zo REST API. Started by ZWESLSTC/zwe with the
# component directory as the working directory and ZWE_* variables describing the instance.

# Zowe sources this script, so $0 is not reliable; locate the component by its manifest.
COMPONENT_HOME=""
for candidate in "${ZWE_zowe_extensionDirectory}/zo-rest" "${ZWE_zowe_runtimeDirectory}/components/zo-rest" \
  "$(pwd)" "${LAUNCH_COMPONENT}" "${LAUNCH_COMPONENT}/.."; do
  if [ -f "${candidate}/manifest.yaml" ] && [ -f "${candidate}/lib/zo-java-app.jar" ]; then
    COMPONENT_HOME=$(cd "${candidate}" && pwd)
    break
  fi
done

fail() {
  echo "ZOREST001E $*" >&2
  exit 1
}

[ -n "${COMPONENT_HOME}" ] || fail "Cannot locate the zo-rest component directory (looked in the Zowe extension and runtime directories)."

# --- Java 25+ (FFM API) -----------------------------------------------------
ZO_JAVA_HOME="${ZWE_configs_javaHome:-${JAVA_HOME}}"
[ -x "${ZO_JAVA_HOME}/bin/java" ] || fail "Java not found. Set components.zo-rest.javaHome to a Java 25+ home."
java_major=$("${ZO_JAVA_HOME}/bin/java" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1)
[ -n "${java_major}" ] && [ "${java_major}" -ge 25 ] || \
  fail "Java ${java_major:-unknown} at ${ZO_JAVA_HOME} is too old; Java 25+ is required (components.zo-rest.javaHome)."

# --- TLS material from zowe.certificate --------------------------------------
export ZO_KEYSTORE="${ZWE_zowe_certificate_keystore_file}"
export ZO_KEYSTORE_TYPE="${ZWE_zowe_certificate_keystore_type:-PKCS12}"
export ZO_KEYSTORE_PASSWORD="${ZWE_zowe_certificate_keystore_password}"
export ZO_KEY_PASSWORD="${ZWE_zowe_certificate_keystore_password}"
export ZO_KEY_ALIAS="${ZWE_zowe_certificate_keystore_alias:-localhost}"
export ZO_TRUSTSTORE="${ZWE_zowe_certificate_truststore_file}"
export ZO_TRUSTSTORE_TYPE="${ZWE_zowe_certificate_truststore_type:-PKCS12}"
export ZO_TRUSTSTORE_PASSWORD="${ZWE_zowe_certificate_truststore_password}"
[ -n "${ZO_KEYSTORE}" ] && [ -n "${ZO_TRUSTSTORE}" ] || fail "zowe.certificate keystore/truststore are not configured."

# SAF keyrings: Spring/JSSE expect safkeyring:// URLs. Semeru 17+ ships the safkeyring URL
# handler per keystore provider (the Java 8 com.ibm.crypto.provider handler no longer exists).
case "${ZO_KEYSTORE_TYPE}" in
  JCECCARACFKS) ZO_PROTOCOL_HANDLER_PKGS="com.ibm.crypto.hdwrCCA.provider" ;;
  JCEHYBRIDRACFKS) ZO_PROTOCOL_HANDLER_PKGS="com.ibm.crypto.ibmjcehybrid.provider" ;;
  *) ZO_PROTOCOL_HANDLER_PKGS="com.ibm.crypto.zsecurity.provider" ;;
esac
case "${ZO_KEYSTORE_TYPE}" in
  JCE*RACFKS)
    ZO_KEYSTORE=$(echo "${ZO_KEYSTORE}" | sed 's#^safkeyring:/*#safkeyring://#')
    ZO_TRUSTSTORE=$(echo "${ZO_TRUSTSTORE}" | sed 's#^safkeyring:/*#safkeyring://#')
    ZO_KEYSTORE_PASSWORD="${ZO_KEYSTORE_PASSWORD:-password}"
    ZO_KEY_PASSWORD="${ZO_KEY_PASSWORD:-password}"
    ZO_TRUSTSTORE_PASSWORD="${ZO_TRUSTSTORE_PASSWORD:-password}"
    ;;
esac

case "${ZWE_zowe_verifyCertificates:-STRICT}" in
  DISABLED) export ZO_VERIFY_CERTIFICATES=false ZO_NONSTRICT_VERIFY_CERTIFICATES=false ;;
  NONSTRICT) export ZO_VERIFY_CERTIFICATES=true ZO_NONSTRICT_VERIFY_CERTIFICATES=true ;;
  *) export ZO_VERIFY_CERTIFICATES=true ZO_NONSTRICT_VERIFY_CERTIFICATES=false ;;
esac

# --- Network / APIML -----------------------------------------------------------
export ZO_PORT="${ZWE_configs_port:-27610}"
export ZO_HOSTNAME="${ZWE_haInstance_hostname:-${ZWE_zowe_externalDomains_0:-localhost}}"
export ZO_LISTEN_ADDRESS="${ZWE_configs_zowe_network_server_listenAddresses_0:-${ZWE_zowe_network_server_listenAddresses_0:-0.0.0.0}}"
export ZO_GATEWAY_PORT="${ZWE_zowe_externalPort:-${ZWE_components_gateway_port:-7554}}"
if [ -n "${ZWE_DISCOVERY_SERVICES_LIST}" ]; then
  export ZO_DISCOVERY_URLS="${ZWE_DISCOVERY_SERVICES_LIST}"
else
  discovery_port="${ZWE_components_discovery_port:-${ZWE_components_apiml_discovery_port:-7553}}"
  export ZO_DISCOVERY_URLS="https://${ZWE_zowe_externalDomains_0:-${ZO_HOSTNAME}}:${discovery_port}/eureka/"
fi

# --- Application settings -------------------------------------------------------
export ZO_LOG_LEVEL="${ZWE_configs_logging_level:-INFO}"
export ZO_NATIVE_LOG_LEVEL="${ZWE_configs_logging_nativeLevel:-WARN}"
export ZO_REQUIRE_CSRF_HEADER="${ZWE_configs_security_requireCsrfHeader:-true}"
# Evaluation only: exposes commands that spawn child processes (see APIML-REGISTRATION.md)
if [ "${ZWE_configs_experimental_spawningCommands}" = "true" ]; then
  export ZO_EXPERIMENTAL_SPAWNING=true
  export ZO_EXPERIMENTAL_METHODS="unixCommand,tsoCommand"
fi
profiles="zos"
[ "${ZWE_configs_debug}" = "true" ] && profiles="${profiles},debug"

LIB_DIR="${COMPONENT_HOME}/lib"
export LIBPATH="${LIB_DIR}:${LIBPATH}"

echo "ZOREST000I Starting zo REST API on ${ZO_HOSTNAME}:${ZO_PORT} (java ${java_major}, discovery ${ZO_DISCOVERY_URLS})"

_BPXK_AUTOCVT=OFF
export _BPX_JOBNAME="${ZWE_zowe_job_prefix:-ZWE}ZO"
exec "${ZO_JAVA_HOME}/bin/java" \
  -Xms${ZWE_configs_heap_init:-64}m -Xmx${ZWE_configs_heap_max:-512}m \
  -XX:+ExitOnOutOfMemoryError \
  --enable-native-access=ALL-UNNAMED \
  -Dfile.encoding=UTF-8 \
  -Djava.io.tmpdir="${TMPDIR:-/tmp}" \
  -Djava.library.path="${LIB_DIR}:${LIBPATH}" \
  -Djava.protocol.handler.pkgs="${ZO_PROTOCOL_HANDLER_PKGS}" \
  -Djava.net.preferIPv4Stack=true \
  -Dibm.serversocket.recover=true \
  -jar "${COMPONENT_HOME}/lib/zo-java-app.jar" \
  --spring.config.location="optional:classpath:/,file:${COMPONENT_HOME}/config/application.yml" \
  --spring.profiles.active="${profiles}"
