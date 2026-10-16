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

OUT_DIR=build-out
OUT_DIR_SWIG=$(OUT_DIR)/swig

# System SSL (GSKCMS) 64-bit definition side-deck. Required to resolve the
# gsk_* certificate/key APIs used by libzkr (certificates command group).
GSKCMS_SIDEDECK=/usr/lib/GSKCMS64.x

.INCLUDE: toolchain.mk

MACLIBS= -I../asmmac $(MACLIBS_BASE)
MTL_HEADERS=$(MTL_HEADERS_BASE) -I./chdsect

C_FLAGS=$(C_FLAGS_BASE) -I./chdsect
CPP_FLAGS=$(CPP_FLAGS_BASE) -I./chdsect
SWIG_FLAGS=$(SWIG_FLAGS_BASE) -I./chdsect

# make -DBuildType=DEBUG or -DBuildType=RELEASE
.IF $(BuildType) == DEBUG
CPP_BND_FLAGS=$(CPP_BND_DEBUG_FLAGS) $(DEBUGGER_FLAGS)
CPP_BND_FLAGS_AUTH=$(CPP_BND_DEBUG_FLAGS_AUTH) $(DEBUGGER_FLAGS)
C_FLAGS+=$(DEBUGGER_FLAGS)
DLL_BND_FLAGS=$(DLL_BND_DEBUG_FLAGS) $(DEBUGGER_FLAGS)
CPP_FLAGS+=$(DEBUGGER_FLAGS) $(OTHER_C_FLAGS)
# Metal C (xlc) rejects full `-g` (GONUMBER/TEST conflict with METAL/INLINE) and the
# clang-only `-H -dM`; use `-g1` like the RELEASE/dev builds.
ASM_FLAGS+=--verbose
ADD_GIT_HASH=TRUE
.ELSIF $(BuildType) == RELEASE
CPP_BND_FLAGS=$(CPP_BND_BASE_FLAGS)
CPP_BND_FLAGS_AUTH=$(CPP_BND_BASE_FLAGS_AUTH)
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS)
C_FLAGS+=$(RELEASE_FLAGS)
CPP_FLAGS+=$(RELEASE_FLAGS)
ASM_FLAGS+=--noverbose
ADD_GIT_HASH=FALSE
.ELSE
CPP_BND_FLAGS=$(CPP_BND_BASE_FLAGS)
CPP_BND_FLAGS_AUTH=$(CPP_BND_BASE_FLAGS_AUTH)
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS)
C_FLAGS+=-g1
CPP_FLAGS+=-g1
ASM_FLAGS+=--noverbose
ADD_GIT_HASH=TRUE
.END

.IF $(IsPullRequest) == TRUE
ADD_GIT_HASH=TRUE
.END

# Still investigating why we need `-g1` for the MetalC flags :'(
MTL_FLAGS+=-g1
MTL_FLAGS64+=-g1

# Apply LOG_FLAGS to all relevant compilation flags if logging is enabled
.IF DEFINED(LOG_FLAGS)
C_FLAGS+=$(LOG_FLAGS)
CPP_FLAGS+=$(LOG_FLAGS)
MTL_FLAGS+=$(LOG_FLAGS)
MTL_FLAGS64+=$(LOG_FLAGS)
.END

COMMAND_OBJS = $(OUT_DIR)/commands/certificates.o \
	$(OUT_DIR)/commands/core.o \
	$(OUT_DIR)/commands/ds.o \
	$(OUT_DIR)/commands/job.o \
	$(OUT_DIR)/commands/server.o \
	$(OUT_DIR)/commands/system.o \
	$(OUT_DIR)/commands/tso.o \
	$(OUT_DIR)/commands/uss.o \
	$(OUT_DIR)/commands/tool.o

SERVER_OBJS = $(OUT_DIR)/server/builder.o \
	$(OUT_DIR)/server/rpc_commands.o \
	$(OUT_DIR)/server/dispatcher.o \
	$(OUT_DIR)/server/plugin_bridge.o \
	$(OUT_DIR)/server/rpcio.o \
	$(OUT_DIR)/server/rpc_server.o \
	$(OUT_DIR)/server/validator.o \
	$(OUT_DIR)/server/worker.o

