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

CC=ibm-clang64
CXX=ibm-clang++64
MC=xlc
ASM=as
PYTHON=python

_CC_CCMODE=1
_CXX_CCMODE=1
_C89_CCMODE=1
_CC_EXTRA_ARGS=1
_CXX_EXTRA_ARGS=1
_C89_EXTRA_ARGS=1
.EXPORT: CC CXX _CC_CCMODE _CXX_CCMODE _C89_CCMODE _CC_EXTRA_ARGS _CXX_EXTRA_ARGS _C89_EXTRA_ARGS

CC_OPTS=metal,langlvl(extended),sscom,nolongname,inline,genasm,inlrpt,csect,nose,lp64,list,warn64,optimize(2),list,showinc,showmacro,source,aggregate
CXX_FLAGS=-std=gnu++17 -fvisibility=default

all: test

#
# Metal C
#
zmetal.s: zmetal.c zmetal.h
	@echo 'Building zmetal.s'
	$(MC) -S -W"c,$(CC_OPTS)" -qlist=zmetal.c.lst -I/usr/include/metal -o $@ zmetal.c

zmetal.o: zmetal.s
	@echo 'Building zmetal.o'
	$(ASM) -mrent -a=zmetal.s.lst -ISYS1.MACLIB -ICBC.SCCNSAM -o $@ zmetal.s

#
# C++ wrapper around the Metal C function
#
hello.o: hello.cpp hello.h zmetal.h
	@echo 'Building hello.o'
	$(CXX) $(CXX_FLAGS) -c -o $@ hello.cpp

#
# SWIG wrapper generation
#
example_wrap.cxx: example.i example.h
	@echo 'Generating SWIG wrapper'
	SWIG_LIB_DIR=$$(swig -swiglib); \
	if [ ! -d "$$SWIG_LIB_DIR" ]; then \
		export SWIG_LIB="$$(dirname "$$(command -v swig)")/Lib"; \
	fi; \
	swig -c++ -python example.i

#
# Python extension module
#
build-ext: example_wrap.cxx example.cpp example.h setup.py zmetal.o hello.o
	@echo 'Building Python extension'
	$(PYTHON) setup.py build_ext --inplace

abi3-wheel: example_wrap.cxx example.cpp example.h setup.py _example_runtime.py zmetal.o hello.o
	env ABI3=1 $(PYTHON) setup.py build --build-base build/abi3 --force bdist_wheel --py-limited-api=cp311 --dist-dir dist/abi3

test: build-ext
	@echo 'Running example'
	$(PYTHON) -c "import example; print(example.hello_ascii('Zowe'))"

clean:
	rm -f *.o
	rm -f *.s
	rm -f *.lst
	rm -f *.cxx
	rm -f *.pyc
	rm -f example.py
	rm -f _example*.so
	rm -rf build

.PHONY: all build-ext abi3-wheel test clean
