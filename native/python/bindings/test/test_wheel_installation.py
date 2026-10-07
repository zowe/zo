"""Checks that a wheel imports its matching native builds from the active venv.

Run from a staged test directory containing no binding sources or binaries.
"""

import importlib
from pathlib import Path
import sys

import pytest


@pytest.mark.parametrize("name", ["zusf_py", "zds_py", "zjb_py", "zkr_py"])
def test_installed_module_locations(name):
    proxy = importlib.import_module(name)
    native = importlib.import_module(f"_{name}")
    environment = Path(sys.prefix).resolve()
    assert sys.prefix != sys.base_prefix, "Run wheel checks in a fresh venv"
    assert Path(proxy.__file__).resolve().is_relative_to(environment)
    assert Path(native.__file__).resolve().is_relative_to(environment)
    assert Path(native.__file__).parent.name == f"cp{sys.version_info.major}{sys.version_info.minor}"
    assert Path(native.__file__).name == f"_{name}.abi3.so"


def test_uss_read_from_installed_wheel(tmp_path):
    import zusf_py

    sample = tmp_path / "wheel-utf8.txt"
    sample.write_text("Wheel encoding: caf\u00e9\n", encoding="utf-8")
    assert zusf_py.read_uss_file(str(sample), "UTF-8") == "Wheel encoding: caf\u00e9\n"
