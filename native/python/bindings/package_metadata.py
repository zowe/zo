"""Release metadata and ABI3 build baseline for the Python bindings."""

import json
from pathlib import Path

PYTHON_MIN_VERSION = (3, 11)
PYTHON_VERSION = ".".join(map(str, PYTHON_MIN_VERSION))
PYTHON_TAG = "cp" + "".join(map(str, PYTHON_MIN_VERSION))
PY_LIMITED_API = f"0x{PYTHON_MIN_VERSION[0]:02X}{PYTHON_MIN_VERSION[1]:02X}0000"


def validate_build_python(version):
    build_version = tuple(map(int, version.split(".")))
    if build_version[0] != PYTHON_MIN_VERSION[0] or build_version < PYTHON_MIN_VERSION:
        raise RuntimeError(
            f"Expected build Python {PYTHON_VERSION} or later (Python {PYTHON_MIN_VERSION[0]}.x), got {version}")


def get_version():
    bindings = Path(__file__).resolve().parent
    # Deployments place package.json beside c/ and python/; checkouts keep it at the repo root.
    for root in (bindings.parents[1], bindings.parents[2]):
        manifest = root / "package.json"
        if manifest.is_file():
            raw = manifest.read_bytes()
            metadata = json.loads(raw.decode("utf-8" if raw.lstrip().startswith(b"{") else "cp1047"))
            version = metadata.get("version")
            if not isinstance(version, str) or not version.strip():
                raise ValueError(f"Missing release version in {manifest}")
            return version
    raise FileNotFoundError("Cannot find package.json; deploy it alongside the c/ and python/ directories")
