#!/bin/sh
#
#  This program and the accompanying materials are
#  made available under the terms of the Eclipse Public License v2.0
#  which accompanies this distribution, and is available at
#  https://www.eclipse.org/legal/epl-v20.html
#
#  SPDX-License-Identifier: EPL-2.0
#
#  Copyright Contributors to the Zowe Project.
#
# List the external symbols zo/zoa import from DLLs, for inspection.
#
# This REPORTS, it does not gate. What keeps the binary loadable on the floor release is the bind
# itself: point LDFLAGS at side decks captured from a system at that release and an unavailable
# symbol becomes an unresolved external at bind time. See compat/README.md.
#
# Use this report to answer "what does the binary actually depend on?" -- when raising the floor,
# changing the compiler level, or investigating a CEE3561S.
#
#   sh tools/report_runtime_imports.sh [level]     # writes build-out/runtime-imports.txt
#
# Run from native/c after a build.

OUT_DIR=${OUT_DIR:-build-out}
REPORT=$OUT_DIR/runtime-imports.txt

# Context for the report header only. `make runtime-imports` passes it as $1 -- not as an env var
# prefix, which some z/OS make implementations exec directly instead of routing through a shell
# when the recipe line has no other shell metacharacter, leaving "ZosMinLevel=zosv2r5" as the
# literal (missing) command. Fall back to reading toolchain.mk so a bare invocation still labels
# the output.
MIN_LEVEL=${1:-$(sed -n 's/^ZosMinLevel[[:blank:]]*=[[:blank:]]*\([^[:blank:]#]*\).*/\1/p' toolchain.mk 2>/dev/null | head -n 1)}
MIN_LEVEL=${MIN_LEVEL:-unknown}

# Every symbol the program object imports from a DLL starts life as an undefined external in one of
# our own object files, so {undefined} - {defined} across all inputs is exactly the external
# dependency set. Objects only, not the lib*.a archives: nm emits per-member headers for archives.
OBJS=$(ls $OUT_DIR/*.o $OUT_DIR/commands/*.o $OUT_DIR/server/*.o $OUT_DIR/extend/*.o 2>/dev/null)
if [ -z "$OBJS" ]; then
	echo "report_runtime_imports: no object files under $OUT_DIR; build first (make all)" >&2
	exit 1
fi

# POSIX nm output is "name type value size"; undefined externals have type U. Note -g and -u are
# mutually exclusive in POSIX nm, so filter on the type column instead of combining them.
if ! nm -P $OBJS >$OUT_DIR/.nm.raw 2>$OUT_DIR/.nm.err; then
	echo "report_runtime_imports: nm failed" >&2
	cat $OUT_DIR/.nm.err >&2
	exit 1
fi

awk '$2 == "U" { print $1 }' $OUT_DIR/.nm.raw | sort -u >$OUT_DIR/.nm.undef
awk '$2 != "U" { print $1 }' $OUT_DIR/.nm.raw | sort -u >$OUT_DIR/.nm.defined
comm -23 $OUT_DIR/.nm.undef $OUT_DIR/.nm.defined >$OUT_DIR/.imports

{
	echo "# External symbols imported by zo / zoa"
	echo "#"
	echo "# Declared minimum supported z/OS level: $MIN_LEVEL"
	echo "# Compiler: $(ibm-clang++64 --version 2>&1 | head -n 1)"
	echo "#"
	echo "# Informational. Every symbol here must be exported by the Language Environment runtime on"
	echo "# the floor release; one that is not causes CEE3561S at load time (zowex#871). That is"
	echo "# enforced at bind time by LDFLAGS, not by this report."
	echo "#"
	cat $OUT_DIR/.imports
} >$REPORT

echo "Wrote $REPORT ($(wc -l <$OUT_DIR/.imports) imported symbols, declared floor $MIN_LEVEL)."
