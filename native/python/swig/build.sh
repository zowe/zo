#!/bin/sh
set -e

swigVersion=$1
pcreVersion=$2
export _CXX_CCMODE=1
export _CXX_EXTRA_ARGS=1
export MAKE=gmake

REQUIRED_TOOLS="bison git gunzip python $MAKE"
for tool in $REQUIRED_TOOLS; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: Required dependency '$tool' was not found in your PATH." >&2
        exit 1
    fi
done

echo "Extracting tarballs..."
rm -rf build
gunzip swig-${swigVersion}.tar.gz
pax -r -o from=ISO8859-1,to=IBM-1047 -f swig-${swigVersion}.tar && rm swig-${swigVersion}.tar
gunzip pcre2-${pcreVersion}.tar.gz
pax -r -o from=ISO8859-1,to=IBM-1047 -f pcre2-${pcreVersion}.tar && rm pcre2-${pcreVersion}.tar
mv swig-${swigVersion} build && mv pcre2-${pcreVersion} build/pcre

echo "Applying patches..."
cd build
for file in ../patches/*.patch; do
    git apply "$file"
done
touch Source/Makefile.in

echo "Building pcre2..."
./Tools/pcre-build.sh CC=xlclang CXX=xlclang++ MAKE=gmake --disable-dependency-tracking

echo "Building swig..."
./configure CC=xlclang CXX=xlclang++ MAKE=gmake --disable-ccache --disable-dependency-tracking
gmake

echo "Packaging swig..."
pax -wvz -f ../swig-${swigVersion}.pax.Z Lib swig

echo "Testing swig..."
export PATH=$PWD:$PATH
cd ../sample
make
