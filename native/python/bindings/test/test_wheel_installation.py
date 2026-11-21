"""Check the shared ABI3 bindings installed in the active venv.

Run from a staged test directory containing no binding sources or binaries.
"""

from email.parser import Parser
import importlib
from importlib.metadata import distribution
import os
from pathlib import Path
import subprocess
import sys
import sysconfig

import pytest


def test_installed_wheel_metadata():
    installed = distribution("zo")
    requires_python = installed.metadata["Requires-Python"]
    assert requires_python and requires_python.startswith(">=")
    minimum = requires_python[2:]
    python_tag = "cp" + minimum.replace(".", "")
    metadata = Parser().parsestr(installed.read_text("WHEEL"))
    tags = metadata.get_all("Tag")
    assert all(tag.startswith(f"{python_tag}-abi3-") for tag in tags)
    assert f"{python_tag}-abi3-zos" in tags
    platform = sysconfig.get_platform().replace("-", "_").replace(".", "_")
    assert f"{python_tag}-abi3-{platform}" in tags
    import _zbind_loader
    assert _zbind_loader.PYTHON_MIN_VERSION == tuple(map(int, minimum.split(".")))
    assert metadata["Root-Is-Purelib"] == "false"
    files = distribution("zo").files
    native = [file for file in files if str(file).endswith(".so")]
    assert len(native) == 4
    assert all(file.parent.name == "_zbind_native" for file in native)


@pytest.mark.parametrize("name", ["zusf_py", "zds_py", "zjb_py", "zkr_py"])
def test_installed_module_locations(name):
    previous = os.environ.get("LIBPATH")
    proxy = importlib.import_module(name)
    native = importlib.import_module(f"_{name}")
    assert os.environ.get("LIBPATH") == previous
    environment = Path(sys.prefix).resolve()
    assert sys.prefix != sys.base_prefix, "Run wheel checks in a fresh venv"
    assert Path(proxy.__file__).resolve().is_relative_to(environment)
    assert Path(native.__file__).resolve().is_relative_to(environment)
    assert Path(native.__file__).parent.name == "_zbind_native"
    assert Path(native.__file__).name == f"_{name}.abi3.so"


def test_uss_read_from_installed_wheel(tmp_path):
    import zusf_py

    sample = tmp_path / "wheel-utf8.txt"
    sample.write_text("Wheel encoding: caf\u00e9\n", encoding="utf-8")
    assert zusf_py.read_uss_file(str(sample), "UTF-8") == "Wheel encoding: caf\u00e9\n"


@pytest.mark.parametrize("parallel", [False, True])
def test_fresh_imports_restore_library_path(tmp_path, parallel):
    code = '''
from concurrent.futures import ThreadPoolExecutor
import importlib, os, sys
previous = os.environ.get("LIBPATH")
names = ("zds_py", "zjb_py", "zusf_py", "zkr_py")
if sys.argv[1] == "parallel":
    with ThreadPoolExecutor(max_workers=4) as executor:
        modules = list(executor.map(importlib.import_module, names))
else:
    modules = [importlib.import_module(name) for name in names]
assert len(modules) == 4
assert os.environ.get("LIBPATH") == previous
'''
    subprocess.run([sys.executable, "-I", "-c", code, "parallel" if parallel else "serial"],
                   cwd=tmp_path, check=True, timeout=60)
