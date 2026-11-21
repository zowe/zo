#!/usr/bin/env python3
"""
This program and the accompanying materials are made available under the terms of the
Eclipse Public License v2.0 which accompanies this distribution, and is available at
https://www.eclipse.org/legal/epl-v20.html

SPDX-License-Identifier: EPL-2.0

Copyright Contributors to the Zowe Project.
"""

import argparse
import hashlib
import importlib.machinery
import json
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile
import zipfile

from pip._vendor.packaging.tags import sys_tags
from pip._vendor.packaging.utils import parse_wheel_filename


def main():
    parser = argparse.ArgumentParser(description="Probe Python or install and test the sample abi3 wheel.")
    parser.add_argument("wheel", nargs="?", type=Path)
    parser.add_argument(
        "--load-only", action="store_true",
        help="Extract and load the unchanged extension without pip's platform-tag check (diagnostic only)",
    )
    args = parser.parse_args()
    if args.load_only and args.wheel is None:
        parser.error("--load-only requires a wheel")
    tags = set(sys_tags())
    print(json.dumps({
        "python": sys.version,
        "executable": sys.executable,
        "platform": sysconfig.get_platform(),
        "extension_suffixes": importlib.machinery.EXTENSION_SUFFIXES,
        "config": {key: sysconfig.get_config_var(key) for key in (
            "CC", "CXX", "LDSHARED", "LIBDIR", "LDLIBRARY", "INSTSONAME", "Py_GIL_DISABLED",
        )},
        "cp311_abi3_tags": sorted(str(tag) for tag in tags
                                 if tag.interpreter == "cp311" and tag.abi == "abi3"),
    }, indent=2), flush=True)
    if args.wheel is None:
        return

    wheel = args.wheel.resolve(strict=True)
    _, _, _, wheel_tags = parse_wheel_filename(wheel.name)
    if not all(tag.interpreter == "cp311" and tag.abi == "abi3" for tag in wheel_tags):
        raise RuntimeError("Expected a cp311-abi3 wheel")
    if not args.load_only and not tags.intersection(wheel_tags):
        raise RuntimeError("This interpreter does not accept the wheel's platform/ABI tags")
    with zipfile.ZipFile(wheel) as archive:
        if "_example.abi3.so" not in archive.namelist() or "example.py" not in archive.namelist():
            raise RuntimeError("Wheel must contain example.py and _example.abi3.so")
        metadata = [name for name in archive.namelist() if name.endswith(".dist-info/WHEEL")]
        if len(metadata) != 1:
            raise RuntimeError("Expected one WHEEL metadata file")
        contents = archive.read(metadata[0]).decode("utf-8")
        declared_tags = {line[5:] for line in contents.splitlines() if line.startswith("Tag: ")}
        if declared_tags != {str(tag) for tag in wheel_tags}:
            raise RuntimeError("Wheel filename and internal tags differ")
    print("wheel:", wheel, flush=True)
    print("sha256:", hashlib.sha256(wheel.read_bytes()).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="swig-abi3-") as temporary:
        target = str(Path(temporary) / "site")
        if args.load_only:
            print("DIAGNOSTIC: bypassing pip installation and wheel platform compatibility", flush=True)
            Path(target).mkdir()
            with zipfile.ZipFile(wheel) as archive:
                for name in ("example.py", "_example.abi3.so", "_example_runtime.py"):
                    if name in archive.namelist():
                        (Path(target) / name).write_bytes(archive.read(name))
            extension = str(Path(target) / "_example.abi3.so")
            os.chmod(extension, 0o755)
            if sys.platform in ("zos", "os390"):
                subprocess.run(["chtag", "-b", extension], check=True)
        else:
            subprocess.run([
                sys.executable, "-m", "pip", "install", "--no-index", "--no-deps", "--no-compile",
                "--target", target, str(wheel),
            ], cwd=temporary, check=True)
        subprocess.run([
            sys.executable, "-I", "-c",
            "import os, pathlib, sys; sys.path.insert(0, sys.argv[1]); "
            "previous_libpath = os.environ.get('LIBPATH'); "
            "import example, _example; "
            "assert os.environ.get('LIBPATH') == previous_libpath; "
            "assert pathlib.Path(_example.__file__).resolve().parent == pathlib.Path(sys.argv[1]).resolve(); "
            "assert _example.__file__.endswith('.abi3.so'); "
            "assert example.hello_ascii('Zowe') == 'Hello, Zowe!'; "
            "assert example.hello_ascii('Python') == 'Hello, Python!'; "
            "print('PASS:', _example.__file__)", target,
        ], cwd=temporary, check=True)


if __name__ == "__main__":
    main()
