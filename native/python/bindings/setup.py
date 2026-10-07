#!/usr/bin/env python3

"""
Build the Zowe Remote SSH native Python bindings.
"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py
import os
import json
import subprocess

C_PATH = "../../c"
chdsect = os.path.abspath(f"{C_PATH}/chdsect")
ztype = os.path.abspath(C_PATH)
build_out_path = f"{C_PATH}/build-out"
GSKCMS_SIDEDECK = "/usr/lib/GSKCMS64.x"

# These sources are shared with zo, which compiles them with the ibm-clang default EBCDIC
# execution charset. The SWIG's default CFLAGS -fzos-le-char-mode=ascii flip the charset and 
# silently reinterprets every string literal. This break all EBCDIC control blocks it builds
# and the Metal C routines it calls. Let's compile those translations in EBCDIC and leave the 
# SWIG wrappers in ASCII. The conversion.hpp should bridge the two.
EBCDIC_CHAR_MODE = "-fzos-le-char-mode=ebcdic"
ABI3_BUILD = os.environ.get("ZBIND_ABI3") == "1"
build_options = {}
if os.environ.get("ZBIND_BUILD_BASE"):
    build_options["build_base"] = os.environ["ZBIND_BUILD_BASE"]


class BuildPyUtf8(build_py):
    """Stage native SWIG's EBCDIC proxies as UTF-8 for wheel installation."""

    def build_module(self, module, module_file, package):
        output, copied = super().build_module(module, module_file, package)
        if ABI3_BUILD and copied:
            with open(output, "wb") as target:
                subprocess.run(["iconv", "-f", "IBM-1047", "-t", "UTF-8", module_file],
                               stdout=target, check=True)
            subprocess.run(["chtag", "-t", "-c", "UTF-8", output], check=True)
        return output, copied


class BuildExtMixedCharMode(build_ext):
    """Compiles the shared native/c sources EBCDIC and the SWIG wrappers ASCII."""

    def finalize_options(self):
        super().finalize_options()
        if ABI3_BUILD:
            self.inplace = False

    def build_extension(self, ext):
        compiler = self.compiler
        if not hasattr(compiler, "_compile"):
            super().build_extension(ext)
            return

        base_compile = compiler._compile
        base_spawn = compiler.spawn

        def _compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts):
            if os.path.abspath(src).startswith(ztype + os.sep):
                extra_postargs = list(extra_postargs) + [EBCDIC_CHAR_MODE]
            return base_compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts)

        compiler._compile = _compile

        def spawn(command, **kwargs):
            result = base_spawn(command, **kwargs)
            record = os.environ.get("ZBIND_LINK_COMMANDS")
            if record and "-o" in command and command[command.index("-o") + 1].endswith(".abi3.so"):
                with open(record, "a", encoding="utf-8") as output:
                    output.write(json.dumps(command) + "\n")
            return result

        compiler.spawn = spawn
        try:
            super().build_extension(ext)
        finally:
            compiler._compile = base_compile
            compiler.spawn = base_spawn

zusf_py_module = Extension("_zusf_py",
                           sources=["zusf_py_wrap.cxx", "zusf_py.cpp",
                                    f"{C_PATH}/zusf.cpp", f"{C_PATH}/zut.cpp"],
                           language="c++",
                           include_dirs=[chdsect, ztype],
                           libraries=["zut"],
                           library_dirs=[build_out_path],
                           extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                           )

zds_py_module = Extension("_zds_py",
                          sources=["zds_py_wrap.cxx", "zds_py.cpp",
                                   f"{C_PATH}/zds.cpp", f"{C_PATH}/zut.cpp"],
                          language="c++",
                          extra_objects=[
                              f"{build_out_path}/zdsm.o",
                              f"{build_out_path}/zutm.o",
                              f"{build_out_path}/zam.o",
                              f"{build_out_path}/zam24.o",
                              f"{build_out_path}/zutm31.o",
                              f"{build_out_path}/zutcall24.o",
                          ],
                          include_dirs=[chdsect, ztype],
                          extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                          )

