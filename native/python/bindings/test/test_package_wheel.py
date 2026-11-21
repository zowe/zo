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
from package_metadata import PYTHON_MIN_VERSION, PYTHON_VERSION, PYTHON_TAG


class RuntimeSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.config = {"python": str(Path(self.temporary.name) / f"python{PYTHON_VERSION}")}
        Path(self.config["python"]).touch()
        self.probe = patch.object(package_wheel.subprocess, "check_output", side_effect=self.runtime_metadata)
        self.mock_probe = self.probe.start()
        self.addCleanup(self.probe.stop)

    @staticmethod
    def runtime_metadata(command, **kwargs):
        version = Path(command[0]).name.removeprefix("python")
        return package_wheel.json.dumps({"version": version, "dll": f"libpython{version}.so"}).encode()

    def test_only_the_build_interpreter_is_required(self):
        runtime = package_wheel.probe_build_runtime(self.config, {})
        self.assertEqual(runtime["version"], PYTHON_VERSION)
        self.mock_probe.assert_called_once()

    def test_ci_requires_the_selected_build_interpreter(self):
        Path(self.config["python"]).unlink()
        with self.assertRaisesRegex(RuntimeError, "Build Python not found"):
            package_wheel.probe_build_runtime(self.config, {})

    def test_ci_accepts_newer_build_python(self):
        self.mock_probe.side_effect = None
        major, minor = PYTHON_MIN_VERSION
        newer = f"{major}.{minor + 1}"
        self.mock_probe.return_value = package_wheel.json.dumps({"version": newer}).encode()
        self.assertEqual(package_wheel.probe_build_runtime(self.config, {})["version"], newer)

    def test_ci_rejects_older_build_python(self):
        self.mock_probe.side_effect = None
        major, minor = PYTHON_MIN_VERSION
        self.mock_probe.return_value = package_wheel.json.dumps({"version": f"{major}.{minor - 2}"}).encode()
        with self.assertRaisesRegex(RuntimeError, "Expected build Python"):
            package_wheel.probe_build_runtime(self.config, {})

    def test_ci_rejects_a_different_python_major_version(self):
        self.mock_probe.side_effect = None
        self.mock_probe.return_value = package_wheel.json.dumps({"version": f"{PYTHON_MIN_VERSION[0] + 1}.0"}).encode()
        with self.assertRaisesRegex(RuntimeError, "Expected build Python"):
            package_wheel.probe_build_runtime(self.config, {})

    def test_ci_propagates_probe_failure(self):
        self.mock_probe.side_effect = subprocess.CalledProcessError(1, self.config["python"])
        with self.assertRaises(subprocess.CalledProcessError):
            package_wheel.probe_build_runtime(self.config, {})


class WheelAssemblyTests(unittest.TestCase):
    def test_single_native_copy_and_expanded_platform_metadata(self):
        major, minor = PYTHON_MIN_VERSION
        for version in (f"{major}.{minor}", f"{major}.{minor + 2}", f"{major}.{minor + 3}"):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as directory:
                self.check_wheel_assembly(Path(directory), version)

    def check_wheel_assembly(self, root, version):
        (root / "dist").mkdir()
        base = root / f"dist/zo-1.0.2-{PYTHON_TAG}-abi3-os390_29_00_3932.whl"
        with zipfile.ZipFile(base, "w") as archive:
            for module in package_wheel.MODULES:
                archive.writestr(f"{module}.py", "# proxy\n")
                archive.writestr(f"_{module}.abi3.so", module.encode())
            archive.writestr("zo-1.0.2.dist-info/METADATA",
                             f"Name: zo\nVersion: 1.0.2\nRequires-Python: >={PYTHON_VERSION}\n")
        options = SimpleNamespace(platform=None, outdir=root / "out", license_file=root / "LICENSE")
        loader = Path(package_wheel.__file__).with_name("_zbind_loader.py").read_bytes()
        with patch.object(package_wheel.sysconfig, "get_platform", return_value="os390-29.00-3932"), \
                patch.object(package_wheel, "probe_runtime", return_value={"version": version, "dll": f"libpython{version}.so"}), \
                patch.object(package_wheel.subprocess, "check_output", side_effect=[loader, b"EPL-2.0"]), \
                patch.object(package_wheel.subprocess, "run"):
            wheel = package_wheel.assemble_wheel(base, options)
        self.assertEqual(wheel.name, f"zo-1.0.2-{PYTHON_TAG}-abi3-os390_29_00_3932.zos.whl")
        with zipfile.ZipFile(wheel) as archive:
            native = [name for name in archive.namelist() if name.endswith(".so")]
            self.assertEqual(len(native), 4)
            for module in package_wheel.MODULES:
                name = f"_zbind_native/_{module}.abi3.so"
                self.assertEqual(archive.read(name), module.encode())
                self.assertTrue(archive.getinfo(name).external_attr >> 16 & 0o111)
            metadata = archive.read("zo-1.0.2.dist-info/WHEEL").decode()
            self.assertIn(f"Tag: {PYTHON_TAG}-abi3-os390_29_00_3932\n", metadata)
            self.assertIn(f"Tag: {PYTHON_TAG}-abi3-zos\n", metadata)
            loader = archive.read("_zbind_loader.py").decode()
            self.assertIn(f"PYTHON_DLL = 'libpython{version}.so'", loader)
            self.assertIn(f"PYTHON_MIN_VERSION = {PYTHON_MIN_VERSION!r}", loader)
            self.assertIn(f"Requires-Python: >={PYTHON_VERSION}\n", archive.read("zo-1.0.2.dist-info/METADATA").decode())

    def test_cannot_relabel_a_wheel_with_a_different_abi3_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            major, minor = PYTHON_MIN_VERSION
            newer = f"{major}.{minor + 1}"
            base = root / f"zo-1.0.2-cp{major}{minor + 1}-abi3-zos.whl"
            options = SimpleNamespace(platform="zos", outdir=root / "out")
            with patch.object(package_wheel, "probe_runtime", return_value={"version": newer}):
                with self.assertRaisesRegex(RuntimeError, "must match the configured ABI3 baseline"):
                    package_wheel.assemble_wheel(base, options)
            self.assertFalse(options.outdir.exists())

    def test_invalid_platform_override_is_rejected(self):
        for platform in ("zos/../any", "zos-whatever", "zos..any"):
            with self.subTest(platform=platform), self.assertRaises(ValueError):
                package_wheel.wheel_platforms(platform)


if __name__ == "__main__":
    unittest.main()
