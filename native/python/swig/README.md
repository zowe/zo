# SWIG for z/OS

This directory builds [SWIG](https://www.swig.org/) (and its `pcre2` dependency) from source for z/OS, so it can generate the C++/Python wrapper code used by the Zowe Remote SSH Python bindings. It also contains a small [sample](sample/) project used to sanity-check a SWIG build.

## Prerequisites

On the z/OS build system:

- `bison`
- `git`
- `python`
- `gmake`
- The `xlclang`/`ibm-clang` compiler toolchain

`build.sh` checks for these tools on `PATH` and fails fast with a clear error if any are missing.

## Building

From the repo root, on your workstation:

```bash
npm run z:upload -- python/swig
npm run z:build:swig
```

This downloads the latest SWIG and PCRE2 source tarballs, uploads them to the configured z/OS host, and runs `build.sh` there. On success, the packaged build (`swig-<version>.pax.Z`) is downloaded to `dist/`.

If you just want to check whether SWIG is already installed on the target system (and skip rebuilding it), run:

```bash
npm run z:has:swig
```

## Testing the sample

`build.sh` builds and runs the [sample](sample/) as its last step, straight from the freshly built `swig` binary under `build/` — no installation needed. To re-test without rebuilding SWIG:

```bash
export PATH="$PWD/build:$PATH"
cd sample && make
```

**Note:** The [sample/Makefile](sample/Makefile) falls back to the `Lib/` directory next to the resolved `swig` binary if `swig -swiglib` can't find one.

## Installing SWIG

The `swig-<version>.pax.Z` from the [Building](#building) step contains the `swig` binary and its `Lib/` support files.

SWIG finds `Lib/` (to resolve `%include`d standard library files like `std_string.i`) via the `SWIG_LIB` environment variable, or otherwise via the path compiled into the binary (`swig -swiglib`) — usually `/usr/local/share/swig/X.Y.Z`.

There are two ways to install it:

- Extract the pax anywhere, put `swig` on `PATH`, and set `SWIG_LIB` to point at the extracted `Lib/`:

  ```bash
  pax -rz -f swig-<version>.pax.Z
  export PATH="$PWD:$PATH"
  export SWIG_LIB="$PWD/Lib"
  ```

- Or remap `Lib` to the compiled-in default path during extraction, so `SWIG_LIB` never needs to be set:

  ```bash
  mkdir -p /usr/local/share/swig
  pax -rz -f swig-<version>.pax.Z -s ',^Lib,/usr/local/share/swig/<version>,' -s ',^swig$,/usr/local/bin/swig,'
  ```
