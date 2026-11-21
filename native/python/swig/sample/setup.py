#!/usr/bin/env python3

"""
* This program and the accompanying materials are made available under the terms of the
* Eclipse Public License v2.0 which accompanies this distribution, and is available at
* https://www.eclipse.org/legal/epl-v20.html
*
* SPDX-License-Identifier: EPL-2.0
*
* Copyright Contributors to the Zowe Project.
"""

import os
from pathlib import Path
import sys
import sysconfig

from setuptools import setup, Extension

abi3 = os.environ.get("ABI3") == "1"
commands = {}

if abi3:
    from setuptools.command.build_py import build_py
    from wheel.bdist_wheel import bdist_wheel

    class Abi3BuildPy(build_py):
        def build_module(self, module, module_file, package):
            result = super().build_module(module, module_file, package)
            if module == "_example_runtime":
                source = Path(module_file).read_text()
                library = sysconfig.get_config_var("LDLIBRARY")
                if not library or Path(library).name != library:
                    raise RuntimeError("Expected a Python DLL filename in LDLIBRARY")
                Path(result[0]).write_text(source.replace("PYTHON_DLL = None", f"PYTHON_DLL = {library!r}", 1))
            return result

    class Abi3Wheel(bdist_wheel):
        def get_tag(self):
            python, abi, platform = super().get_tag()
            if sys.platform in ("zos", "os390"):
                platform = ".".join(sorted({platform, "zos"}))
            return python, abi, platform

    commands = {"build_py": Abi3BuildPy, "bdist_wheel": Abi3Wheel}

example_module = Extension('_example',
                           sources=['example_wrap.cxx', 'example.cpp'],
                           language='c++',
                           extra_objects=['hello.o', 'zmetal.o'],
                           define_macros=[('Py_LIMITED_API', '0x030B0000')] if abi3 else [],
                           py_limited_api=abi3,
                           )

setup (name = 'example',
       version = '0.1',
       author      = "Zowe",
       description = """SWIG example that invokes Metal C from Python""",
       ext_modules = [example_module],
       py_modules = ["example", "_example_runtime"],
       cmdclass = commands,
       **({'python_requires': '>=3.11'} if abi3 else {}),
       )
