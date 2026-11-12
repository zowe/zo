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
# Runs once during `zwe components install`. Thread-level SAF identity switching requires
# every module loaded into the address space to be program controlled, so mark the native
# libraries (needs READ on FACILITY BPX.FILEATTR.PROGCTL; otherwise ask a security admin to
# run the same command).

COMPONENT_HOME="$(pwd)"
for lib in "${COMPONENT_HOME}"/lib/*.so; do
  if ! extattr +p "${lib}"; then
    echo "ZOREST002W Could not set program-controlled attribute on ${lib}; run: extattr +p ${lib}" >&2
  fi
done
