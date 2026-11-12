#!/usr/bin/env python3

import os
import subprocess
import sys

# Define paths relative to this script's directory
BINDINGS_DIR = os.path.dirname(os.path.abspath(__file__))
C_DIR = os.path.abspath(os.path.join(BINDINGS_DIR, "../../c"))
DIST_DIR = os.path.join(BINDINGS_DIR, "zbind_src_dist")
TARBALL_PATH = os.path.join(BINDINGS_DIR, "zbind_src_dist.tar.gz")

print(f"Bindings directory: {BINDINGS_DIR}")
print(f"C directory: {C_DIR}")
print(f"Target distribution directory: {DIST_DIR}")

# 1. Clean previous build directory
if os.path.exists(DIST_DIR):
    print("Cleaning previous dist directory...")
    subprocess.run(["rm", "-rf", DIST_DIR], check=True)
os.makedirs(DIST_DIR, exist_ok=True)

# 2. Copy bindings files and wrapper files using native cp (prevents C-runtime copy corruption)
bindings_files = [
    "zusf_py.cpp", "zusf_py.hpp", "zusf_py.py", "zusf_py_wrap.cxx",
    "zds_py.cpp", "zds_py.hpp", "zds_py.py", "zds_py_wrap.cxx",
    "zjb_py.cpp", "zjb_py.hpp", "zjb_py.py", "zjb_py_wrap.cxx",
    "conversion.hpp"
]

for filename in bindings_files:
    src = os.path.join(BINDINGS_DIR, filename)
    if os.path.exists(src):
        subprocess.run(["cp", src, DIST_DIR], check=True)
        print(f"Copied bindings file: {filename}")
        
        # Rewrite relative includes of core files (e.g. "../../c/zds.hpp" to "zds.hpp")
        # We check both ASCII and EBCDIC encodings of "../../c/" to be 100% compatible!
        dst_file = os.path.join(DIST_DIR, filename)
        if filename.endswith(".hpp") or filename.endswith(".cpp") or filename.endswith(".cxx"):
            with open(dst_file, "rb") as f:
                data = f.read()
            
            ascii_target = b"../../c/"
            ebcdic_target = b"\\x4b\\x4b\\x61\\x4b\\x4b\\x61\\x83\\x61".decode('unicode-escape').encode('latin-1')
            
            replaced = False
            if ascii_target in data:
                data = data.replace(ascii_target, b"")
                replaced = True
            if ebcdic_target in data:
                data = data.replace(ebcdic_target, b"")
                replaced = True
                
            if replaced:
                with open(dst_file, "wb") as f:
                    f.write(data)
                print(f"Rewrote relative includes in {filename}")
    else:
        print(f"Warning: bindings file not found: {filename}")

# 3. Copy C++ core sources
core_sources = ["zusf.cpp", "zds.cpp", "zjb.cpp", "zut.cpp"]
for filename in core_sources:
    src = os.path.join(C_DIR, filename)
    if os.path.exists(src):
        subprocess.run(["cp", src, DIST_DIR], check=True)
        print(f"Copied core source file: {filename}")
    else:
        print(f"Error: core source file not found: {filename}")

# 4. Copy ALL headers from C_DIR to ensure no missing dependencies
for filename in os.listdir(C_DIR):
    if filename.endswith(".h") or filename.endswith(".hpp"):
        src = os.path.join(C_DIR, filename)
        subprocess.run(["cp", src, DIST_DIR], check=True)
        print(f"Copied core header: {filename}")

# 5. Copy the entire chdsect directory using cp -R
chdsect_src = os.path.join(C_DIR, "chdsect")
if os.path.exists(chdsect_src):
    subprocess.run(["cp", "-R", chdsect_src, DIST_DIR], check=True)
    print("Copied chdsect directory structure.")
else:
    print("Error: chdsect directory not found!")

# 6. Copy precompiled Metal C object files using native cp
object_files = ["zdsm.o", "zutm.o", "zam.o", "zam24.o", "zutm31.o", "zjbm.o"]
build_out_dir = os.path.join(C_DIR, "build-out")
for filename in object_files:
    src = os.path.join(build_out_dir, filename)
    if os.path.exists(src):
        subprocess.run(["cp", src, DIST_DIR], check=True)
        print(f"Copied object file: {filename}")
    else:
        print(f"Error: precompiled object file not found: {filename}")
        sys.exit(1)

