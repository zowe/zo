"""Select the bundled extension linked to the active IBM Python runtime."""

import importlib.util
from pathlib import Path
import sys
import sysconfig


def load_extension(name):
    if sys.platform != "zos" or sys.implementation.name != "cpython" or sysconfig.get_config_var("Py_GIL_DISABLED"):
        raise ImportError("zbind requires a GIL-enabled IBM CPython runtime on z/OS")
    tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    path = Path(__file__).resolve().parent / "_zbind_native" / tag / f"{name}.abi3.so"
    if not path.is_file():
        raise ImportError(f"This zbind wheel does not include a native build for {tag}")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.modules[name] = module
