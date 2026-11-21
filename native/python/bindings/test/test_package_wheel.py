"""Test baseline-interpreter discovery and ABI3 wheel assembly without z/OS."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location(
    "package_wheel", Path(__file__).resolve().parents[1] / "package_wheel.py"
)
package_wheel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_wheel)


class RuntimeSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.config = {"python": str(Path(self.temporary.name) / "python3.11")}
        Path(self.config["python"]).touch()
        self.probe = patch.object(package_wheel.subprocess, "check_output", side_effect=self.runtime_metadata)
        self.mock_probe = self.probe.start()
        self.addCleanup(self.probe.stop)

    @staticmethod
    def runtime_metadata(command, **kwargs):
        version = Path(command[0]).name.removeprefix("python")
        return package_wheel.json.dumps({"version": version, "deck": f"/lib/libpython{version}.x"}).encode()

    def test_only_the_build_interpreter_is_required(self):
        runtime = package_wheel.probe_build_runtime(self.config, {})
        self.assertEqual(runtime["version"], "3.11")
        self.mock_probe.assert_called_once()

    def test_ci_still_requires_python_311(self):
        Path(self.config["python"]).unlink()
        with self.assertRaisesRegex(RuntimeError, "Build Python not found"):
            package_wheel.probe_build_runtime(self.config, {})

    def test_ci_rejects_wrong_version(self):
        self.mock_probe.side_effect = None
        self.mock_probe.return_value = b'{"version": "3.14"}'
        with self.assertRaisesRegex(RuntimeError, "Expected build Python 3.11"):
            package_wheel.probe_build_runtime(self.config, {})

    def test_ci_propagates_probe_failure(self):
        self.mock_probe.side_effect = subprocess.CalledProcessError(1, "python3.11")
        with self.assertRaises(subprocess.CalledProcessError):
            package_wheel.probe_build_runtime(self.config, {})


class WheelAssemblyTests(unittest.TestCase):
    def test_single_native_copy_and_expanded_platform_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "dist").mkdir()
            base = root / "dist/zo-1.0.2-cp311-abi3-os390_29_00_3932.whl"
            with zipfile.ZipFile(base, "w") as archive:
                for module in package_wheel.MODULES:
                    archive.writestr(f"{module}.py", "# proxy\n")
                    archive.writestr(f"_{module}.abi3.so", module.encode())
                archive.writestr("zo-1.0.2.dist-info/METADATA", "Name: zo\nVersion: 1.0.2\n")
            options = SimpleNamespace(platform=None, outdir=root / "out", license_file=root / "LICENSE")
            loader = Path(package_wheel.__file__).with_name("_zbind_loader.py").read_bytes()
            with patch.object(package_wheel.sysconfig, "get_platform", return_value="os390-29.00-3932"), \
                    patch.object(package_wheel.sysconfig, "get_config_var", return_value="libpython3.11.so"), \
                    patch.object(package_wheel.subprocess, "check_output", side_effect=[loader, b"EPL-2.0"]), \
                    patch.object(package_wheel.subprocess, "run"):
                wheel = package_wheel.assemble_wheel(base, options)
            self.assertEqual(wheel.name, "zo-1.0.2-cp311-abi3-os390_29_00_3932.zos.whl")
            with zipfile.ZipFile(wheel) as archive:
                native = [name for name in archive.namelist() if name.endswith(".so")]
                self.assertEqual(len(native), 4)
                for module in package_wheel.MODULES:
                    name = f"_zbind_native/_{module}.abi3.so"
                    self.assertEqual(archive.read(name), module.encode())
                    self.assertTrue(archive.getinfo(name).external_attr >> 16 & 0o111)
                metadata = archive.read("zo-1.0.2.dist-info/WHEEL").decode()
                self.assertIn("Tag: cp311-abi3-os390_29_00_3932\n", metadata)
                self.assertIn("Tag: cp311-abi3-zos\n", metadata)
                self.assertIn(b"PYTHON_DLL = 'libpython3.11.so'", archive.read("_zbind_loader.py"))

    def test_invalid_platform_override_is_rejected(self):
        for platform in ("zos/../any", "zos-whatever", "zos..any"):
            with self.subTest(platform=platform), self.assertRaises(ValueError):
                package_wheel.wheel_platforms(platform)


if __name__ == "__main__":
    unittest.main()