# 7. Write setup.py for the flat layout distribution
setup_py_content = """#!/usr/bin/env python3

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import os

chdsect = "chdsect"
ztype = "."

# These sources are shared with zo, which compiles them with the ibm-clang default EBCDIC
# execution charset. The SWIG's default CFLAGS -fzos-le-char-mode=ascii flip the charset and 
# silently reinterprets every string literal. This break all EBCDIC control blocks it builds
# and the Metal C routines it calls. Let's compile those translations in EBCDIC and leave the 
# SWIG wrappers in ASCII. The conversion.hpp should bridge the two.
EBCDIC_CHAR_MODE = "-fzos-le-char-mode=ebcdic"
CORE_SOURCES = {"zusf.cpp", "zds.cpp", "zjb.cpp", "zut.cpp"}


class BuildExtMixedCharMode(build_ext):
    \"\"\"Compiles the shared core sources EBCDIC and the SWIG wrappers ASCII.\"\"\"

    def build_extension(self, ext):
        compiler = self.compiler
        if not hasattr(compiler, "_compile"):
            super().build_extension(ext)
            return

        base_compile = compiler._compile

        def _compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts):
            if os.path.basename(src) in CORE_SOURCES:
                extra_postargs = list(extra_postargs) + [EBCDIC_CHAR_MODE]
            return base_compile(obj, src, src_ext, cc_args, extra_postargs, pp_opts)

        compiler._compile = _compile
        try:
            super().build_extension(ext)
        finally:
            compiler._compile = base_compile

zusf_py_module = Extension("_zusf_py",
                           sources=["zusf_py_wrap.cxx", "zusf_py.cpp", "zusf.cpp", "zut.cpp"],
                           language="c++",
                           extra_objects=[
                               "zdsm.o",
                               "zutm.o",
                               "zam.o",
                               "zam24.o",
                               "zutm31.o",
                           ],
                           include_dirs=[chdsect, ztype],
                           extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                           )

zds_py_module = Extension("_zds_py",
                          sources=["zds_py_wrap.cxx", "zds_py.cpp", "zds.cpp", "zut.cpp"],
                          language="c++",
                          extra_objects=[
                              "zdsm.o",
                              "zutm.o",
                              "zam.o",
                              "zam24.o",
                              "zutm31.o",
                          ],
                          include_dirs=[chdsect, ztype],
                          extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                          )

zjb_py_module = Extension("_zjb_py",
                          sources=["zjb_py_wrap.cxx", "zjb_py.cpp", "zjb.cpp", "zut.cpp", "zds.cpp"],
                          language="c++",
                          extra_objects=[
                              "zjbm.o",
                              "zutm.o",
                              "zutm31.o",
                              "zam.o",
                              "zdsm.o",
                              "zam24.o",
                          ],
                          include_dirs=[chdsect, ztype],
                          extra_compile_args=["-D_EXT", "-D_OPEN_SYS_FILE_EXT=1"],
                          )

setup(name="zbind",
      version="1.0.0",
      description="Zowe Remote SSH Python Bindings (Self-contained Bundle)",
      cmdclass={"build_ext": BuildExtMixedCharMode},
      ext_modules=[zusf_py_module, zds_py_module, zjb_py_module],
      py_modules=["zusf_py", "zds_py", "zjb_py"],
      )
"""

with open(os.path.join(DIST_DIR, "setup.py"), "w") as f:
    f.write(setup_py_content)
print("Wrote setup.py to bundle.")

# 8. Write setup.cfg to bundle
setup_cfg_content = """[build_ext]
define=SWIG
"""
with open(os.path.join(DIST_DIR, "setup.cfg"), "w") as f:
    f.write(setup_cfg_content)
print("Wrote setup.cfg to bundle.")

# 9. Write README.md to bundle
readme_content = """# Zowe Remote SSH Python Bindings Bundle (Source Distribution)

This bundle contains self-contained C++ bindings and precompiled Metal C object files for z/OS. 
It is system agnostic and does NOT require SWIG or complex dependencies to build.

## How to Install

You can build and install this bundle on any Python environment (e.g. Python 3.11, Python 3.12, etc.) using `pip`:

```bash
# Build and install in the current environment
pip install .
```

Alternatively, you can compile it in-place and use the `sys.path` insertion trick:

```bash
# Compile in-place
python setup.py build_ext --inplace

# Then, in your script, append this directory to sys.path:
# sys.path.insert(0, "/path/to/zbind_src_dist")
```
"""
with open(os.path.join(DIST_DIR, "README.md"), "w") as f:
    f.write(readme_content)
print("Wrote README.md to bundle.")

# 10. Correctly tag files inside the distribution directory
print("Applying file tagging for z/OS compatibility...")
# Tag Python files as ASCII
subprocess.run("chtag -t -c ISO8859-1 zbind_src_dist/*.py zbind_src_dist/*.cfg zbind_src_dist/*.md", shell=True, check=True, cwd=BINDINGS_DIR)
# Tag C++ files as IBM-1047 (EBCDIC)
subprocess.run("chtag -t -c IBM-1047 zbind_src_dist/*.cpp zbind_src_dist/*.hpp zbind_src_dist/*.h zbind_src_dist/*.cxx zbind_src_dist/chdsect/*.h", shell=True, check=True, cwd=BINDINGS_DIR)

# 11. Package everything in a tarball using native tar and gzip (prevents compression tagging bugs)
print(f"Creating tarball {TARBALL_PATH}...")
if os.path.exists(TARBALL_PATH):
    os.remove(TARBALL_PATH)

uncompressed_tar = TARBALL_PATH[:-3] # Removes .gz extension
if os.path.exists(uncompressed_tar):
    os.remove(uncompressed_tar)

# Run native tar
subprocess.run(f"tar -cf {uncompressed_tar} -C {BINDINGS_DIR} zbind_src_dist", shell=True, check=True, cwd=BINDINGS_DIR)

# Tag the raw tar archive as binary to disable C runtime autoconversion during Python's read
subprocess.run(f"chtag -b {uncompressed_tar}", shell=True, check=True, cwd=BINDINGS_DIR)

# Compress using Python's built-in gzip module
import gzip
with open(uncompressed_tar, 'rb') as f_in:
    with gzip.open(TARBALL_PATH, 'wb') as f_out:
        f_out.writelines(f_in)

# Remove uncompressed tar
os.remove(uncompressed_tar)

# Explicitly tag the tarball as binary to prevent any autoconversion issues during transfer
subprocess.run(f"chtag -b {TARBALL_PATH}", shell=True, check=True, cwd=BINDINGS_DIR)

print("Successfully packaged Python bindings bundle!")
print(f"Bundle path: {TARBALL_PATH}")
