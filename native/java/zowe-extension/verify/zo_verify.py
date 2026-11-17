#!/usr/bin/env python3
"""Functional verification of a deployed zo-rest (0.6.0) through direct and Gateway paths.

Credentials are passed to curl as netrc files and never read here.
Usage: zo_verify.py <host> <service-port> <gateway-port> <netrc-user1> <netrc-user2> <expected-version>
"""
import json
import secrets
import subprocess
import sys
import tempfile

HOST, SVC, GW, N1, N2, VERSION = sys.argv[1:7]
DIRECT = f"https://{HOST}:{SVC}"
GATEWAY = f"https://{HOST}:{GW}/zo/api/v1"
# Fixture: a directory unique to this run, created exclusively (mode 700) and owned by user 1.
# Nothing outside it is written or deleted, so concurrent runs and pre-existing files are safe.
FIXTURE_DIR = f"/tmp/zo-verify-{secrets.token_hex(8)}"
TMP_FILE = f"{FIXTURE_DIR}/content.txt"
results = []


def curl(url, method="GET", netrc=None, body=None, headers=(), json_body=None):
    out = tempfile.NamedTemporaryFile(delete=False).name
    cmd = ["curl", "-sk", "-g", "--max-time", "240", "-o", out, "-w", "%{http_code}", "-X", method,
           "-H", "X-CSRF-ZOSMF-HEADER: true"]
    if netrc:
        cmd += ["--netrc-file", netrc]
    for h in headers:
        cmd += ["-H", h]
    if json_body is not None:
        cmd += ["-H", "Content-Type: application/json", "--data-binary", json.dumps(json_body)]
    elif body is not None:
        cmd += ["--data-binary", body]
    code = int(subprocess.run(cmd + [url], capture_output=True, text=True).stdout or 0)
    data = open(out, "rb").read()
    return code, data


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail and not ok else ""))


def js(data):
    try:
        return json.loads(data)
    except Exception:
        return {}


# --- A. info, health, gating -------------------------------------------------
for label, base in (("direct", DIRECT), ("gateway", GATEWAY)):
    code, _ = curl(f"{base}/zosmf/info")
    check(f"A0 {label}: /zosmf/info without credentials -> 401", code == 401, f"{code}")
    code, data = curl(f"{base}/zosmf/info", netrc=N1)
    info = js(data)
    plugin = (info.get("plugins") or [{}])[0]
    check(f"A1 {label}: /zosmf/info authenticated 200", code == 200, f"{code}")
    check(f"A2 {label}: plugin reports zo-rest;{VERSION}", plugin.get("pluginVersion") == f"zo-rest;{VERSION}",
          str(plugin))
    check(f"A3 {label}: plugin says it is zo, not z/OSMF", "this is zo, not z/OSMF" in plugin.get("pluginDefaultName", ""))
    check(f"A4 {label}: z/OSMF fields present",
          all(k in info for k in ("zos_version", "zosmf_version", "zosmf_hostname", "zosmf_port", "api_version")))
code, data = curl(f"{DIRECT}/application/health")
check("A5 health UP (includes native init)", code == 200 and js(data).get("status") == "UP", f"{code} {data[:80]}")
code, _ = curl(f"{DIRECT}/zo/api/v1/experimental/unix-command", "POST", N1, json_body={"command": "id"})
check("A6 experimental unixCommand not exposed (404)", code == 404, f"{code}")
code, _ = curl(f"{DIRECT}/zosmf/restfiles/ds?dslevel=SYS1")
check("A7 unauthenticated API call -> 401", code == 401, f"{code}")

# --- B. TSO ------------------------------------------------------------------
for label, base in (("direct", DIRECT), ("gateway", GATEWAY.replace("/zo/api/v1", ""))):
    tso = f"{base}/zo/api/v1/tso"
    code, data = curl(f"{tso}/command", "POST", N1, json_body={"command": "TIME"})
    r = js(data)
    check(f"B1 {label}: one-shot TIME rc 0", code == 200 and r.get("rc") == 0 and "IKJ56650I" in r.get("output", ""),
          f"{code} {data[:120]}")
