#!/usr/bin/env bash
set -e
printf "\n"

script_dir="$(cd "$(dirname "$0")" && pwd)"
export PATH="$script_dir/../c/build-out:$PATH"

echo "Setting required env vars..."
printf "\n"
export _BPXK_AUTOCVT=ON
export _CEE_RUNOPTS="$_CEE_RUNOPTS FILETAG(AUTOCVT,AUTOTAG) POSIX(ON)"
export _TAG_REDIR_ERR=txt
export _TAG_REDIR_IN=txt
export _TAG_REDIR_OUT=txt

user=$(whoami)

data_set_adata=$user.ZNP.ADATA
data_set_chdr=$user.ZNP.CHDR

echo "Using ADATA data set: $data_set_adata"
echo "Using CHDR data set: $data_set_chdr"
printf "\n"

# Create ADATA data set, ignore errors if it fails
echo "Creating ADATA data set: $data_set_adata"
if ! zo data-set create-adata "$data_set_adata" >/dev/null 2>&1; then
	echo "Warning: Failed to create ADATA data set (may already exist)"
fi
printf "\n"

# Create CHDR data set, ignore errors if it fails
echo "Creating CHDR data set: $data_set_chdr"
if ! zo data-set create-vb "$data_set_chdr" >/dev/null 2>&1; then
	echo "Warning: Failed to create CHDR data set (may already exist)"
fi
printf "\n"

# Get filename without extension
filename_no_ext="${1%.*}"

echo "Running assembler command..."
echo "Command: as -madata --gadata=\"//'$data_set_adata($filename_no_ext)'\" $1"
as -madata --gadata="//'$data_set_adata($filename_no_ext)'" $1
echo "Assembler command completed."
printf "\n"

zo tool ccnedsct --ad "$data_set_adata($filename_no_ext)" --cd "$data_set_chdr($filename_no_ext)"
printf "\n"

echo "Copying generated header file to build-out..."
cp "//'$data_set_chdr($filename_no_ext)'" "build-out/$filename_no_ext.h"
echo "Copying completed."
printf "\n"

echo "Converting EBCDIC to ASCII..."
mv build-out/$filename_no_ext.h build-out/$filename_no_ext.h.u
echo "Command: iconv -f 1047 -t utf8 build-out/$filename_no_ext.h.u > build-out/$filename_no_ext.h"
iconv -f 1047 -t utf8 build-out/$filename_no_ext.h.u > build-out/$filename_no_ext.h
chtag -tc ISO8859-1 build-out/$filename_no_ext.h
echo "Conversion completed."
printf "\n"

echo "Adding #ifdef for ibm-clang vs legacy pragma syntax..."
awk '{
  if ($0 ~ /#pragma pack\(packed\)/) {
    print "#ifdef __open_xl__"
    print "#pragma pack(1)"
    print "#else"
    print "#pragma pack(packed)"
    print "#endif"
  } else if ($0 ~ /#pragma pack\(reset\)/) {
    print "#ifdef __open_xl__"
    print "#pragma pack()"
    print "#else"
    print "#pragma pack(reset)"
    print "#endif"
  } else {
    print $0
  }
}' build-out/$filename_no_ext.h > build-out/$filename_no_ext.h.tmp && \
	mv build-out/$filename_no_ext.h.tmp build-out/$filename_no_ext.h
echo "Pragma ifdef replacement completed."
printf "\n"

echo "Cleaning up..."
rm "build-out/$filename_no_ext.h.u"
echo "Cleanup completed."
printf "\n"
