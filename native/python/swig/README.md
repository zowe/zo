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

The sample has an opt-in stable ABI wheel targeting GIL-enabled CPython 3.11 and later on z/OS. It includes an import-time loader that handles Python's version-specific DLL name automatically. It requires SWIG 4.2 or newer, `setuptools`, and `wheel` to build, plus `pip` for installation. Use SWIG's default Python generation options; options such as `-builtin` and `-O` are incompatible with this build.

On z/OS, with Python, SWIG, and the compiler on `PATH`:

```sh
cd sample
make abi3-wheel
```

`PYTHON` can select a different build interpreter, for example `make abi3-wheel PYTHON=/path/to/python3.11`. The target sets `ABI3=1`, which enables `Py_LIMITED_API=0x030B0000` and `py_limited_api=True` in `setup.py`, and passes `--py-limited-api=cp311` to the wheel builder. It forces compilation in `build/abi3` and writes the wheel to `dist/abi3`. The ordinary `make test` build remains version-specific.

Build with the oldest supported Python interpreter. On the tested build host, the resulting filename is `example-0.1-cp311-abi3-os390_29_00_3932.zos.whl`. The wheel advertises both the build host's platform tag and the newer `zos` tag using [compressed wheel tags](https://packaging.python.org/en/latest/specifications/platform-compatibility-tags/#compressed-tag-sets). It remains specific to z/OS and compatible native runtime/toolchain levels; these tags do not establish compatibility with every z/OS release or hardware model.

To test with each target interpreter, install the wheel in a fresh virtual environment
and run outside the source directory. Replace the wheel path with its absolute path:

```sh
python -m venv /path/to/test-venv
/path/to/test-venv/bin/python -m pip install --no-index --no-deps /path/to/example-0.1-cp311-abi3-os390_29_00_3932.zos.whl
cd /path/to/test-venv
bin/python -I -c "import os; previous = os.environ.get('LIBPATH'); import example; assert example.hello_ascii('Zowe') == 'Hello, Zowe!'; assert os.environ.get('LIBPATH') == previous"
```

The build records the linking interpreter's `LDLIBRARY` in `_example_runtime.py`. When `example` imports its native extension, the loader locates the running interpreter's library using `sys.base_prefix` and `sysconfig` (including inside a virtual environment). It creates a private temporary symlink using the DLL name recorded at build time, prepends its directory to `LIBPATH` for the native import, and then restores `LIBPATH` and removes the temporary directory. No Python DLL is bundled, and the Python installation and installed wheel files are not modified. Directly importing the internal `_example` module bypasses this loader.

The sample follows the real bindings' `_zbind_loader.py` checks and DLL resolution.
It uses Python's normal import mechanism for its single extension; the real bindings
also need explicit native-module selection and a shared lock across four extensions.

The loader temporarily changes the process-wide DLL search path; z/OS resolves DLL import names using [`LIBPATH`](https://www.ibm.com/docs/en/zos/2.5.0?topic=mudwrda-loading-dlls). This is a sample loading strategy, rather than an IBM-provided version-independent Python DLL interface.

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
