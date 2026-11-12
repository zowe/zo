# Makefile for building Python bindings for zo

# Default target
all: swig-extenders build

# Environment variables for z/OS compatibility
_SWIG_ENV= \
	export _BPXK_AUTOCVT=ON; \
	export CC=ibm-clang64; \
	export CXX=ibm-clang++64; \
	export _CEE_RUNOPTS="FILETAG(AUTOCVT,AUTOTAG) POSIX(ON)" ; \
	export _CC_CCMODE=1; \
	export _CXX_CCMODE=1; \
	export _C89_CCMODE=1; \
	export _CC_EXTRA_ARGS=1; \
	export _CXX_EXTRA_ARGS=1; \
	export _C89_EXTRA_ARGS=1;

# Determine SWIG path and directory
_SWIG_LIB = \
	if [ -n "$$SWIG_LIB" ]; then \
		export SWIG_LIB="$$SWIG_LIB"; \
	elif SWIGLIB_PATH=$$(swig -swiglib 2>/dev/null) && [ -d "$$SWIGLIB_PATH" ]; then \
		export SWIG_LIB="$$SWIGLIB_PATH"; \
	else \
		export SWIG_LIB="$$(dirname $$(type swig | awk '{print $$NF}'))/swig_lib"; \
	fi;

SWIG=$(_SWIG_ENV) $(_SWIG_LIB) swig

# Generate SWIG wrappers
swig-wrappers:
	@echo '\nGenerating SWIG wrappers'
	$(SWIG) -python -c++ zusf_py.i
	$(SWIG) -python -c++ zjb_py.i
	$(SWIG) -python -c++ zds_py.i

# Build Python extensions
build: swig-wrappers
	@echo '\nBuilding Python extensions'
	$(_SWIG_ENV) python setup.py build_ext

# Build specific modules only
# build-zusf: swig-zusf
build-zusf:
	@echo '\nBuilding zusf_py module only'
	$(SWIG) -python -c++ zusf_py.i
	$(_SWIG_ENV) ZBIND_MODULES=zusf python setup.py build_ext

# build-zds: swig-zds
build-zds:
	@echo '\nBuilding zds_py module only'
	$(SWIG) -python -c++ zds_py.i
	$(_SWIG_ENV) ZBIND_MODULES=zds python setup.py build_ext

# build-zjb: swig-zjb
build-zjb:
	@echo '\nBuilding zjb_py module only'
	$(SWIG) -python -c++ zjb_py.i
	$(_SWIG_ENV) ZBIND_MODULES=zjb python setup.py build_ext

# Run SWIG extenders in the C directory
swig-extenders:
	@echo '\nBuilding SWIG extenders'
	$(MAKE) -c ../../c swig-extenders

# # Selective SWIG extenders for specific modules
# swig-zusf:
# 	@echo '\nBuilding SWIG extenders for zusf_py module'
# 	$(MAKE) -c ../../c swig-zusf

# swig-zds:
# 	@echo '\nBuilding SWIG extenders for zds_py module'
# 	$(MAKE) -c ../../c swig-zds

# swig-zjb:
# 	@echo '\nBuilding SWIG extenders for zjb_py module'
# 	$(MAKE) -c ../../c swig-zjb

help:
	@echo "Available targets:"
	@echo "  all           - Build all modules"
	@echo "  build         - Build all modules"
	@echo "  swig-extenders - Build SWIG extenders"
	@echo "  swig-wrappers - Generate SWIG wrappers"
	@echo "  build-zusf    - Build zusf_py module only"
	@echo "  build-zds     - Build zds_py module only"
	@echo "  build-zjb     - Build zjb_py module only"
	@echo "  swig-zusf     - Build SWIG extenders for zusf_py module"
	@echo "  swig-zds      - Build SWIG extenders for zds_py module"
	@echo "  swig-zjb      - Build SWIG extenders for zjb_py module"
	@echo "  clean         - Clean generated files"
	@echo "  help          - Show this help message"

# Clean generated files
clean:
	@echo '\nCleaning generated files'
	rm -f *.cxx
	rm -f *.x
	rm -f *.o
	rm -f *.so
	rm -f *.py[co]
	rm -rf build/
	rm -rf ../../c/build-out/swig

.PHONY: all help swig-wrappers swig-extenders build clean build-zusf build-zds build-zjb swig-zusf swig-zds swig-zjb