zjb_py_module = Extension("_zjb_py",
                          sources=["zjb_py_wrap.cxx", "zjb_py.cpp",
                                   f"{C_PATH}/zjb.cpp", f"{C_PATH}/zut.cpp", f"{C_PATH}/zds.cpp"],
                          language="c++",
                          extra_objects=[
                              f"{build_out_path}/zjbm.o",
                              f"{build_out_path}/zutm.o",
                              f"{build_out_path}/zutm31.o",
                              f"{build_out_path}/zam.o",
                              f"{build_out_path}/zdsm.o",
                              f"{build_out_path}/zam24.o",
                              f"{build_out_path}/zutcall24.o",
                          ],
                          include_dirs=[chdsect, ztype],
                          extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                          )

zkr_py_module = Extension("_zkr_py",
                          sources=["zkr_py_wrap.cxx", "zkr_py.cpp",
                                   f"{C_PATH}/zkr.cpp", f"{C_PATH}/zkrio.cpp",
                                   f"{C_PATH}/zds.cpp", f"{C_PATH}/zut.cpp"],
                          language="c++",
                          extra_objects=[
                              f"{build_out_path}/zdsm.o",
                              f"{build_out_path}/zutm.o",
                              f"{build_out_path}/zam.o",
                              f"{build_out_path}/zam24.o",
                              f"{build_out_path}/zutm31.o",
                              f"{build_out_path}/zutcall24.o",
                              GSKCMS_SIDEDECK,
                          ],
                          include_dirs=[chdsect, ztype],
                          extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                          )

# Parse environment variable for selective building


def get_modules_to_build():
    """Determine which modules to build based on ZBIND_MODULES environment variable."""
    modules_env = os.environ.get('ZBIND_MODULES', '')

    if modules_env:
        modules_to_build = set(modules_env.split(','))
        # Clean up any whitespace
        modules_to_build = {m.strip() for m in modules_to_build if m.strip()}
    else:
        # If no specific modules requested, build all
        modules_to_build = {'zusf', 'zds', 'zjb', 'zkr'}

    return modules_to_build


# Determine which modules to build
modules_to_build = get_modules_to_build()

# Select extensions and py_modules based on what's requested
ext_modules = []
py_modules = []

if 'zusf' in modules_to_build:
    ext_modules.append(zusf_py_module)
    py_modules.append("zusf_py")

if 'zds' in modules_to_build:
    ext_modules.append(zds_py_module)
    py_modules.append("zds_py")

if 'zjb' in modules_to_build:
    ext_modules.append(zjb_py_module)
    py_modules.append("zjb_py")

if 'zkr' in modules_to_build:
    ext_modules.append(zkr_py_module)
    py_modules.append("zkr_py")

print(f"Building modules: {', '.join(modules_to_build)}")

# Each DLL must use its own SWIG iterator proxy. On z/OS, routing an iterator
# through another DLL's proxy can leave its C++ stop_iteration exception uncaught.
for extension in ext_modules:
    extension.define_macros.append(("SWIG_TYPE_TABLE", extension.name))
    if ABI3_BUILD:
        extension.define_macros.append(("Py_LIMITED_API", "0x030B0000"))
        extension.py_limited_api = True

if ABI3_BUILD:
    if modules_to_build != {'zusf', 'zds', 'zjb', 'zkr'}:
        raise ValueError("The release wheel requires all four binding modules")
    # Embed the Metal C helpers, avoiding a runtime dependency on libzut.so.
    zusf_py_module.libraries = []
    zusf_py_module.library_dirs = []
    zusf_py_module.extra_objects = [
        f"{build_out_path}/{name}.o"
        for name in ("zutm", "zam", "zam24", "zutm31", "zutcall24")
    ]

setup(name="zbind",
      version="1.0.0",
      description="Zowe Remote SSH native Python bindings for z/OS",
      python_requires=">=3.11" if ABI3_BUILD else None,
      cmdclass={"build_ext": BuildExtMixedCharMode, "build_py": BuildPyUtf8},
      license="EPL-2.0",
      license_files=["../../../LICENSE"],
      options={"bdist_wheel": {"py_limited_api": "cp311"}, "build": build_options} if ABI3_BUILD else {},
      ext_modules=ext_modules,
      py_modules=py_modules,
      )
