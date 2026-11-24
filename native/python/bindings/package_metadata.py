"""Read the release version shared by zo and its Python bindings."""

import json
from pathlib import Path


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
