#!/usr/bin/env python3
"""Build one z/OS wheel containing extensions linked for each supported runtime."""

import argparse
import itertools
import json
import os
from pathlib import Path
import shlex
import shutil
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


def python_side_deck_index(command):
    decks = [i for i, item in enumerate(command)
             if Path(item).name.startswith("libpython") and item.endswith(".x")]
    if len(decks) != 1:
        raise RuntimeError("Expected exactly one Python side deck in the link command")
    return decks[0]


def probe_runtime():
    # pip supplies the platform tag it will accept, which can differ from sysconfig.
    from pip._vendor.packaging.tags import sys_tags

    version = sysconfig.get_python_version()
    paths = [Path(sys.executable).resolve().parents[1] / "lib"]
    paths += [Path(sysconfig.get_config_var(key) or ".") for key in ("LIBDIR", "LIBPL")]
    side_deck = next((directory / f"libpython{version}.x" for directory in paths
                      if (directory / f"libpython{version}.x").is_file()), None)
    if side_deck is None:
        raise RuntimeError(f"Cannot find the Python {version} side deck")
    return {"version": version, "deck": str(side_deck), "platform": next(sys_tags()).platform}


def decode_json(raw):
    return json.loads(raw.decode("utf-8" if raw.startswith(b"{") else "cp1047"))


def build_from_stdin():
    """Prepare an isolated build venv and invoke the normal Makefile wheel target."""
    config = decode_json(sys.stdin.buffer.read())
    bindings = Path(__file__).resolve().parent
    root = bindings.parents[2]
    env = os.environ.copy()
    if config.get("indexUrl"):
        env["PIP_INDEX_URL"] = config["indexUrl"]
    runtimes = {}
    for minor in (11, 13, 14):
        executable = Path(config["runtimes"][str(minor)]).resolve()
        if not executable.is_file():
            raise RuntimeError(f"Python 3.{minor} not found: {executable}")
        result = subprocess.check_output([str(executable), str(Path(__file__).resolve()),
                                          "--probe-runtime"], env=env)
        runtime = decode_json(result)
        if runtime["version"] != f"3.{minor}":
            raise RuntimeError(f"Expected Python 3.{minor} at {executable}, got {runtime['version']}")
        runtimes[minor] = runtime
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
    platforms = ".".join(dict.fromkeys(runtime["platform"] for runtime in runtimes.values()))
    args = ["--platform", platforms]
    for minor in (13, 14):
        args += ["--runtime", f"cp3{minor}={runtimes[minor]['deck']}"]
    subprocess.run(["make", "wheel", "PYTHON=" + python, "WHEEL_ARGS=" + shlex.join(args)],
                   cwd=bindings, env=env, stdin=subprocess.DEVNULL, check=True)


def link_runtimes(commands, runtimes, destination):
    for tag, side_deck in runtimes.items():
        target = destination / tag
        target.mkdir(parents=True)
        for command in commands:
            output_index = command.index("-o") + 1
            output = target / Path(command[output_index]).name
            deck_index = python_side_deck_index(command)
            linked = list(command)
            linked[deck_index] = side_deck
            linked[output_index] = str(output)
            if side_deck == command[deck_index]:
                shutil.copy2(command[output_index], output)
            else:
                subprocess.run(linked, check=True)
            output.chmod(0o755)
            subprocess.run(["chtag", "-b", str(output)], check=True)


def assemble_wheel(base, native, release):
    from wheel.wheelfile import WheelFile

    runtimes, platforms, outdir = release.runtimes, release.platform, release.outdir
    tags = ".".join(sorted(runtimes))
    # Compressed tags form a Cartesian product. Normal CPython pip accepts only
    # its matching interpreter/ABI pair; free-threaded builds cannot match.
    distribution, version = base.name.split("-")[:2]
    filename = f"{distribution}-{version}-{tags}-{tags}-{platforms}.whl"
    outdir.mkdir(parents=True, exist_ok=True)
    wheel = native.parent / filename
    with zipfile.ZipFile(base) as source, WheelFile(wheel, "w") as target:
        info = f"{distribution}-{version}.dist-info"
        for name in source.namelist():
            if name in {f"{module}.py" for module in MODULES} or name == f"{info}/METADATA":
                target.writestr(name, source.read(name))
        loader = subprocess.check_output(["iconv", "-f", "IBM-1047", "-t", "UTF-8",
                                          "_zbind_loader.py"])
        target.writestr("_zbind_loader.py", loader)
        for module in MODULES:
            target.writestr(f"_{module}.py",
                            f"from _zbind_loader import load_extension\nload_extension(__name__)\n")
        for tag in runtimes:
            for module in MODULES:
                name = f"_{module}.abi3.so"
                target.write(native / tag / name, f"_zbind_native/{tag}/{name}")
        license_text = subprocess.check_output(["iconv", "-f", "IBM-1047", "-t", "UTF-8",
                                                str(release.license_file)])
        target.writestr(f"{info}/LICENSE", license_text)
        metadata = "Wheel-Version: 1.0\nGenerator: zbind multi-runtime builder\nRoot-Is-Purelib: false\n"
        for interpreter, abi, platform in itertools.product(sorted(runtimes), sorted(runtimes),
                                                             platforms.split(".")):
            metadata += f"Tag: {interpreter}-{abi}-{platform}\n"
        target.writestr(f"{info}/WHEEL", metadata)
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
                        help="Repeat for each additional runtime: cp313=/path/libpython3.13.x")
    parser.add_argument("--platform", default=sysconfig.get_platform().replace("-", "_").replace(".", "_"),
                        help="Verified pip platform tags, separated by dots")
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
        env["ZBIND_LINK_COMMANDS"] = str(work / "links.jsonl")
        subprocess.run([sys.executable, "-m", "build", "--wheel", "--no-isolation",
                        "--outdir", str(work / "dist")], cwd=bindings, env=env, check=True)
        commands = [json.loads(line) for line in (work / "links.jsonl").read_text(encoding="utf-8").splitlines()]
        if len(commands) != len(MODULES):
            raise RuntimeError("Expected a fresh link command for each of the four modules")
        base_deck = commands[0][python_side_deck_index(commands[0])]
        runtimes = {"cp311": base_deck, **dict(args.runtime)}
        args.runtimes = runtimes
        native = work / "native"
        link_runtimes(commands, runtimes, native)
        base = next((work / "dist").glob("*.whl"))
        wheel = assemble_wheel(base, native, args)
        print(f"Created {wheel}")


if __name__ == "__main__":
    main()
