#!/usr/bin/env python3
"""Install a wheel in a fresh venv and test it away from the source package."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import uuid


def run_logged(command, work, env):
    with (work / "setup.log").open("ab") as log:
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wheel", type=Path)
    parser.add_argument("--functional", action="store_true", help="Include dataset, job and key-ring lifecycle tests")
    parser.add_argument("--owner", help="z/OS user ID for functional test resources")
    parser.add_argument("--keep-venv", action="store_true", help="Preserve the installed test environment")
    args = parser.parse_args()
    if sys.platform != "zos":
        parser.error("Run with the target IBM Python interpreter on z/OS")
    if not args.wheel.is_file():
        parser.error("Wheel file does not exist")
    if args.functional and (not args.owner or not re.fullmatch(r"[A-Z@$#][A-Z0-9@$#]{0,7}", args.owner)):
        parser.error("Functional tests require --owner with an uppercase z/OS user ID")
    wheel = args.wheel.resolve()
    work = Path(tempfile.mkdtemp(prefix="zbind-wheel-tests-", dir="/tmp"))
    print(f"Validation artifacts: {work}", flush=True)
    env = os.environ.copy()
    env.pop("PYTHONPATH", None)
    executable = os.path.realpath(sys.executable)
    run_logged([executable, "-m", "venv", str(work / "venv")], work, env)
    python = str(work / "venv/bin/python")
    run_logged([python, "-m", "pip", "install", "--quiet", "--no-index", str(wheel)], work, env)
    run_logged([python, "-m", "pip", "install", "--quiet", "pytest==8.4.1", "pyyaml==6.0.2"], work, env)
    tests = work / "tests"
    tests.mkdir()
    sources = Path(__file__).resolve().parent
    for source in sources.glob("test_*.py"):
        if source.stem.endswith("_utf8"):
            continue
        target = tests / source.name
        with target.open("wb") as output:
            subprocess.run(["iconv", "-f", "IBM-1047", "-t", "UTF-8", str(source)], stdout=output, check=True)
        subprocess.run(["chtag", "-t", "-c", "UTF-8", str(target)], check=True)
    selected = ["test_wheel_installation.py", "test_swig_lifetimes.py"]
    if args.functional:
        prefix = "W" + uuid.uuid4().hex[:7].upper()
        uss = work / "uss"
        uss.mkdir()
        fixtures = tests / "fixtures"
        fixtures.mkdir()
        fixture = fixtures / "env.yml"
        fixture.write_text(f"OWNER: {args.owner}\nDSN_PREFIX: {args.owner}.{prefix}\n"
                           f"USS_BASE_DIR: {uss}\nKEYRING_PREFIX: {prefix}\n", encoding="utf-8")
        subprocess.run(["chtag", "-t", "-c", "UTF-8", str(fixture)], check=True)
        # Verify absence before allowing the suites to create/delete scratch data.
        probe = '''import sys, zds_py
try:
    matches = zds_py.list_data_sets(sys.argv[1] + ".*")
except RuntimeError as error:
    if str(error) != "Not found in any catalog":
        raise
    matches = []
assert not matches, "Scratch dataset prefix already exists"
'''
        subprocess.run([python, "-c", probe, f"{args.owner}.{prefix}"], cwd=tests, env=env, check=True)
        selected += ["test_zds.py", "test_zjb.py", "test_zusf.py", "test_zkr.py"]
    with (work / "tests.log").open("wb") as output:
        result = subprocess.run([python, "-m", "pytest", "-q", *selected,
                                 f"--basetemp={work / 'pytest-temp'}",
                                 f"--junitxml={work / 'results.xml'}"],
                                cwd=tests, env=env, stdout=output, stderr=subprocess.STDOUT)
    # pip output stays in setup.log so configured index credentials are not echoed.
    lines = (work / "tests.log").read_text(encoding="utf-8", errors="replace").splitlines()
    print("\n".join(lines[-20:]))
    if result.returncode:
        raise SystemExit(result.returncode)
    run_logged([python, "-m", "pip", "uninstall", "--quiet", "-y", "zbind"], work, env)
    run_logged([python, "-m", "pip", "install", "--quiet", "--no-index", str(wheel)], work, env)
    subprocess.run([python, "-c", "import zds_py, zjb_py, zusf_py, zkr_py"], cwd=tests, env=env, check=True)
    if not args.keep_venv:
        shutil.rmtree(work / "venv")


if __name__ == "__main__":
    main()