SWIG_EXTENDER_OBJS = $(OUT_DIR_SWIG)/zut.o $(OUT_DIR_SWIG)/zds.o $(OUT_DIR_SWIG)/zjb.o $(OUT_DIR_SWIG)/zcn.o $(OUT_DIR_SWIG)/zusf.o $(OUT_DIR_SWIG)/ztso.o $(OUT_DIR_SWIG)/zkr.o $(OUT_DIR_SWIG)/zkrio.o

all: libzut.so libzut.a libzds.so libzds.a libzusf.so libzusf.a libzcn.so libzcn.a libzjb.so libzjb.a libzkr.so libzkr.a zo zoa
swig-extenders: $(OUT_DIR_SWIG) $(SWIG_EXTENDER_OBJS)
tests:
	$(MAKE) -c test

#
# Out directories
#
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR_SWIG):
	mkdir -p $(OUT_DIR_SWIG)

#
# Utilities
#
$(OUT_DIR)/zut.o: zut.cpp
	@echo 'Building $(OUT_DIR)/zut.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zut.o: $(OUT_DIR_SWIG) zut.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zut.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zut.cpp

$(OUT_DIR)/zutm.s: zutm.c
	@echo 'Building $(OUT_DIR)/zutm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zutm.o: $(OUT_DIR)/zutm.s
	@echo 'Building $(OUT_DIR)/zutm.s'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zutm31.s: zutm31.c
	@echo 'Building $(OUT_DIR)/zutm31.s'
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zutm31.o: $(OUT_DIR)/zutm31.s
	@echo 'Building $(OUT_DIR)/zutm31.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

#
# Metal C Logger
#
.IF DEFINED(LOG_FLAGS)
$(OUT_DIR)/zlogger_metal.s: zlogger_metal.c
	@echo 'Building $(OUT_DIR)/zlogger_metal.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zlogger_metal.o: $(OUT_DIR)/zlogger_metal.s
	@echo 'Building $(OUT_DIR)/zlogger_metal.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

LIBZUT_LOGGER_OBJ=$(OUT_DIR)/zlogger_metal.o
.ELSE
LIBZUT_LOGGER_OBJ=
.END

$(OUT_DIR)/libzut.so: $(OUT_DIR)/zut.o $(OUT_DIR)/zutm.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o $(OUT_DIR)/zutcall24.o $(OUT_DIR)/zutm31.o $(LIBZUT_LOGGER_OBJ)
	@echo 'Building $(OUT_DIR)/libzut.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libzut.so: $(OUT_DIR) $(OUT_DIR)/libzut.so

$(OUT_DIR)/libzut.a: $(OUT_DIR)/zut.o $(OUT_DIR)/zutm.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o $(OUT_DIR)/zutcall24.o $(OUT_DIR)/zutm31.o $(LIBZUT_LOGGER_OBJ)
	@echo 'Building $(OUT_DIR)/libzut.a'
	ar -rv $@ $^

libzut.a: $(OUT_DIR) $(OUT_DIR)/libzut.a

#
# Data sets
#
$(OUT_DIR)/zam.s: zam.c
	@echo 'Building $(OUT_DIR)/zam.s'
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zam.o: $(OUT_DIR)/zam.s
	@echo 'Building $(OUT_DIR)/zam.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zdsm.s: zdsm.c
	@echo 'Building $(OUT_DIR)/zdsm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zdsm.o: $(OUT_DIR)/zdsm.s
	@echo 'Building $(OUT_DIR)/zdsm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zds.o: zds.cpp
	@echo 'Building $(OUT_DIR)/zds.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zds.o: $(OUT_DIR_SWIG) zds.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zds.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zds.cpp

$(OUT_DIR)/libzds.so: $(OUT_DIR)/zds.o $(OUT_DIR)/zdsm.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o $(OUT_DIR)/libzut.a
	@echo 'Building $(OUT_DIR)/libzds.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libzds.so: $(OUT_DIR) $(OUT_DIR)/libzds.so

