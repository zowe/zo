#!/usr/bin/env python3

"""
setup.py file for SWIG example
"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import os
import sys

C_PATH = "../../c"
chdsect = os.path.abspath(f"{C_PATH}/chdsect")
ztype = os.path.abspath(C_PATH)
build_out_path = f"{C_PATH}/build-out"

# These sources are shared with zo, which compiles them with the ibm-clang default EBCDIC
# execution charset. The SWIG's default CFLAGS -fzos-le-char-mode=ascii flip the charset and 
# silently reinterprets every string literal. This break all EBCDIC control blocks it builds
# and the Metal C routines it calls. Let's compile those translations in EBCDIC and leave the 
# SWIG wrappers in ASCII. The conversion.hpp should bridge the two.
EBCDIC_CHAR_MODE = "-fzos-le-char-mode=ebcdic"


class BuildExtMixedCharMode(build_ext):
    """Compiles the shared native/c sources EBCDIC and the SWIG wrappers ASCII."""

    def build_extension(self, ext):
        compiler = self.compiler
        if not hasattr(compiler, "_compile"):
            super().build_extension(ext)
            return

        base_compile = compiler._compile

        def _compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts):
            if os.path.abspath(src).startswith(ztype + os.sep):
                extra_postargs = list(extra_postargs) + [EBCDIC_CHAR_MODE]
            return base_compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts)

        compiler._compile = _compile
        try:
            super().build_extension(ext)
        finally:
            compiler._compile = base_compile

zusf_py_module = Extension("_zusf_py",
                           sources=["zusf_py_wrap.cxx", "zusf_py.cpp",
                                    f"{C_PATH}/zusf.cpp", f"{C_PATH}/zut.cpp"],
                           language="c++",
                           include_dirs=[chdsect],
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
        modules_to_build = {'zusf', 'zds', 'zjb'}

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

print(f"Building modules: {', '.join(modules_to_build)}")

setup(name="zbind",
      description="""Simple swig example""",
      cmdclass={"build_ext": BuildExtMixedCharMode},
      ext_modules=ext_modules,
      py_modules=py_modules,
      )
