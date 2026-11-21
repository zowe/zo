"""Resolve the active Python DLL when importing the shared ABI3 bindings."""

import importlib.util
import os
from pathlib import Path
import sys
import sysconfig
import tempfile
import threading


# Filled in by package_wheel.py using the interpreter that links the extensions.
PYTHON_DLL = None
PYTHON_MIN_VERSION = None
_LOAD_LOCK = threading.RLock()
_MODULES = {"_zusf_py", "_zds_py", "_zjb_py", "_zkr_py"}


def load_extension(name):
    if (sys.platform not in ("zos", "os390") or sys.implementation.name != "cpython"
            or sysconfig.get_config_var("Py_GIL_DISABLED")):
        raise ImportError("zo requires a GIL-enabled IBM CPython runtime on z/OS")
    if name not in _MODULES:
        raise ImportError(f"Unknown zo binding module: {name}")
    if (not PYTHON_DLL or PYTHON_DLL in (".", "..") or Path(PYTHON_DLL).name != PYTHON_DLL
            or PYTHON_MIN_VERSION is None):
        raise ImportError("This zo wheel is missing its Python DLL build metadata")
    if sys.version_info[:2] < PYTHON_MIN_VERSION:
        minimum = ".".join(map(str, PYTHON_MIN_VERSION))
        raise ImportError(f"This zo wheel requires CPython {minimum} or later")
    path = Path(__file__).resolve().parent / "_zbind_native" / f"{name}.abi3.so"
    if not path.is_file():
        raise ImportError(f"This zo wheel is missing {path.name}")
    library = (Path(sys.base_prefix) / sysconfig.get_config_var("platlibdir")
               / sysconfig.get_config_var("LDLIBRARY")).resolve(strict=True)
    with _LOAD_LOCK:
        existing = sys.modules.get(name)
        if existing is not None and getattr(existing, "__file__", None) == str(path):
            return existing
        with tempfile.TemporaryDirectory(prefix="zo-abi3-") as directory:
            (Path(directory) / PYTHON_DLL).symlink_to(library)
            previous = os.environ.get("LIBPATH")
            os.environ["LIBPATH"] = directory + (":" + previous if previous else "")
            try:
                spec = importlib.util.spec_from_file_location(name, path)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                sys.modules[name] = module
                return module
            finally:
                if previous is None:
                    os.environ.pop("LIBPATH", None)
                else:
                    os.environ["LIBPATH"] = previous
