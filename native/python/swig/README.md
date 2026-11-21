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

### Testing an abi3 wheel

The sample has an opt-in stable ABI wheel targeting GIL-enabled CPython 3.11 and later on z/OS. It includes an import-time loader that handles Python's version-specific DLL name automatically. It requires SWIG 4.2 or newer, `setuptools`, `wheel`, and `pip` to build. Use SWIG's default Python generation options; options such as `-builtin` and `-O` are incompatible with this experiment.

On z/OS, with Python, SWIG, and the compiler on `PATH`:

```sh
cd sample
python check_abi3.py
make abi3-wheel
python check_abi3.py dist/abi3/example-0.1-cp311-abi3-*.whl
```

`PYTHON` can select a different build interpreter, for example `make abi3-wheel PYTHON=/path/to/python3.11`. The target sets `ABI3=1`, which enables `Py_LIMITED_API=0x030B0000` and `py_limited_api=True` in `setup.py`, and passes `--py-limited-api=cp311` to the wheel builder. It forces compilation in `build/abi3` and writes the wheel to `dist/abi3`. The ordinary `make test` build remains version-specific.

Build with the oldest supported Python interpreter. On the tested build host, the resulting filename is `example-0.1-cp311-abi3-os390_29_00_3932.zos.whl`. The wheel advertises both the build host's platform tag and the newer `zos` tag using [compressed wheel tags](https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/#compressed-tag-sets). It remains specific to z/OS and compatible native runtime/toolchain levels; these tags do not establish compatibility with every z/OS release or hardware model.

After installation, use the public `example` module:

```sh
python -m pip install /path/to/example-0.1-cp311-abi3-os390_29_00_3932.zos.whl
python -c "import example; print(example.hello_ascii('Zowe'))"
```

The build records the linking interpreter's `LDLIBRARY` in `_example_runtime.py`. When `example` imports its native extension, the loader locates the running interpreter's library using `sys.base_prefix` and `sysconfig` (including inside a virtual environment). It creates a private temporary symlink using the DLL name recorded at build time, prepends its directory to `LIBPATH` for the native import, and then restores `LIBPATH` and removes the temporary directory. No Python DLL is bundled, and the Python installation and installed wheel files are not modified. Directly importing the internal `_example` module bypasses this loader.

The loader temporarily changes the process-wide DLL search path; z/OS resolves DLL import names using [`LIBPATH`](https://www.ibm.com/docs/en/zos/2.5.0?topic=mudwrda-loading-dlls). This is a sample loading strategy, rather than an IBM-provided version-independent Python DLL interface.

Without a wheel argument, `check_abi3.py` prints the interpreter's extension suffixes, linker settings, and compatible `cp311-abi3` wheel tags. With a wheel, it verifies the filename and internal metadata, installs into a temporary directory without dependencies, and checks the greetings and `LIBPATH` restoration. The import runs in an isolated Python process outside the source directory. The optional `--load-only` mode bypasses pip's platform-tag check for diagnostics.

The wheel was built once on IBM Python 3.11.7 with SWIG 4.3.1 and Open XL C/C++ 2.2. The same wheel passed pip installation, isolated import, and Metal C greeting checks on IBM Python 3.11.7, 3.13.15, and 3.14.7, without external DLL aliases or additional loader configuration. These checks cover the sample on the tested systems; other Python versions and z/OS environments require validation.

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
