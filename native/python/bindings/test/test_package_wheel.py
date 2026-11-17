"""Test wheel runtime selection without a z/OS installation."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "package_wheel", Path(__file__).resolve().parents[1] / "package_wheel.py"
)
package_wheel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_wheel)


class RuntimeSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.config = {"runtimes": {
            str(minor): str(Path(self.temporary.name) / f"python3.{minor}")
            for minor in (11, 13, 14)
        }}
        self.add_runtime(11)
        self.probe = patch.object(package_wheel.subprocess, "check_output", side_effect=self.runtime_metadata)
        self.mock_probe = self.probe.start()
        self.addCleanup(self.probe.stop)

    def add_runtime(self, minor):
        Path(self.config["runtimes"][str(minor)]).touch()

    @staticmethod
    def runtime_metadata(command, **kwargs):
        version = Path(command[0]).name.removeprefix("python")
        return package_wheel.json.dumps({"version": version, "deck": f"/lib/libpython{version}.x"}).encode()

    def test_all_runtimes(self):
        self.add_runtime(13)
        self.add_runtime(14)
        runtimes = package_wheel.probe_runtimes(self.config, {})
        self.assertEqual(list(runtimes), [11, 13, 14])

    def test_default_requires_all_runtimes(self):
        with self.assertRaisesRegex(RuntimeError, "Python 3.13 not found"):
            package_wheel.probe_runtimes(self.config, {})

    def test_ci_uses_available_runtimes(self):
        self.config["allowMissingRuntimes"] = True
        for installed in (None, 13, 14):
            with self.subTest(installed=installed):
                if installed:
                    self.add_runtime(installed)
                expected = [11] if installed is None else [11, installed]
                self.assertEqual(list(package_wheel.probe_runtimes(self.config, {})), expected)
                if installed:
                    Path(self.config["runtimes"][str(installed)]).unlink()

    def test_ci_still_requires_python_311(self):
        self.config["allowMissingRuntimes"] = True
        Path(self.config["runtimes"]["11"]).unlink()
        with self.assertRaisesRegex(RuntimeError, "Python 3.11 not found"):
            package_wheel.probe_runtimes(self.config, {})

    def test_ci_rejects_wrong_version(self):
        self.config["allowMissingRuntimes"] = True
        self.add_runtime(13)
        self.mock_probe.side_effect = [
            self.runtime_metadata([self.config["runtimes"]["11"]]),
            b'{"version": "3.14", "deck": "/lib/libpython3.14.x"}',
        ]
        with self.assertRaisesRegex(RuntimeError, "Expected Python 3.13"):
            package_wheel.probe_runtimes(self.config, {})

    def test_ci_propagates_probe_failure(self):
        self.config["allowMissingRuntimes"] = True
        self.add_runtime(13)
        self.mock_probe.side_effect = [
            self.runtime_metadata([self.config["runtimes"]["11"]]),
            subprocess.CalledProcessError(1, "python3.13"),
        ]
        with self.assertRaises(subprocess.CalledProcessError):
            package_wheel.probe_runtimes(self.config, {})


if __name__ == "__main__":
    unittest.main()
