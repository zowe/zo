#
#  This program and the accompanying materials are
#  made available under the terms of the Eclipse Public License v2.0
#  which accompanies this distribution, and is available at
#  https://www.eclipse.org/legal/epl-v20.html
#
#  SPDX-License-Identifier: EPL-2.0
#
#  Shared toolchain definitions for the native Zowe Remote SSH components.
#

CXX=ibm-clang++64
METALC_CC_FLAGS=_CC_ACCEPTABLE_RC=0 _C89_ACCEPTABLE_RC=0 _CXX_ACCEPTABLE_RC=0
CC=$(METALC_CC_FLAGS) xlc
ASM=as

#
# Minimum supported z/OS level.
#
# $(ZosMinLevel) is the release this project promises its published artifact loads on, declared here
# and nowhere else. `-mzos-target` pins the system-header API level the compiler assumes (via
# __TARGET_LIB__), which catches a call to a libc function that only exists on a newer release.
#
# It does NOT restrict libc++ and makes no link-step change, so it is not what keeps the binary
# loadable on the floor release. Two other things do that, and both live outside this file:
#   - the Open XL C/C++ level used to build (2.1; pinned in .github/workflows/zos-build.yml)
#   - binding against side decks captured from a system at the floor level (LDFLAGS)
# See compat/README.md and doc/troubleshooting.md.
#
# Open XL 2.x rejects target levels older than zosv2r4. A compiler that does not accept the option
# at all cannot build this project; drop -mzos-target from TARGET_FLAGS below if you need to try.
#
ZosMinLevel=zosv2r5

TARGET_FLAGS=-mzos-target=$(ZosMinLevel)

CPP_BND_BASE_FLAGS=-Wl,-bMAP $(TARGET_FLAGS) $(LDFLAGS)
CPP_BND_BASE_FLAGS_AUTH=-Wl,-bMAP -Wl,-bAC=1 $(TARGET_FLAGS) $(LDFLAGS)
CPP_BND_DEBUG_FLAGS=-Wl,-bMAP -Wl,-bLIST $(TARGET_FLAGS) $(LDFLAGS)
CPP_BND_DEBUG_FLAGS_AUTH=-Wl,-bMAP -Wl,-bLIST -Wl,-bAC=1 $(TARGET_FLAGS) $(LDFLAGS)

DLL_BND_BASE_FLAGS=-shared -Wl,-bMAP $(TARGET_FLAGS) $(LDFLAGS)
DLL_BND_DEBUG_FLAGS=-shared -Wl,-bMAP -Wl,-bLIST $(TARGET_FLAGS) $(LDFLAGS)

#
# Metal C compilation options
#
MTL_BASE_OPTS=metal,\
 langlvl(extended),\
 sscom,\
 nolongname,\
 inline,\
 genasm,\
 csect,\
 nose,\
 warn64,\
 optimize(2)
MTL_LIST_OPTS=,inlrpt,list,aggregate

MTL_OPTS=$(MTL_BASE_OPTS)
.IF $(BuildType) == DEBUG
MTL_OPTS+=$(MTL_LIST_OPTS)
.END
MTL_OPTS64=$(MTL_OPTS),lp64
MTL_FLAGS=-S -W "c,$(MTL_OPTS)"
MTL_FLAGS64=-S -W "c,$(MTL_OPTS64)"

#
# Assembly macros and headers
#
MACLIBS_BASE= -ISYS1.MACLIB \
 -ISYS1.MODGEN \
 -ICBC.SCCNSAM

MTL_HEADERS_BASE=-I/usr/include/metal \
 -I/usr/include

ASM_FLAGS=-mRENT

#
# Compilation flags
#
C_FLAGS_BASE=-fvisibility=default -c
CPP_FLAGS_BASE=$(TARGET_FLAGS) -fvisibility=default -c -std=gnu++17 -fno-aligned-allocation -D_EXT -D_OPEN_SYS_FILE_EXT=1 -MD
SWIG_FLAGS_BASE=-DSWIG $(CPP_FLAGS_BASE)

#
# Build type handling (DEBUG, RELEASE, or default for dev)
#
.IF $(BuildType) == DEBUG
# Enable only Metal C instrumentation; C++ logging is always available.
LOG_FLAGS=-DZLOG_ENABLE
DEBUGGER_FLAGS=-g
OTHER_C_FLAGS=-H -dM
.ELSIF $(BuildType) == RELEASE
RELEASE_FLAGS=-g0 -O2
.END