$(OUT_DIR)/libzds.a: $(OUT_DIR)/zds.o $(OUT_DIR)/zdsm.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o
	@echo 'Building $(OUT_DIR)/libzds.a'
	ar -rv $@ $^

libzds.a: $(OUT_DIR) $(OUT_DIR)/libzds.a

#
# Certificates / key rings (R_datalib + System SSL GSKCMS)
#
$(OUT_DIR)/zkr.o: zkr.cpp
	@echo 'Building $(OUT_DIR)/zkr.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zkr.o: $(OUT_DIR_SWIG) zkr.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zkr.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zkr.cpp

$(OUT_DIR)/libzkr.so: $(OUT_DIR)/zkr.o
	@echo 'Building $(OUT_DIR)/libzkr.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^ $(GSKCMS_SIDEDECK)

libzkr.so: $(OUT_DIR) $(OUT_DIR)/libzkr.so

$(OUT_DIR)/libzkr.a: $(OUT_DIR)/zkr.o
	@echo 'Building $(OUT_DIR)/libzkr.a'
	ar -rv $@ $^

libzkr.a: $(OUT_DIR) $(OUT_DIR)/libzkr.a

# zkrio is a plain (non-SWIG) helper used only by the zowex command layer today;
# it deliberately stays out of libzkr.{so,a}, which binds standalone against
# nothing but the GSKCMS side deck (see zkr_py plan D8).
$(OUT_DIR)/zkrio.o: zkrio.cpp
	@echo 'Building $(OUT_DIR)/zkrio.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zkrio.o: $(OUT_DIR_SWIG) zkrio.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zkrio.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zkrio.cpp

#
# Jobs
#
$(OUT_DIR)/zjbm.s: zjbm.c
	@echo 'Building $(OUT_DIR)/zjbm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zjbm.o: $(OUT_DIR)/zjbm.s
	@echo 'Building $(OUT_DIR)/zjbm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zjb.o: zjb.cpp
	@echo 'Building $(OUT_DIR)/zjb.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zjb.o: $(OUT_DIR_SWIG) zjb.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zjb.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zjb.cpp

$(OUT_DIR)/libzjb.so: $(OUT_DIR)/zjb.o $(OUT_DIR)/zjbm.o $(OUT_DIR)/libzds.a $(OUT_DIR)/libzut.a
	@echo 'Building $(OUT_DIR)/libzjb.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libzjb.so: $(OUT_DIR) $(OUT_DIR)/libzjb.so

$(OUT_DIR)/libzjb.a: $(OUT_DIR)/zjb.o $(OUT_DIR)/zjbm.o $(OUT_DIR)/libzds.a $(OUT_DIR)/libzut.a
	@echo 'Building $(OUT_DIR)/libzjb.a'
	ar -rv $@ $^

libzjb.a: $(OUT_DIR) $(OUT_DIR)/libzjb.a

#
# Console
#
$(OUT_DIR)/zcn.o: zcn.cpp
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zcn.o: $(OUT_DIR_SWIG) zcn.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zcn.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zcn.cpp

$(OUT_DIR)/zcnm.s: zcnm.c
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zcnm.o: $(OUT_DIR)/zcnm.s
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zcnm31.s: zcnm31.c
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zcnm31.o: $(OUT_DIR)/zcnm31.s
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/libzcn.so: $(OUT_DIR)/zcn.o $(OUT_DIR)/zcnm.o $(OUT_DIR)/zcnm31.o $(OUT_DIR)/libzut.a
	@echo 'Building $(OUT_DIR)/libzcn.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libzcn.so: $(OUT_DIR) $(OUT_DIR)/libzcn.so

$(OUT_DIR)/libzcn.a: $(OUT_DIR)/zcn.o $(OUT_DIR)/zcnm.o $(OUT_DIR)/zcnm31.o
	@echo 'Building libzcn.a'
	ar -rv $@ $^

libzcn.a: $(OUT_DIR) $(OUT_DIR)/libzcn.a

#
# USS data
#
$(OUT_DIR)/zusf.o: zusf.cpp
	@echo 'Building $(OUT_DIR)/zusf.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/zusf.o: $(OUT_DIR_SWIG) zusf.cpp
	@echo 'Building $(OUT_DIR_SWIG)/zusf.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ zusf.cpp

