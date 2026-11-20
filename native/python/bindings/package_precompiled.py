#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys

# Define paths relative to this script's directory
BINDINGS_DIR = os.path.dirname(os.path.abspath(__file__))
PRECOMPILED_DIR = os.path.join(BINDINGS_DIR, "zbind_bin_dist")
TARBALL_PATH = os.path.join(BINDINGS_DIR, "zbind_bin_dist.tar.gz")

print("==================================================")
print("Building precompiled Python bindings wheel-like bundle...")
print(f"Bindings directory: {BINDINGS_DIR}")
print(f"Target precompiled directory: {PRECOMPILED_DIR}")
print("==================================================")

# 1. Build the extensions in-place to get the compiled .so files
print("\n[Step 1] Building C++ extensions in-place...")
env = os.environ.copy()
env["CC"] = "ibm-clang64"
env["CXX"] = "ibm-clang++64"

# Clean any existing build artifacts first to ensure a fresh compile
if os.path.exists(os.path.join(BINDINGS_DIR, "build")):
    shutil.rmtree(os.path.join(BINDINGS_DIR, "build"))

# Run the build command
subprocess.run(["python", "setup.py", "build_ext", "--inplace"], cwd=BINDINGS_DIR, env=env, check=True)

# 2. Clean previous precompiled directory
if os.path.exists(PRECOMPILED_DIR):
    shutil.rmtree(PRECOMPILED_DIR)
os.makedirs(PRECOMPILED_DIR, exist_ok=True)

# 3. Copy the compiled .so files and Python wrapper modules
print("\n[Step 2] Packaging compiled binaries and Python files...")
py_files = ["zusf_py.py", "zds_py.py", "zjb_py.py"]

for filename in os.listdir(BINDINGS_DIR):
    # Find compiled .so libraries
    if filename.startswith("_") and filename.endswith(".so"):
        src = os.path.join(BINDINGS_DIR, filename)
        shutil.copy2(src, PRECOMPILED_DIR)
        print(f"Packaged precompiled library: {filename}")

for filename in py_files:
    src = os.path.join(BINDINGS_DIR, filename)
    if os.path.exists(src):
        shutil.copy2(src, PRECOMPILED_DIR)
        print(f"Packaged Python wrapper: {filename}")
    else:
        print(f"Error: Python wrapper not found: {filename}")
        sys.exit(1)

# 4. Create an __init__.py to allow importing as a standard python package
init_content = """# Zowe Remote SSH Python Bindings (Precompiled)

from .zusf_py import *
from .zds_py import *
from .zjb_py import *
"""
with open(os.path.join(PRECOMPILED_DIR, "__init__.py"), "w") as f:
    f.write(init_content)
print("Created __init__.py inside precompiled package.")

# 5. Create a helpful README.md inside the bundle
readme_content = """# Zowe Remote SSH Python Bindings (Precompiled)

This bundle contains precompiled z/OS C++ shared libraries and Python wrapper modules.
Your users **do not need a compiler, C++ headers, or SWIG** to use this! They only need Python.

## How to use this bundle:

### Option A: Insert directory to sys.path (Recommended for simple scripts)
Unpack this bundle and add the path of the `zbind_bin_dist` directory to `sys.path` in your Python script:

```python
import sys
import os

# Append the directory containing the files to sys.path
sys.path.insert(0, "/path/to/zbind_bin_dist")

# Now import the modules directly!
from zusf_py import list_uss_dir
from zjb_py import list_jobs_by_owner
from zds_py import list_data_sets
```

### Option B: Use as a Python Package
Alternatively, you can place the `zbind_bin_dist` folder directly inside your application's directory and import it:

```python
import zbind_bin_dist

# Access modules as sub-modules
print(zbind_bin_dist.zusf_py.list_uss_dir("/tmp"))
```
"""
with open(os.path.join(PRECOMPILED_DIR, "README.md"), "w") as f:
    f.write(readme_content)
print("Created README.md inside precompiled package.")

# 6. Apply precise file tagging for z/OS compatibility
print("\n[Step 3] Applying precise file tagging for z/OS...")
# Tag Python files as ASCII
subprocess.run("chtag -t -c ISO8859-1 zbind_bin_dist/*.py zbind_bin_dist/*.md", shell=True, check=True, cwd=BINDINGS_DIR)
# Tag .so binary files as binary
subprocess.run("chtag -b zbind_bin_dist/*.so", shell=True, check=True, cwd=BINDINGS_DIR)

# 7. Package everything in a tarball using native tar and python gzip
print(f"\n[Step 4] Creating tarball {TARBALL_PATH}...")
if os.path.exists(TARBALL_PATH):
    os.remove(TARBALL_PATH)

uncompressed_tar = TARBALL_PATH[:-3]
if os.path.exists(uncompressed_tar):
    os.remove(uncompressed_tar)

# Create raw tar archive
subprocess.run(f"tar -cf {uncompressed_tar} -C {BINDINGS_DIR} zbind_bin_dist", shell=True, check=True, cwd=BINDINGS_DIR)

# Tag tar archive as binary to disable C runtime autoconversion during read/write
subprocess.run(f"chtag -b {uncompressed_tar}", shell=True, check=True, cwd=BINDINGS_DIR)

# Compress using Python's built-in gzip module
import gzip
with open(uncompressed_tar, 'rb') as f_in:
    with gzip.open(TARBALL_PATH, 'wb') as f_out:
        f_out.writelines(f_in)

os.remove(uncompressed_tar)

# Explicitly tag the tarball as binary
subprocess.run(f"chtag -b {TARBALL_PATH}", shell=True, check=True, cwd=BINDINGS_DIR)

print("\n==================================================")
print("Successfully packaged precompiled Python bindings!")
print(f"Precompiled Bundle: {TARBALL_PATH}")
print("==================================================")