code, data = curl(f"{DIRECT}/zo/api/v1/tso/command", "POST", N1, json_body={"command": "LISTCAT ENT('NO.SUCH.ZOVERIFY')"})
r = js(data)
check("B2 TSO failure returned as rc, HTTP 200", code == 200 and r.get("rc", 0) > 0, f"{code} {data[:160]}")
code, _ = curl(f"{DIRECT}/zo/api/v1/tso/command", "POST", N1, json_body={"command": ""})
check("B3 empty command -> 400", code == 400, f"{code}")

code, data = curl(f"{DIRECT}/zo/api/v1/tso/sessions", "POST", N1)
sid = js(data).get("sessionId")
check("B4 start session -> 201 with id", code == 201 and bool(sid), f"{code} {data[:120]}")
if sid:
    s = f"{DIRECT}/zo/api/v1/tso/sessions/{sid}"
    c1, d1 = curl(s, "PUT", N1, json_body={"command": "TIME"})
    c2, _ = curl(s, "PUT", N1, json_body={"command": "PROFILE"})
    check("B5 send x2 -> 200 with output", c1 == 200 and c2 == 200 and "output" in js(d1), f"{c1} {c2}")
    c, d = curl(s, "GET", N1)
    check("B6 receive returns 2 buffered results", c == 200 and len(js(d).get("results", [])) == 2, f"{c} {d[:120]}")
    c, d = curl(s, "GET", N1)
    check("B7 second receive is empty", c == 200 and js(d).get("results") == [], f"{c} {d[:80]}")
    c, _ = curl(s, "GET", N2)
    check("B8 other user cannot see the session (404)", c == 404, f"{c}")
    c, _ = curl(s, "PUT", N2, json_body={"command": "TIME"})
    check("B9 other user cannot send to the session (404)", c == 404, f"{c}")
    c, _ = curl(s, "DELETE", N1)
    check("B10 end session -> 204", c == 204, f"{c}")
    c, _ = curl(s, "GET", N1)
    check("B11 ended session -> 404", c == 404, f"{c}")
ids = []
codes = []
for _ in range(6):
    c, d = curl(f"{DIRECT}/zo/api/v1/tso/sessions", "POST", N1)
    codes.append(c)
    if c == 201:
        ids.append(js(d)["sessionId"])
check("B12 sixth session for one user -> 409", codes[:5] == [201] * 5 and codes[5] == 409, str(codes))
for i in ids:
    curl(f"{DIRECT}/zo/api/v1/tso/sessions/{i}", "DELETE", N1)

# --- C. content safety -----------------------------------------------------------
code, _ = curl(f"{DIRECT}/zosmf/restfiles/fs{FIXTURE_DIR}", "POST", N1, json_body={"type": "dir", "mode": "rwx------"})
if code != 201:
    check("C0 create private fixture directory (exclusive)", False, f"{code}; refusing to continue")
    print(f"\n{len([r for r in results if r[1]])}/{len(results)} passed")
    sys.exit(1)
check("C0 create private fixture directory (exclusive)", True)
f = f"{DIRECT}/zosmf/restfiles/fs{TMP_FILE}"
curl(f, "PUT", N1, body="original content\n", headers=("Content-Type: text/plain; charset=UTF-8",))
code, _ = curl(f, "PUT", N1, body="replacement\n",
               headers=("Content-Type: text/plain; charset=UTF-8", "X-IBM-Data-Type: text;fileEncoding=NOTREAL"))
check("C1 unsupported encoding -> 400", code == 400, f"{code}")
code, data = curl(f, "GET", N1)
check("C2 file unchanged after rejected write", code == 200 and data == b"original content\n", f"{code} {data[:60]}")
utf8 = "café\n".encode("utf-8")
curl(f, "PUT", N1, body=utf8, headers=("Content-Type: text/plain",))
code, data = curl(f, "GET", N1)
check("C3 charset-less upload uses ISO8859-1 (z/OSMF default)", data == "cafÃ©\n".encode("utf-8"), f"{data!r}")
curl(f, "PUT", N1, json_body={"request": "chmod", "mode": "600"})
code, _ = curl(f, "GET", N2)
check("C4 other user denied private file -> 403", code == 403, f"{code}")
code, _ = curl(f"{DIRECT}/zosmf/restfiles/fs{FIXTURE_DIR}", "DELETE", N1, headers=("X-IBM-Option: recursive",))
check("C5 cleanup of this run's fixture directory -> 204", code == 204, f"{code}")

failed = [r for r in results if not r[1]]
print(f"\n{len(results) - len(failed)}/{len(results)} passed")
sys.exit(1 if failed else 0)