$(OUT_DIR)/libzusf.so: $(OUT_DIR)/zusf.o $(OUT_DIR)/libzut.a
	@echo 'Building $(OUT_DIR)/libzusf.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libzusf.so: $(OUT_DIR) $(OUT_DIR)/libzusf.so

$(OUT_DIR)/libzusf.a: $(OUT_DIR)/zusf.o
	@echo 'Building $(OUT_DIR)/libzusf.a'
	ar -rv $@ $^

libzusf.a: $(OUT_DIR) $(OUT_DIR)/libzusf.a

#
# TSO
#
$(OUT_DIR)/ztso.o: ztso.cpp
	@echo 'Building $(OUT_DIR)/ztso.o'
	$(CXX) $(CPP_FLAGS) -o $@ $^

$(OUT_DIR_SWIG)/ztso.o: $(OUT_DIR_SWIG) ztso.cpp
	@echo 'Building $(OUT_DIR_SWIG)/ztso.o with SWIG macro'
	$(CXX) $(SWIG_FLAGS) -o $@ ztso.cpp

$(OUT_DIR)/libztso.so: ztso
	@echo 'Building $(OUT_DIR)/libztso.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^

libztso.so: $(OUT_DIR) $(OUT_DIR)/libztso.so

$(OUT_DIR)/libztso.a: $(OUT_DIR)/ztso.o
	@echo 'Building $(OUT_DIR)/libztso.a'
	ar -rv $@ $^

libztso.a: $(OUT_DIR) $(OUT_DIR)/libztso.a

#
# Commands
#
$(OUT_DIR)/commands/%.o: commands/%.cpp
	@echo 'Building $@'
	@mkdir -p $(@D)
	$(CXX) $(CPP_FLAGS) -c $< -o $@

#
# Server
#
$(OUT_DIR)/server/%.o: server/%.cpp
	@echo 'Building $@'
	@mkdir -p $(@D)
	$(CXX) $(CPP_FLAGS) -c $< -o $@

#
# JSON Metal C (used by server)
#
$(OUT_DIR)/zjsonm.s: zjsonm.c
	@echo 'Building $(OUT_DIR)/zjsonm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zjsonm.o: $(OUT_DIR)/zjsonm.s
	@echo 'Building $(OUT_DIR)/zjsonm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/json_output.o: json_output.cpp
	@echo 'Building $(OUT_DIR)/json_output.o'
	$(CXX) $(CPP_FLAGS) -c $< -o $@

#
# ZAM24
#
$(OUT_DIR)/zam24.o: zam24.s
	@echo 'Building $(OUT_DIR)/zam24.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

#
# ZUTCALL24 (below-the-line shim for calling 24-bit modules)
#
$(OUT_DIR)/zutcall24.o: zutcall24.s
	@echo 'Building $(OUT_DIR)/zutcall24.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

#
# Extensibility
#
$(OUT_DIR)/extend/plugin.o: extend/plugin.cpp
	@echo 'Building $@'
	@mkdir -p $(@D)
	$(CXX) $(CPP_FLAGS) -c $< -o $@

#
# Test CLI
#
$(OUT_DIR)/zo: $(OUT_DIR)/zo.o $(OUT_DIR)/json_output.o $(OUT_DIR)/extend/plugin.o $(OUT_DIR)/zkrio.o $(COMMAND_OBJS) $(SERVER_OBJS) $(OUT_DIR)/zjsonm.o $(OUT_DIR)/libzut.a $(OUT_DIR)/libzjb.a $(OUT_DIR)/libzds.a $(OUT_DIR)/libzusf.a $(OUT_DIR)/libztso.a $(OUT_DIR)/libzkr.a
	@echo 'Building zo (z/OS target level: $(ZosMinLevel))'
	$(CXX) $(CPP_BND_FLAGS) -o $@ $^ $(GSKCMS_SIDEDECK)

zo: $(OUT_DIR) $(OUT_DIR)/zo

