"""
This program and the accompanying materials are made available under the terms of the
Eclipse Public License v2.0 which accompanies this distribution, and is available at
https://www.eclipse.org/legal/epl-v20.html

SPDX-License-Identifier: EPL-2.0

Copyright Contributors to the Zowe Project.
"""

import importlib
import importlib.util
import os
from pathlib import Path
import sys
import sysconfig
import tempfile


# Filled in by the abi3 build from the interpreter used to link the extension.
PYTHON_DLL = None


def load_native():
    if sys.platform not in ("zos", "os390") or "_example" in sys.modules:
        return importlib.import_module("_example")
    spec = importlib.util.find_spec("_example")
    if spec is None or not spec.origin or not spec.origin.endswith(".abi3.so"):
        return importlib.import_module("_example")
    if not PYTHON_DLL:
        raise ImportError("Build the abi3 wheel before importing this extension")
    if sysconfig.get_config_var("Py_GIL_DISABLED"):
        raise ImportError("This abi3 extension requires GIL-enabled CPython")

    library = (Path(sys.base_prefix) / sysconfig.get_config_var("platlibdir")
               / sysconfig.get_config_var("LDLIBRARY")).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="example-abi3-") as directory:
        (Path(directory) / PYTHON_DLL).symlink_to(library)
        previous = os.environ.get("LIBPATH")
        os.environ["LIBPATH"] = directory + (":" + previous if previous else "")
        try:
            return importlib.import_module("_example")
        finally:
            if previous is None:
                os.environ.pop("LIBPATH", None)
            else:
                os.environ["LIBPATH"] = previous
