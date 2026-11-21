"""Check DLL alias cleanup, import failures, and concurrent binding imports."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import threading
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "zbind_loader_test", Path(__file__).resolve().parents[1] / "_zbind_loader.py"
)
loader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(loader)


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        root = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        self.root = root
        (root / "_zbind_native").mkdir()
        for name in loader._MODULES:
            (root / "_zbind_native" / f"{name}.abi3.so").touch()
        self.library = root / "base/lib/libpython3.13.so"
        self.library.parent.mkdir(parents=True)
        self.library.touch()
        self.aliases = []
        values = {"platlibdir": "lib", "LDLIBRARY": self.library.name, "Py_GIL_DISABLED": 0}
        self.stack.enter_context(patch.object(loader, "__file__", str(root / "_zbind_loader.py")))
        self.stack.enter_context(patch.object(loader, "PYTHON_DLL", "libpython3.11.so"))
        self.stack.enter_context(patch.object(sys, "platform", "zos"))
        self.stack.enter_context(patch.object(sys, "version_info", (3, 13)))
        self.stack.enter_context(patch.object(sys, "base_prefix", str(root / "base")))
        self.stack.enter_context(patch.object(loader.sysconfig, "get_config_var", side_effect=values.get))
        self.stack.enter_context(patch.dict(os.environ))
        for name in loader._MODULES:
            self.stack.enter_context(patch.dict(sys.modules, {name: None}))
        self.execute = self.inspect_import
        self.stack.enter_context(patch.object(loader.importlib.util, "spec_from_file_location", side_effect=self.native_spec))
        self.stack.enter_context(patch.object(loader.importlib.util, "module_from_spec", side_effect=self.native_module))

    def native_spec(self, name, path):
        return SimpleNamespace(name=name, origin=str(path), loader=SimpleNamespace(exec_module=lambda module: self.execute(module)))

    @staticmethod
    def native_module(spec):
        module = ModuleType(spec.name)
        module.__file__ = spec.origin
        return module

    def inspect_import(self, module):
        alias = Path(os.environ["LIBPATH"].split(":")[0]) / loader.PYTHON_DLL
        self.assertEqual(alias.resolve(), self.library)
        self.aliases.append(alias.parent)

    def test_alias_cleanup_and_environment_restoration(self):
        for previous in (None, "", "/existing/library"):
            with self.subTest(previous=previous):
                sys.modules.pop("_zds_py", None)
                if previous is None:
                    os.environ.pop("LIBPATH", None)
                else:
                    os.environ["LIBPATH"] = previous
                module = loader.load_extension("_zds_py")
                self.assertIs(sys.modules["_zds_py"], module)
                self.assertEqual(os.environ.get("LIBPATH"), previous)
                self.assertTrue(all(not path.exists() for path in self.aliases))

    def test_failed_import_restores_environment_without_caching_native_module(self):
        def fail(module):
            self.inspect_import(module)
            raise ImportError("native import failed")

        self.execute = fail
        os.environ["LIBPATH"] = "/existing/library"
        with self.assertRaisesRegex(ImportError, "native import failed"):
            loader.load_extension("_zds_py")
        self.assertEqual(os.environ["LIBPATH"], "/existing/library")
        self.assertIsNone(sys.modules["_zds_py"])
        self.assertTrue(all(not path.exists() for path in self.aliases))

    def test_concurrent_modules_do_not_replace_each_others_library_path(self):
        entered = threading.Event()
        release = threading.Event()
        second = threading.Event()

        def execute(module):
            self.inspect_import(module)
            if module.__name__ == "_zds_py":
                entered.set()
                self.assertTrue(release.wait(5))
                self.inspect_import(module)
            else:
                second.set()

        self.execute = execute
        os.environ["LIBPATH"] = "/existing/library"
        with ThreadPoolExecutor(max_workers=2) as executor:
            first = executor.submit(loader.load_extension, "_zds_py")
            try:
                self.assertTrue(entered.wait(5))
                other = executor.submit(loader.load_extension, "_zjb_py")
                self.assertFalse(second.wait(0.1))
            finally:
                release.set()
            first.result(timeout=5)
            other.result(timeout=5)
        self.assertEqual(os.environ["LIBPATH"], "/existing/library")
        self.assertTrue(all(not path.exists() for path in self.aliases))

    def test_unknown_module_is_rejected(self):
        with self.assertRaisesRegex(ImportError, "Unknown zo binding module"):
            loader.load_extension("../../other")

    def test_invalid_dll_metadata_is_rejected(self):
        for library in (None, "", ".", "..", "../libpython3.11.so", "/lib/libpython3.11.so"):
            with self.subTest(library=library), patch.object(loader, "PYTHON_DLL", library):
                with self.assertRaisesRegex(ImportError, "Python DLL build metadata"):
                    loader.load_extension("_zds_py")

    def test_free_threaded_interpreter_is_rejected(self):
        with patch.object(loader.sysconfig, "get_config_var", return_value=1):
            with self.assertRaisesRegex(ImportError, "GIL-enabled"):
                loader.load_extension("_zds_py")


if __name__ == "__main__":
    unittest.main()
