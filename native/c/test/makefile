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

.INCLUDE: ../toolchain.mk

# System SSL (GSKCMS) 64-bit side-deck: resolves the gsk_* symbols referenced by
# zkr.o (linked into the runner for the certificates tests). Mirrors ../makefile.
GSKCMS_SIDEDECK=/usr/lib/GSKCMS64.x

BIND=ld

# https://www.ibm.com/docs/en/zos/3.1.0?topic=bor-binder-options
# https://www.ibm.com/docs/en/zos/3.1.0?topic=descriptions-ld-link-object-files
BIND_FLAGS = -bRMODE=ANY
BIND_FLAGS_AUTH = -bRMODE=ANY,AC=1
BIND_FLAGS_64 = -bRMODE=ANY,AMODE=64
BIND_FLAGS_64_AUTH = -bRMODE=ANY,AMODE=64,AC=1

MACLIBS= -I../../asmmac $(MACLIBS_BASE)
MTL_HEADERS=$(MTL_HEADERS_BASE) -I../ -I../chdsect

C_FLAGS=$(C_FLAGS_BASE) -I./chdsect
CPP_FLAGS=$(CPP_FLAGS_BASE)
CXXFLAGS=$(CPP_FLAGS_BASE) -I../ -I../chdsect

# make -DBuildType=DEBUG or -DBuildType=RELEASE
.IF $(BuildType) == DEBUG
DEBUGGER_FLAGS=-g
CPP_BND_FLAGS=$(CPP_BND_DEBUG_FLAGS) $(DEBUGGER_FLAGS)
CPP_BND_FLAGS_AUTH=$(CPP_BND_DEBUG_FLAGS_AUTH) $(DEBUGGER_FLAGS)
C_FLAGS+=$(DEBUGGER_FLAGS)
DLL_BND_FLAGS=$(DLL_BND_DEBUG_FLAGS) $(DEBUGGER_FLAGS)
MTL_FLAGS+=-g1
MTL_FLAGS64+=-g1
ASM_FLAGS+=--verbose
CPP_LIST_FLAG=
MTL_LIST_FLAG=-qlist=$*.mtl.lst
ASM_LIST_FLAG=-a=$*.s.lst
.ELSIF $(BuildType) == RELEASE
CPP_BND_FLAGS=$(CPP_BND_BASE_FLAGS) $(RELEASE_FLAGS)
CPP_BND_FLAGS_AUTH=$(CPP_BND_BASE_FLAGS_AUTH) $(RELEASE_FLAGS)
C_FLAGS+=$(RELEASE_FLAGS)
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS) $(RELEASE_FLAGS)
CPP_FLAGS+=$(RELEASE_FLAGS)
CXXFLAGS+=-DRELEASE_BUILD
MTL_FLAGS+=-qNOSOURCE -qNODEBUG -qNOSHOWINC
MTL_FLAGS64+=-qNOSOURCE -qNODEBUG -qNOSHOWINC
ASM_FLAGS+=--noverbose
CPP_LIST_FLAG=
MTL_LIST_FLAG=
ASM_LIST_FLAG=
.ELSE
CPP_BND_FLAGS=$(CPP_BND_BASE_FLAGS) -g1
CPP_BND_FLAGS_AUTH=$(CPP_BND_BASE_FLAGS_AUTH) -g1
C_FLAGS+=-g1
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS) -g1
CPP_FLAGS+=-g1
MTL_FLAGS+=-qNOSOURCE -qNODEBUG -qNOSHOWINC
MTL_FLAGS64+=-qNOSOURCE -qNODEBUG -qNOSHOWINC
ASM_FLAGS+=--noverbose
CPP_LIST_FLAG=
MTL_LIST_FLAG=
ASM_LIST_FLAG=
.END

# Apply DEBUG-only Metal instrumentation flags, including C++ lifecycle callers.
.IF $(BuildType) == DEBUG
C_FLAGS+=$(LOG_FLAGS)
CPP_FLAGS+=$(LOG_FLAGS)
CXXFLAGS+=$(LOG_FLAGS)
MTL_FLAGS+=$(LOG_FLAGS)
MTL_FLAGS64+=$(LOG_FLAGS)
.END

