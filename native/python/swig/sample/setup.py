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

from setuptools import setup, Extension

example_module = Extension('_example',
                           sources=['example_wrap.cxx', 'example.cpp'],
                           language='c++',
                           extra_objects=['hello.o', 'zmetal.o'],
                           )

setup (name = 'example',
       version = '0.1',
       author      = "Zowe",
       description = """SWIG example that invokes Metal C from Python""",
       ext_modules = [example_module],
       py_modules = ["example"],
       )
