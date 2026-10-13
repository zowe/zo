# Plan and implementation: one z/OS wheel for multiple Python versions

Updated: 2026-10-07

## Goal and chosen approach

Install one `zbind` wheel into a fresh z/OS UNIX venv without a compiler, SWIG,
source checkout, or `PYTHONPATH`. Keep the existing public module imports.

The first approach was a single CPython Stable ABI binary. The Limited API build
succeeded, but IBM's z/OS linker bound those extensions to `libpython3.11.so`.
They failed to load under Python 3.13 and 3.14 because that DLL was unavailable.
Python API stability alone does not remove this platform loader dependency.
See [Python's platform considerations](https://docs.python.org/3.13/c-api/stable.html)
and [IBM's IMPORT statement](https://www.ibm.com/docs/en/zos/3.1.0?topic=reference-import-statement).

The selected fallback is **one wheel containing native copies linked for Python
3.11, 3.13, and 3.14**. A small loader chooses the matching copy. Adding a future
Python minor version requires another native copy and validation; this wheel is
not automatically compatible with every Python release.

## Alternatives considered

After the Stable ABI probe exposed the version-specific Python DLL dependency,
three options were presented. The first was selected for this implementation.

| Option | Installation experience | Tradeoffs and future maintenance |
| --- | --- | --- |
| **One wheel containing multiple native builds (selected)** | Users install the same wheel on Python 3.11, 3.13, or 3.14; the loader chooses the matching extension. | Larger archive and a small selection layer. Each supported Python minor version needs its own linked copies and testing. Future versions require an updated wheel. |
| **One wheel per Python version** | Publish separate wheels for each interpreter/ABI. When installing from an index, pip selects a compatible wheel; manual downloads require choosing the matching file. | Smaller downloads and simpler loading, but more release artifacts to build and publish. Future versions still require another wheel and validation. This would not meet the preference for one shared wheel file. |
| **Extensive refactoring to remove the Python ABI dependency** | A Python wrapper calls a Python-independent native library through a C ABI and an FFI mechanism supported on z/OS. This could allow one platform wheel across more Python versions. | Requires designing a C interface around the C++ APIs, handling memory ownership, strings, byte payloads, errors, and result collections, and proving the FFI/loader works on z/OS. Existing Python behavior would need compatibility coverage. This is a larger project, and broader version support would still require a minimum Python version and runtime testing. |

The chosen option preserves the existing SWIG bindings and public imports while
providing one installable artifact for the three tested runtimes. Separate wheels
remain a practical alternative if archive size or release tooling becomes more
important. A C ABI/FFI redesign remains an option if avoiding native builds for
each future Python version becomes the priority; its feasibility has not been
proven in this implementation.

## Verified target inventory

| Selection through `fpm` | IBM Python version | pip platform convention |
| --- | --- | --- |
| `fpm 11` | 3.11.7 | `os390_29_00_3932` |
| `fpm 13` | 3.13.15 | `os390_29_00_3932` |
| `fpm 14` | 3.14.7 | `zos` |

All three recognize `.abi3.so` filenames. The final wheel carries explicit
interpreter/ABI tags for the included versions, plus both verified platform
conventions. It excludes Python 3.12, free-threaded builds, Python 2, and other
implementations. Only the current LPAR/runtime combinations have been exercised.

`fpm` changes a shared `current` symlink. Each test uses a fresh venv created from
its selected interpreter's resolved executable. Restore the original selection
when the matrix finishes; switching versions does not update an existing venv.

## Implementation

1. Compile all four modules with Python 3.11's Limited API, preserving ASCII
   wrappers and EBCDIC shared native sources. Embed the USS Metal C helpers to
   avoid a dependency on the checkout's `libzut.so`.
2. Record successful link commands and link native copies using each additional
   runtime's Python side deck. The System SSL side deck remains a build input;
   compatible System SSL and Language Environment/C++ runtimes remain prerequisites.
3. Stage SWIG Python proxies, loader code, license text, and metadata as UTF-8.
   Store extensions and the wheel ZIP as binary bytes.
4. Assemble one wheel with all twelve native copies, complete `METADATA`, expanded
   compatibility tags, and `RECORD` hashes. Publish the completed archive only after
   successful assembly.
5. On import, select `_zbind_native/cp311`, `cp313`, or `cp314` and load that
   runtime's extension. Preserve `zds_py`, `zjb_py`, `zusf_py`, and `zkr_py` imports.

The relevant files are `setup.py`, `package_wheel.py`, `_zbind_loader.py`,
`pyproject.toml`, `requirements-build.txt`, and the `Makefile` wheel target.
`test/run_wheel_tests.py` creates fresh environments and stages tests outside the
bindings package; `test/test_wheel_installation.py` asserts installed import paths.

Functional testing also exposed a native SWIG encoding issue: parsed default
`"pem"` arguments became EBCDIC octal escapes inside an ASCII wrapper. A default
argument typemap in `zkr_py.i` initializes that value using the wrapper's execution
charset. Existing certificate lifecycle tests exercise this behavior.

## Build and install

See [README.md](README.md#python-wheel) for commands to prepare the build venv,
provide additional Python side decks, build the wheel, and validate installation.
The laptop entry point is `npm run z:python:wheel`, implemented in
`scripts/buildTools.ts`. It uses the active `config.yaml` profile
(`sshProfile`, `deployDir`, and `preBuildCmd`). The profile's `pythonEnv` supports
`ZPY_PYTHON_ROOT` (default `/usr/lpp/IBM/cyp/`) and individual
`ZPY_PYTHON_311`, `ZPY_PYTHON_313`, and `ZPY_PYTHON_314` executable paths; local
environment values take precedence. Build preparation and runtime discovery
live in `package_wheel.py`, invoked with `--build-from-stdin`; TypeScript handles
configuration, SSH, source uploads, download verification, and cleanup.
It stages current sources in
a temporary remote directory, builds all native runtime copies, downloads and
verifies the wheel in the project's root `dist/`, and removes its staging trees
and build venv. It does not switch the shared `fpm` selection.
Build dependencies use the authorized package index through `PIP_INDEX_URL`;
credentials must stay out of repository files and console output.

Expected artifact:

```text
zbind-1.0.0-cp311.cp313.cp314-cp311.cp313.cp314-os390_29_00_3932.zos.whl
```

Compressed interpreter and ABI tags expand to a Cartesian product. Standard
CPython pip matches its own interpreter/ABI pair; free-threaded ABI tags do not
match. The filename and `WHEEL` metadata describe the same expanded tag set.
See the [wheel specification](https://packaging.python.org/en/latest/specifications/binary-distribution-format/).

## Validation completed

- Build in an isolated remote copy, preserving the working bindings sources.
- Build and download with `npm run z:python:wheel`, then install the identical
  archive in fresh venvs using each runtime's fixed executable path.
- Assert proxies and native modules load from the venv, with the matching native directory.
- Run installation, USS, dataset, job, certificate/key-ring, and SWIG lifetime tests
  with disposable resources and cleanup. Report skipped authority-dependent cases.
- Verify uninstall/reinstall and reject unsupported interpreter/ABI tags.
- Check archive contents, UTF-8 source validity, `RECORD` hashes, and transfer checksum.
- Preserve test reports and document the tested support matrix.

The wheel downloaded by the npm command completed the final matrix:

| Runtime | Functional and installation tests | Uninstall/reinstall |
| --- | --- | --- |
| Python 3.11.7 | 119 passed; no skips | Passed |
| Python 3.13.15 | 119 passed; no skips | Passed |
| Python 3.14.7 | 119 passed; no skips | Passed |

The eight warnings per test run are SWIG type deprecation warnings. The earlier
PEM default failures are fixed. Disposable venvs are removed after validation to
avoid exhausting `/tmp`. Final JUnit reports are saved in the project's root
`dist/wheel-tests/`; remote validation directories and earlier build experiments
are removed after collecting the reports. The npm build and final matrix use
fixed interpreter paths, leaving `fpm` unchanged.
Compatibility tag checks reject Python 3.10, 3.12, 3.15, and free-threaded 3.14.
The remote and downloaded archive checksums match; all `RECORD` hashes, UTF-8
Python sources, twelve native copies, and extension execute permissions were verified.

The downloaded wheel has a companion `.whl.sha256` file. Its SHA-256 is:

```text
8e72a0223f9dcdca3d4c73b957dc2f14404c3000732959da84e50896ef21fddd
```

Binary download required extra care: this SSH environment converted raw binary
output even when the remote wheel was tagged binary. Base64 text transfer followed
by local decoding preserved the archive and matched its remote SHA-256 checksum.
Use a verified binary-safe transfer and compare hashes; a `.whl` filename alone
is not evidence that transfer preserved the bytes.
The npm command applies this base64 transfer automatically, compares the remote
SHA-256 before publishing the local archive, and writes a companion checksum file.
Rebuilding can change archive hashes; use the checksum beside the current artifact.

## Cleanup and build lifetime

The npm command removes its uploaded sources, native objects, generated wrappers,
build venv, and intermediate wheel on completion, including failure paths. Only
the downloaded wheel and checksum remain locally. It reuses the existing remote
generated headers and preserves the original remote bindings directory.

Earlier isolated build copies, validation venvs, transfer scratch files, and the
duplicate wheel in the local bindings `dist/` were removed after final validation.
The release wheel and final test reports remain in the project's root `dist/`.
The implementation also avoids relinking the already-built Python 3.11 extensions
and builds only the Metal C helper objects required by the wheel.

## Future versions

For each new supported Python minor version, add its side deck to the build,
include its runtime tag, and run the same fresh-venv matrix. If a new runtime needs
changes beyond the 3.11 Limited API, update the wrapper strategy before adding it
to the support statement. A fully version-independent C ABI/FFI design remains a
larger alternative if maintaining runtime copies becomes undesirable.