all:
	$(MAKE) build-out build-out/ztest_runner


#
# Output directories
#
build-out:
	@echo 'Creating build-out'
	mkdir -p build-out

#
# Testing utilities
#
build-out/ztest_runner.o: ztest_runner.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

.IF $(BuildType) == DEBUG
METAL_LOGGER_OBJ=build-out/zlogger_metal.o
.ELSE
METAL_LOGGER_OBJ=
.END

build-out/ztest_runner: build-out/ztest_runner.o \
build-out/zbase64.test.o \
build-out/zstd.test.o \
build-out/zstorage.test.o \
build-out/zstorage.metal.test.o \
build-out/zut.test.o \
build-out/zut.o \
build-out/zutm.o \
build-out/zutm31.o \
build-out/zutcall24.o \
build-out/zds.test.o \
build-out/zds.o \
build-out/zdsm.o \
build-out/zkr.test.o \
build-out/zkr.o \
build-out/zam.o \
build-out/zam24.o \
build-out/zjb.test.o \
build-out/zjb.o \
build-out/zjbm.o \
build-out/zcn.test.o \
build-out/zcn.o \
build-out/zcnm.o \
build-out/zcnm31.o \
build-out/zrecovery.test.o \
build-out/zrecovery.metal.test.o \
build-out/zmetal.test.o \
build-out/zmetal.metal.test.o \
build-out/zusf.test.o \
build-out/zusf.o \
build-out/zo.ds.test.o \
build-out/zo.uss.test.o \
build-out/zo.job.test.o \
build-out/zo.system.test.o \
build-out/zo.job.list.test.o \
build-out/zo.job.submit.test.o \
build-out/zo.job.manage.test.o \
build-out/zo.test.o \
build-out/zo.tso.test.o \
build-out/zo.plugin.test.o \
build-out/zoa.console.test.o \
build-out/zutils.o \
build-out/parser.test.o \
build-out/result_table.test.o \
build-out/zlogger.test.o \
$(METAL_LOGGER_OBJ) \
build-out/zwto.test.o \
build-out/zjson.test.o \
build-out/zjsonm.o \
build-out/json_output.o \
build-out/zo.server.test.o \
build-out/zo.uss.server.test.o \
build-out/zo.ds.server.test.o \
build-out/zo.cert.server.test.o \
build-out/zo.job.server.test.o \
build-out/server.worker.test.o \
build-out/server_validator.o \
build-out/server.validator.test.o \
build-out/server_logger.test.o \
build-out/plugin.test.o \
build-out/plugin.o
	$(CXX) $(CPP_BND_FLAGS) -o $@ $^ $(GSKCMS_SIDEDECK)

build-out/zut.o:
	ln -sf ../../build-out/zut.o build-out/zut.o

build-out/zutm.o:
	ln -sf ../../build-out/zutm.o build-out/zutm.o

build-out/zutm31.o:
	ln -sf ../../build-out/zutm31.o build-out/zutm31.o

build-out/zutcall24.o:
	ln -sf ../../build-out/zutcall24.o build-out/zutcall24.o

build-out/zds.o:
	ln -sf ../../build-out/zds.o build-out/zds.o

build-out/zdsm.o:
	ln -sf ../../build-out/zdsm.o build-out/zdsm.o

build-out/zkr.o:
	ln -sf ../../build-out/zkr.o build-out/zkr.o

build-out/zam.o:
	ln -sf ../../build-out/zam.o build-out/zam.o

build-out/zam24.o:
	ln -sf ../../build-out/zam24.o build-out/zam24.o

build-out/zjb.o:
	ln -sf ../../build-out/zjb.o build-out/zjb.o

build-out/zjbm.o:
	ln -sf ../../build-out/zjbm.o build-out/zjbm.o

build-out/zcn.o:
	ln -sf ../../build-out/zcn.o build-out/zcn.o

build-out/zcnm.o:
	ln -sf ../../build-out/zcnm.o build-out/zcnm.o

build-out/zcnm31.o:
	ln -sf ../../build-out/zcnm31.o build-out/zcnm31.o

build-out/zusf.o:
	ln -sf ../../build-out/zusf.o build-out/zusf.o