$(OUT_DIR)/zoa: $(OUT_DIR)/zoa.o $(OUT_DIR)/json_output.o $(OUT_DIR)/commands/console.o $(OUT_DIR)/commands/core.o $(OUT_DIR)/zjsonm.o $(OUT_DIR)/libzcn.a $(OUT_DIR)/libzut.a
	@echo 'Building zoa (z/OS target level: $(ZosMinLevel))'
	$(CXX) $(CPP_BND_FLAGS_AUTH) -o $@ $^
	extattr +ap $@
	extattr -s $@

zoa: $(OUT_DIR) $(OUT_DIR)/zoa

$(OUT_DIR)/zo.o: zo.cpp
	@echo 'Building $(OUT_DIR)/zo.o'
	@echo 'Add git hash? ${ADD_GIT_HASH}'
	@if [ -f ../package.json ] && [ "${ADD_GIT_HASH}" = "TRUE" ]; then \
		GIT_HASH=$$(grep '"gitHash"' ../package.json | sed 's/.*"gitHash"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'); \
		echo "Extracted git hash: $$GIT_HASH"; \
	else \
		echo "ADD_GIT_HASH is not TRUE, or package.json not found, building without GIT_HASH"; \
		GIT_HASH=""; \
	fi \
	&& if [ -f ../package.json ]; then \
		PACKAGE_VERSION=$$(grep '"version"' ../package.json | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'); \
		PACKAGE_VERSION="$$PACKAGE_VERSION$$GIT_HASH"; \
		echo "Git hash is $$GIT_HASH for build type '${BuildType}'";  \
		echo "Extracted version: $$PACKAGE_VERSION"; \
		$(CXX) $(CPP_FLAGS) -DPACKAGE_VERSION="\"$$PACKAGE_VERSION\"" -c $^ -o $@; \
	else \
		echo "package.json not found, building without version"; \
		$(CXX) $(CPP_FLAGS) -c $^ -o $@; \
	fi

$(OUT_DIR)/zoa.o: zoa.cpp
	@echo 'Building $(OUT_DIR)/zoa.o'
	@echo 'Add git hash? ${ADD_GIT_HASH}'
	@if [ -f ../package.json ] && [ "${ADD_GIT_HASH}" = "TRUE" ]; then \
		GIT_HASH=$$(grep '"gitHash"' ../package.json | sed 's/.*"gitHash"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'); \
		echo "Extracted git hash: $$GIT_HASH"; \
	else \
		echo "ADD_GIT_HASH is not TRUE, or package.json not found, building without GIT_HASH"; \
		GIT_HASH=""; \
	fi \
	&& if [ -f ../package.json ]; then \
		PACKAGE_VERSION=$$(grep '"version"' ../package.json | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/'); \
		PACKAGE_VERSION="$$PACKAGE_VERSION$$GIT_HASH"; \
		echo "Git hash is $$GIT_HASH for build type '${BuildType}'";  \
		echo "Extracted version: $$PACKAGE_VERSION"; \
		$(CXX) $(CPP_FLAGS) -DPACKAGE_VERSION="\"$$PACKAGE_VERSION\"" -c $^ -o $@; \
	else \
		echo "package.json not found, building without version"; \
		$(CXX) $(CPP_FLAGS) -c $^ -o $@; \
	fi

#
# TODO(Kelosky): makefile for chdsect (must have ADATA in data set)
#

#
# Runtime import report
#
# Lists the external symbols zo/zoa import from DLLs. Informational: the floor is enforced at
# bind time by LDFLAGS, not here. Kept out of `all` so incremental builds and `npm run z:watch` stay
# fast. See compat/README.md.
#
runtime-imports: zo zoa
	sh tools/report_runtime_imports.sh $(ZosMinLevel)

#
# Misc targets
#
clean:
	rm -f *.o
	rm -f *.dbg
	rm -f *.so
	rm -f *.a
	rm -f *.u
	rm -f *.x
	rm -f *.lst
	rm -f CEEDUMP.*
	rm -rf $(OUT_DIR)

# Include auto-generated dependency files
.INCLUDE .IGNORE: $(SWIG_EXTENDER_OBJS:.o=.d)
