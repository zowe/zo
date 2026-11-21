#!/usr/bin/env python3
"""Build one z/OS ABI3 wheel using only the Python 3.11 build runtime."""

import argparse
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import sysconfig
import tempfile
import zipfile

MODULES = ("zusf_py", "zds_py", "zjb_py", "zkr_py")


def parse_runtime(value):
    tag, separator, path = value.partition("=")
    if not separator or tag not in {"cp313", "cp314"} or not Path(path).is_file():
        raise argparse.ArgumentTypeError("Expected cp313/cp314=/existing/python/side-deck.x")
    return tag, str(Path(path).resolve())


def probe_runtime():
    return {"version": sysconfig.get_python_version(), "dll": sysconfig.get_config_var("LDLIBRARY")}


def decode_json(raw):
    return json.loads(raw.decode("utf-8" if raw.startswith(b"{") else "cp1047"))


def probe_build_runtime(config, env):
    """Only the baseline interpreter is required to build the ABI3 wheel."""
    executable = Path(config["python"]).resolve()
    if not executable.is_file():
        raise RuntimeError(f"Build Python not found: {executable}")
    result = subprocess.check_output([str(executable), str(Path(__file__).resolve()),
                                      "--probe-runtime"], env=env)
    runtime = decode_json(result)
    if runtime["version"] != "3.11":
        raise RuntimeError(f"Expected build Python 3.11 at {executable}, got {runtime['version']}")
    print("Wheel build runtime: 3.11 (ABI3; newer Python installations are not required)", flush=True)
    return runtime


def build_from_stdin():
    """Prepare an isolated build venv and invoke the normal Makefile wheel target."""
    config = decode_json(sys.stdin.buffer.read())
    bindings = Path(__file__).resolve().parent
    root = bindings.parents[2]
    env = os.environ.copy()
    if config.get("indexUrl"):
        env["PIP_INDEX_URL"] = config["indexUrl"]
    probe_build_runtime(config, env)
    venv = root / "build-venv"
    subprocess.run([sys.executable, "-m", "venv", str(venv)], env=env, check=True)
    python = str(venv / "bin/python")
    requirements = root / "requirements-utf8.txt"
    with requirements.open("wb") as output:
        subprocess.run(["iconv", "-f", "IBM-1047", "-t", "UTF-8",
                        str(bindings / "requirements-build.txt")], stdout=output, check=True)
    subprocess.run(["chtag", "-t", "-c", "UTF-8", str(requirements)], check=True)
    subprocess.run([python, "-m", "pip", "install", "--quiet", "-r", str(requirements)],
                   env=env, stdin=subprocess.DEVNULL, check=True)
    subprocess.run(["make", "wheel", "PYTHON=" + python],
                   cwd=bindings, env=env, stdin=subprocess.DEVNULL, check=True)


def wheel_platforms(override=None):
    if override is None:
        platform = sysconfig.get_platform().replace("-", "_").replace(".", "_")
        override = ".".join(sorted({platform, "zos"}))
    if not re.fullmatch(r"[a-z0-9_]+(?:\.[a-z0-9_]+)*", override):
        raise ValueError("Wheel platforms must be dot-separated normalized platform tags")
    return override


def assemble_wheel(base, release):
    from wheel.wheelfile import WheelFile

    platforms, outdir = wheel_platforms(release.platform), release.outdir
    distribution, version = base.name.split("-")[:2]
    filename = f"{distribution}-{version}-cp311-abi3-{platforms}.whl"
    outdir.mkdir(parents=True, exist_ok=True)
    wheel = base.parent.parent / filename
    with zipfile.ZipFile(base) as source, WheelFile(wheel, "w") as target:
        info = f"{distribution}-{version}.dist-info"
        for name in source.namelist():
            if name in {f"{module}.py" for module in MODULES} or name == f"{info}/METADATA":
                target.writestr(name, source.read(name))
        loader = subprocess.check_output(["iconv", "-f", "IBM-1047", "-t", "UTF-8",
                                          "_zbind_loader.py"]).decode("utf-8")
        library = sysconfig.get_config_var("LDLIBRARY")
        if not library or Path(library).name != library or loader.count("PYTHON_DLL = None") != 1:
            raise RuntimeError("Cannot record the linking interpreter's Python DLL name")
        loader = loader.replace("PYTHON_DLL = None", f"PYTHON_DLL = {library!r}", 1)
        target.writestr("_zbind_loader.py", loader.encode("utf-8"))
        for module in MODULES:
            target.writestr(f"_{module}.py",
                            b"from _zbind_loader import load_extension\nload_extension(__name__)\n")
            name = f"_{module}.abi3.so"
            entry = zipfile.ZipInfo(f"_zbind_native/{name}")
            entry.external_attr = (stat.S_IFREG | 0o755) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            target.writestr(entry, source.read(name))
        license_text = subprocess.check_output(["iconv", "-f", "IBM-1047", "-t", "UTF-8",
                                                str(release.license_file)])
        target.writestr(f"{info}/LICENSE", license_text)
        metadata = "Wheel-Version: 1.0\nGenerator: zo ABI3 builder\nRoot-Is-Purelib: false\n"
        for platform in platforms.split("."):
            metadata += f"Tag: cp311-abi3-{platform}\n"
        target.writestr(f"{info}/WHEEL", metadata.encode("utf-8"))
    subprocess.run(["chtag", "-b", str(wheel)], check=True)
    destination = outdir / filename
    wheel.replace(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--build-from-stdin", action="store_true",
                       help="Prepare the build from JSON settings on stdin (used by buildTools)")
    modes.add_argument("--probe-runtime", action="store_true",
                       help="Print this interpreter's runtime metadata as JSON")
    parser.add_argument("--outdir", default="dist")
    parser.add_argument("--license-file", type=Path, default=Path(__file__).resolve().parents[3] / "LICENSE")
    parser.add_argument("--runtime", type=parse_runtime, action="append", default=[],
                        help="Legacy option; additional side decks are no longer needed for ABI3")
    parser.add_argument("--platform",
                        help="Override the build-platform and zos tags; separate multiple tags with dots")
    args = parser.parse_args()
    if args.probe_runtime:
        print(json.dumps(probe_runtime()))
        return
    if sys.platform != "zos" or sys.version_info[:2] != (3, 11):
        parser.error("Build with IBM z/OS Python 3.11; newer versions are install targets")
    if args.build_from_stdin:
        build_from_stdin()
        return
    if not args.license_file.is_file():
        parser.error("License file is missing; use --license-file for a deployment without the repo root")
    if len(dict(args.runtime)) != len(args.runtime):
        parser.error("Each extra runtime must be unique")
    if args.runtime:
        print("Additional --runtime side decks are unnecessary for the ABI3 wheel", flush=True)
    bindings = Path(__file__).resolve().parent
    os.chdir(bindings)
    args.outdir = Path(args.outdir).resolve()
    args.license_file = args.license_file.resolve()
    env = os.environ.copy()
    env["ZBIND_ABI3"] = "1"
    env.pop("ZBIND_MODULES", None)
    with tempfile.TemporaryDirectory(prefix="wheel-", dir=bindings) as work:
        work = Path(work)
        env["ZBIND_BUILD_BASE"] = str(work / "build")
        subprocess.run([sys.executable, "-m", "build", "--wheel", "--no-isolation",
                        "--outdir", str(work / "dist")], cwd=bindings, env=env, check=True)
        base = next((work / "dist").glob("*.whl"))
        wheel = assemble_wheel(base, args)
        print(f"Created {wheel}")


if __name__ == "__main__":
    main()