build-out/server_validator.o:
	ln -sf ../../build-out/server/validator.o build-out/server_validator.o

build-out/plugin.o: ../extend/plugin.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/json_output.o: ../json_output.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.ds.test.o: zo.ds.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.uss.test.o: zo.uss.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.job.test.o: zo.job.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.system.test.o: zo.system.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.job.list.test.o: zo.job.list.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.job.submit.test.o: zo.job.submit.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.job.manage.test.o: zo.job.manage.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.tso.test.o: zo.tso.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.plugin.test.o: zo.plugin.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zoa.console.test.o: zoa.console.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.test.o: zo.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zutils.o: zutils.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zbase64.test.o: zbase64.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zstd.test.o: zstd.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zstorage.test.o: zstorage.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zstorage.metal.test.s: zstorage.metal.test.c
	$(CC) $(MTL_FLAGS64) $(MTL_LIST_FLAG) $(MTL_HEADERS) -o $@ $^

build-out/zstorage.metal.test.o: build-out/zstorage.metal.test.s
	$(ASM) $(ASM_FLAGS) $(ASM_LIST_FLAG) $(MACLIBS) -o $@ $^

build-out/zut.test.o: zut.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zjb.test.o: zjb.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zds.test.o: zds.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zkr.test.o: zkr.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zcn.test.o: zcn.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zrecovery.test.o: zrecovery.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zrecovery.metal.test.s: zrecovery.metal.test.c
	$(CC) $(MTL_FLAGS64) $(MTL_LIST_FLAG) $(MTL_HEADERS) -o $@ $^

build-out/zrecovery.metal.test.o: build-out/zrecovery.metal.test.s
	$(ASM) $(ASM_FLAGS) $(ASM_LIST_FLAG) $(MACLIBS) -o $@ $^

build-out/zmetal.test.o: zmetal.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zmetal.metal.test.s: zmetal.metal.test.c
	$(CC) $(MTL_FLAGS64) $(MTL_LIST_FLAG) $(MTL_HEADERS) -o $@ $^

build-out/zmetal.metal.test.o: build-out/zmetal.metal.test.s
	$(ASM) $(ASM_FLAGS) $(ASM_LIST_FLAG) $(MACLIBS) -o $@ $^

build-out/zusf.test.o: zusf.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/parser.test.o: parser.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/result_table.test.o: result_table.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zlogger.test.o: zlogger.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

.IF $(BuildType) == DEBUG
build-out/zlogger_metal.s: ../zlogger_metal.c
	$(CC) $(MTL_FLAGS64) $(MTL_LIST_FLAG) $(MTL_HEADERS) -o $@ $^

build-out/zlogger_metal.o: build-out/zlogger_metal.s
	$(ASM) $(ASM_FLAGS) $(ASM_LIST_FLAG) $(MACLIBS) -o $@ $^
.END

build-out/zwto.test.o: zwto.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zjson.test.o: zjson.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.server.test.o: zo.server.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.uss.server.test.o: zo.uss.server.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.ds.server.test.o: zo.ds.server.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.cert.server.test.o: zo.cert.server.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zo.job.server.test.o: zo.job.server.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/server.worker.test.o: server/worker.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/zjsonm.s: ../zjsonm.c
	$(CC) $(MTL_FLAGS64) $(MTL_LIST_FLAG) $(MTL_HEADERS) -o $@ $^

build-out/zjsonm.o: build-out/zjsonm.s
	$(ASM) $(ASM_FLAGS) $(ASM_LIST_FLAG) $(MACLIBS) -o $@ $^

build-out/server.validator.test.o: server/validator.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/server_logger.test.o: server_logger.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

build-out/plugin.test.o: plugin.test.cpp
	$(CXX) $(CXXFLAGS) $(CPP_LIST_FLAG) -c $^ -o $@

#
# Testing utilities
#
test:
	$(MAKE) build-out build-out/ztest_runner
	chmod +x test.sh
	./build-out/ztest_runner

clean:
	rm -f *.o
	rm -f *.dbg
	rm -f *.so
	rm -f *.a
	rm -f *.u
	rm -f *.x
	rm -f *.lst
	rm -f CEEDUMP.*
	rm -rf build-out
