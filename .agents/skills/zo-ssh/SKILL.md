---
name: zo-ssh
description: Deploy and operate the zo JSON-RPC server on a *remote* z/OS host over SSH, for end-user dataset/job/USS operations when that host has no working z/OSMF endpoint. Use when the user asks to "connect to <host> over ssh", "deploy zo to <lpar>", or "use zo on <lpar>" to work with a remote system's datasets, jobs, or USS files. Not for building/testing this repo's own native code — that uses `npm run z:rebuild` / `npm run z:test`.
---

# zo over SSH — deploy and operate

This skill is an alternative to the `zowe` CLI (z/OSMF REST): it uses the **zo** native backend driven over SSH stdio instead. Use it when the target z/OS system has SSH but no z/OSMF. It's for operating on a remote target host's datasets/jobs/USS — unrelated to building or testing this repo's own native code, which uses `npm run z:rebuild` / `npm run z:test`.

**Two helpers, one command grammar.** Pick the one for your platform — they take the same subcommands, flags, env vars, and config keys:

| platform | helper | invoke as |
|---|---|---|
| macOS / Linux | [zx](zx) (bash) | `.agents/skills/zo-ssh/zx ds list "SYS1.*"` |
| Windows | [zx.ps1](zx.ps1) (Windows PowerShell 5.1+) | `.agents\skills\zo-ssh\zx.ps1 ds list "SYS1.*"` |

Everything below is written with the bash helper; on Windows substitute `zx.ps1` (or install the `zx.cmd` shim — §0) and PowerShell variable syntax. **Windows-specific behavior and gotchas are collected in §8 — read that section before your first call on Windows.**

Everything below uses two shell variables you should set once per target:

```bash
ZX_HOST=user@host          # ssh target (z/OS USS)
ZX_DIR=/u/$USER/zowex      # remote deploy dir; pick anything the user can write
```

If the user gives only a hostname, ask for (or infer) the SSH user and a writable USS directory.

**If you are an LLM/agent running this skill, always set your own unique `ZX_STATE` first — never rely on the default.** All `zx` state (config, persistent-session pid, FIFOs, `ControlMaster` socket) lives under `$ZX_STATE`, which defaults to one shared directory per UID (`/tmp/zx.$UID`). That default is meant for a human's interactive shell. If an agent run uses it too, it collides with any persistent session the user has open — `zx use`/`zx deploy` overwrites their saved host config, `zx start`/`zx stop`/`zx reset` clobbers their session, and vice versa. It also lets two concurrent agent runs stomp on each other the same way.

```bash
export ZX_STATE=/tmp/zx-agent-$$.$UID   # unique per agent run; keep it short (see §6 ControlPath note)
```

```powershell
$env:ZX_STATE = "$env:TEMP\zx-agent-$PID"   # same rule on Windows (default is %TEMP%\zx.%USERNAME%)
```

Do this at the very start of every session, before the first `zx`/`$zx` call — deploy included. Never call `zx reset` under a `ZX_STATE` a human or another agent might be using (see §7).

**Local prereqs (bash):** `ssh`, `sftp`, `jq`, `base64`, bash ≥3.2, `curl` or `wget`, and `sha256sum`/`shasum`/`openssl` (bundle verification). Run `.agents/skills/zo-ssh/zx check` to verify. (`jq` is the only one not stock on macOS — if it's missing, ask the user to install it via their package manager, e.g. Homebrew on macOS, before continuing.)
**Local prereqs (Windows):** `ssh` + `sftp` (the Windows OpenSSH Client feature) and Windows PowerShell 5.1 or newer. No `jq`/`base64`/`curl` needed — `zx.ps1` does JSON, base64, and downloads in-process. Run `.agents\skills\zo-ssh\zx.ps1 check` to verify.
**Remote prereqs:** SSH login + a writable USS directory. The `zo` binary is self-contained.
**Bundle:** `zx deploy` auto-downloads the server bundle pinned in [bundle.pin](bundle.pin) (release `tag`, `asset` name, and `sha256`) from [github.com/zowe/zowex/releases](https://github.com/zowe/zowex/releases) if it isn't found locally. Default save path is `~/.local/share/zx/server.pax.Z` (`%LOCALAPPDATA%\zx\server.pax.Z` on Windows) — always user-writable, works whether the helper is run directly or via PATH. **Every deploy checks the bundle's SHA-256 against `bundle.pin`** — whether it was just downloaded, cached from an earlier run, or supplied via `ZX_PAX` — and refuses to upload on a mismatch. To deploy a different release or a local build, update `bundle.pin` (and set `ZX_PAX` for a local file); never skip the check.

---

## 0 · Add `zx` to your PATH (optional but recommended)

Run once to symlink the helper into `~/.local/bin` (created if absent):

```bash
.agents/skills/zo-ssh/zx install          # -> ~/.local/bin/zx  (default)
.agents/skills/zo-ssh/zx install ~/bin    # -> ~/bin/zx          (custom dir)
```

`zx install` prints an `export PATH=...` line if the target directory isn't already on your PATH — paste it into your shell profile (`~/.zshrc`, `~/.bashrc`, etc.).

On Windows there are no symlink permissions to worry about — `install` writes a `zx.cmd` shim that launches the script with `-ExecutionPolicy Bypass`, so it works regardless of the machine's execution policy:

```powershell
.agents\skills\zo-ssh\zx.ps1 install              # -> ~\.local\bin\zx.cmd  (default)
.agents\skills\zo-ssh\zx.ps1 install C:\tools     # -> C:\tools\zx.cmd      (custom dir)
```

It prints the `$env:PATH` / `[Environment]::SetEnvironmentVariable(...)` line to run if the directory isn't on PATH yet.

After that, every command below can be called as `zx ...` directly. To remove the symlink/shim: `zx uninstall`.

---

## 1 · One-time deploy

The easiest path — just run `zx deploy`:

```bash
zx deploy user@host [/remote/dir]
```

If `server.pax.Z` isn't present locally, `zx deploy` will:
1. Download `https://github.com/zowe/zowex/releases/download/<tag>/<asset>` using the values in [bundle.pin](bundle.pin)
2. Verify its SHA-256 against the pinned `sha256` — on a mismatch the download is deleted and deploy stops
3. Continue with the normal sftp → unpax → verify flow

The release asset carries a version in its name (e.g. `zowe-server-1.0.1.pax.Z`); `zx` saves it as `server.pax.Z` at the `ZX_PAX` path. An existing file at that path is re-verified on every deploy, so a stale bundle from an older pin fails with a "sha256 mismatch" — delete it and re-run to fetch the pinned one.

**Different release or local build** — edit `bundle.pin`. For another GitHub release, set `tag`, `asset`, and `sha256` (GitHub lists each asset's `sha256:` digest on the release page; or hash the download yourself). For a local build, set `sha256` to its hash and point `ZX_PAX` at it:

```bash
sha256sum /path/to/server.pax.Z             # macOS: shasum -a 256 ...
# PowerShell: (Get-FileHash C:\path\to\server.pax.Z -Algorithm SHA256).Hash
ZX_PAX=/path/to/server.pax.Z
zx deploy user@host [/remote/dir]
```

`zx check` prints the current pin and whether the local bundle matches it.

**Low-level steps** (if you need to do it by hand):

```bash
# 1a. push the bundle (binary mode is sftp default for `put`)
sftp -b - "$ZX_HOST" <<EOF
-mkdir $ZX_DIR
cd $ZX_DIR
put server.pax.Z
bye
EOF

# 1b. unpax on the host (extracts flat: ./zo)
ssh "$ZX_HOST" "cd $ZX_DIR && pax -rvf server.pax.Z && chmod +x zo"

ZX_BIN=$ZX_DIR/zo

# 1c. verify
ssh "$ZX_HOST" "$ZX_BIN --help"
```

If `pax -rvf` fails on the `.Z`, do `uncompress server.pax.Z && pax -rvf server.pax`.

The CLI also exposes `tso` / `system` / `tool` subcommands — run `$ZX_BIN <cmd> --help` if you need one of those interactively. Only a subset is exposed via JSON-RPC (see §3; `tso` is RPC-backed, most of `system`/`tool` is CLI-only). Console is special: on servers newer than v0.8.0 the `console` CLI group exists only in the separate APF-authorized `zoa` binary, and the `consoleCommand` RPC (see §3) is how `zo server` reaches it. This CLI-only gap isn't limited to those groups: `data-set copy` is CLI-only too (no `copyDataset` RPC — see §3's Datasets table). When in doubt whether a `zo data-set`/`job`/`uss` verb has an RPC equivalent, check `$ZX_BIN data-set --help` against §3 rather than assuming parity.

---

## 2 · Talking to the server

`zo server` reads **JSON-RPC 2.0** requests on **stdin** and writes JSON responses on **stdout**, one object per line. Auth is whatever SSH gave you — there is no in-band handshake.

**Wire behavior (verified against v0.6.0):**
- The server emits one **ready banner** line first: `{"status":"ready","message":"...","data":{"version":"..."}}`. Consume and discard it before reading responses.
- With the default worker pool, **responses can arrive out of order** (matched by `id`). For scripted/sequential use, start with `-w 1` to serialize. The `zx` helper does this.
- Unknown methods return `{"error":{"code":-32601,"message":"Unrecognized command <name>"}}`.
- Result shape: `{"id":N,"result":{"success":true, ...},"jsonrpc":"2.0"}`. Common result fields: `data` (b64 for `readDataset`/`readFile`/`readSpool`; **plain text** for `unixCommand`/`tsoCommand`), `items` (lists), `jobId`/`jobName` (`submitJcl`), `etag` (`readDataset`), `returnedRows`.

### 2a · The `zx` helper (preferred)

```bash
zx=.agents/skills/zo-ssh/zx     # or alias/symlink it onto PATH

$zx deploy user@host [/remote/dir]   # one-time per host (idempotent); also saves config
$zx ds list "SYS1.*"                 # works immediately — auto one-shot
$zx job submit foo.jcl
$zx start                            # optional: persistent session = faster repeated calls
$zx job status JOB01234
$zx stop
```

On Windows, same sequence:

```powershell
$zx = '.agents\skills\zo-ssh\zx.ps1'   # or install the zx.cmd shim onto PATH (§0)

& $zx deploy user@host /u/user/zowex
& $zx ds list "SYS1.*"
& $zx start                               # strongly recommended on Windows - see §8
& $zx job status JOB01234
& $zx stop
```

`zx` remembers `host`+`bin` in `$ZX_STATE/config` (`config.json` for `zx.ps1`; set by `deploy` or `zx use <host> <bin>`; overridable via `ZX_HOST`/`ZX_BIN` env). Every grouped command auto-detects whether a persistent session is live and falls back to a one-shot ssh if not.

**Command groups** (run `zx help` for the full list):

| group | subcommands |
|---|---|
| `zx ds` | `list <pat>` · `members <dsn>` · `read <dsn>` · `write <dsn> <file>` · `get`/`put` `[--binary]` (member or whole PDS; default is text with EBCDIC conversion — pass `--binary` for byte-faithful transfer, e.g. load modules or tersed files) · `create` · `delete` · `copy` · `rename` |
| `zx job` | `list [<owner> [<prefix>]]` · `submit <file>` · `status <id>` · `spools <id>` · `spool <id> <n>` · `jcl <id>` · `cancel`/`delete`/`hold`/`release <id>` |
| `zx uss` | `ls` · **`get`/`put`** (sftp, binary-safe) · `read`/`write` (RPC, text) · `rm` · `mkdir` · `mv` · `cp` · `chmod` · `chown` · `chtag` · `sh '<cmd>'` |
| `zx tso` | `'<cmd>'` |
| `zx system` | `apf` · `linklist` · `proclib` · `syslog` (RPC) · `parmlib` · `subsystems` · `symbol <s>` (CLI) |
| `zx tool` | `amblist <dsn> --cs '<stmts>'` · `run <pgm> [opts]` · `search <dsn> <str>` · `dynalloc` · `dsect` (all CLI) |
| `zx console` | `'<cmd>' [--cn <n>] [--timeout <s>] [--no-wait]` (RPC `consoleCommand`, servers > v0.8.0; needs `zoa` installed + APF-authorized on the host and ESM OPERCMDS grants for non-display commands) |
| `zx rpc` | `<method> ['<params>']` — raw escape hatch |

There is no `zx` group yet for the ESM certificate / key ring commands (`zo system cert ...` / `zo system keyring ...`, servers newer than v0.7.0) — drive them with `zx rpc <method>` (see §3's Certificates table) or CLI passthrough: `ssh "$ZX_HOST" "$ZX_BIN system keyring list-rings $USER"`.

**Output:** grouped commands pretty-print by default (lists → one per line, b64 → decoded, status → `key=value`). Add `-j`/`--json` anywhere (before or after the group) for raw JSON: `zx ds list "SYS1.*" -j` or `zx -j ds list "SYS1.*"`.

`zx info` shows config + whether a session is live. `zx reset` is a harder stop: kills the session, closes the ControlMaster socket, and removes the entire `$ZX_STATE` directory — useful when the state gets corrupted or you want a clean slate.

### 2b · One-shot

For a single call without the persistent session (note `-w 1`; first output line is the ready banner):

```bash
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"listJobs","params":{"owner":"*","prefix":"XYZ*"}}' \
  | ssh "$ZX_HOST" "$ZX_BIN server -w 1" | sed 1d
```

### 2c · Isolated / parallel connections (second host without disturbing the first)

All `zx` state — config, persistent-session pid, FIFOs, and the SSH `ControlMaster` socket — lives
under `$ZX_STATE`, which defaults to **one shared directory per UID** (`/tmp/zx.$UID`). That means
`zx deploy`/`zx use` for a second host **overwrites** the first host's saved config, and — more
importantly — `_live()` (the check every grouped command uses to decide "route through the
persistent session or go one-shot") only checks *whether a pid exists*, not *which host it's
for*. If a persistent session is already live for host A and you need one-shot calls against host
B without touching it, exporting `ZX_HOST`/`ZX_BIN` alone is **not enough** — the dispatcher will
still shove your request down host A's session.

Fix: give the second host its own `ZX_STATE` so it never sees host A's pid file or socket:

```bash
export ZX_STATE=/tmp/zx-<label>.$UID     # short — see ControlPath length note below
export ZX_HOST=user@hostB
zx deploy "$ZX_HOST" /remote/dir          # writes config under the isolated ZX_STATE only
zx ds list "HLQ.*"                        # one-shot; host A's session/socket is untouched
```

If host B needs password auth and you can't leave an interactive prompt open, prime the
`ControlMaster` once with `sshpass` using **the same `ControlPath` pattern `zx` uses**, then let
`zx` reuse the socket silently for subsequent calls:

```bash
sshpass -p "$PASSWORD" ssh -o ControlMaster=auto -o ControlPath="$ZX_STATE/cm-%C" \
  -o ControlPersist=30m -T "$ZX_HOST" exit
```

Do this under the same isolated `$ZX_STATE` you'll use for the real `zx` calls — the `%C` token
resolves to the same socket path either way, so `zx`'s own `ssh` invocations find it already open
and skip the password prompt. Never run `zx start`/`zx stop`/`zx reset` in this workflow — those
only make sense for the single shared default state and `reset` will nuke whichever `$ZX_STATE`
is currently exported.

On Windows the same isolation rule applies — a separate `$env:ZX_STATE` gives you a separate
config, pid file, **and** named pipe (the pipe name is derived from the state directory), so two
`ZX_STATE`s never see each other's sessions. The `ControlMaster`/`sshpass` priming above does
*not* apply: Windows OpenSSH has no connection multiplexing. Instead give each host its own
`ZX_STATE` and run `zx start` per host — each gets its own long-lived ssh connection, and each
authenticates once (§8).

### Envelope

```json
{"jsonrpc":"2.0","method":"<name>","params":{...},"id":<int>}
```

- `id` must be unique per request within a session.
- All `data` fields (file/dataset content, JCL text) are **base64**.
- Responses are standard JSON-RPC 2.0: `{"jsonrpc":"2.0","id":N,"result":{...}}` or `{"...","error":{...}}`.

---

## 3 · Method reference

### Datasets

| method | params |
|---|---|
| `listDatasets` | `{"pattern":"HLQ.*"}` |
| `listDsMembers` | `{"dsname":"HLQ.PDS"}` |
| `readDataset` | `{"dsname":"HLQ.PS"}` · optional `"encoding":"ISO8859-1"` |
| `writeDataset` | `{"dsname":"HLQ.PS","data":"<b64>"}` · optional `"encoding"` |
| `createDataset` | `{"dsname":"HLQ.NEW","attributes":{"primary":10,"lrecl":80}}` |
| `deleteDataset` | `{"dsname":"HLQ.OLD"}` |
| `renameDataset` | `{"dsnameBefore":"A","dsnameAfter":"B"}` |

**No `copyDataset` RPC** — at least as of server v0.6.0, `copyDataset` isn't wired over JSON-RPC (returns `-32601 Unrecognized command`) even though it exists as a CLI verb. `zx ds copy` calls the CLI directly instead (`zo data-set copy <src> <dst> [--ow|-r]`, via SSH passthrough — see §2's CLI-vs-RPC note). If you're issuing raw RPC via `zx rpc` or `zx -j ds copy`-style JSON, don't rely on `copyDataset` — shell out to the CLI form instead.

### Jobs

| method | params |
|---|---|
| `listJobs` | `{}` · optional `"owner"`, `"prefix"` |
| `getJobStatus` | `{"jobId":"JOB01234"}` |
| `listSpools` | `{"jobId":"JOB01234"}` |
| `readSpool` | `{"jobId":"JOB01234","spoolId":2}` |
| `getJcl` | `{"jobId":"JOB01234"}` |
| `submitJcl` | `{"jcl":"<b64 of JCL text>"}` |
| `submitJob` | `{"dsname":"HLQ.JCL(MBR)"}` |
| `submitUss` | `{"fspath":"/path/job.jcl"}` |
| `cancelJob` / `deleteJob` / `holdJob` / `releaseJob` | `{"jobId":"JOB01234"}` |

### USS

| method | params |
|---|---|
| `listFiles` | `{"fspath":"/u/x"}` |
| `readFile` | `{"fspath":"/u/x/f"}` · optional `"encoding"` |
| `writeFile` | `{"fspath":"/u/x/f","data":"<b64>"}` · optional `"encoding"` |
| `createFile` | `{"fspath":"/u/x/f"}` · add `"isDir":true` for mkdir |
| `deleteFile` | `{"fspath":"/u/x/f"}` |
| `moveFile` | `{"source":"...","target":"..."}` |
| `copyUss` | `{"srcFsPath":"...","dstFsPath":"..."}` |
| `chmodFile` | `{"fspath":"...","mode":"755"}` |
| `chownFile` | `{"fspath":"...","owner":"..."}` |
| `chtagFile` | `{"fspath":"...","tag":"UTF-8"}` |
| `unixCommand` | `{"commandText":"<shell cmd>"}` — `result.data` is **plain text** |

### TSO

| method | params |
|---|---|
| `tsoCommand` | `{"commandText":"<tso cmd>"}` — `result.data` is plain text |

### Console

**Server version gate:** `consoleCommand` exists only in servers **newer than v0.8.0** (zowex PR #1110). On older servers it returns `-32601 Unrecognized command`.

| method | params |
|---|---|
| `consoleCommand` | `{"commandText":"<mvs cmd>","consoleName"?:"<name>","timeout"?:N,"wait"?:bool}` — `result.data` is plain text |

The server spawns the APF-authorized **`zoa`** binary per request (located next to `zo`, on the server's `PATH`, or via `ZOA_PATH` on the remote host) — the server itself never holds APF authorization. Prereqs on the host: `zoa` installed and `extattr +ap`'d by a system programmer, and ESM OPERCMDS grants (`MVS.*` profiles, e.g. `MVS.REPLY.*` for WTOR replies) for anything beyond informational `D ...` displays. `consoleName` defaults to the SSH user's ID plus a digit suffix; console activation can be gated per user with `MVS.MCSOPER.<name>` profiles. Failures come back with actionable messages ("command not found" → zoa not installed; "Not authorized" → not APF-authorized or ESM denied).

### Certificates / key rings (ESM)

**Server version gate:** these methods exist only in servers **newer than v0.7.0** (zowex PR #1079). On older servers they return `-32601 Unrecognized command`. The caller's SSH user needs the corresponding ESM `IRR.DIGTCERT.*` / `RDATALIB` authority — without it, calls fail with a SAF diagnostic (see below), not an auth prompt.

CLI equivalents live under `zo system cert ...` and `zo system keyring ...`.

| method | params |
|---|---|
| `createKeyring` | `{"owner":"USER01","keyring":"RING02"}` |
| `deleteKeyring` | `{"owner":"USER01","keyring":"RING02"}` |
| `listRings` | `{"owner":"USER01"}` · optional `"keyring"` to narrow — rings + connected certs |
| `countRing` | `{"owner":"USER01","keyring":"RING02"}` — `"*"` counts all the owner's certs |
| `listCertificates` | `{"owner":"USER01","keyring":"RING02"}` · optional `"label"`, `"usage"`, `"labelOnly"`, `"ownerOnly"`, `"maxEntries"` (default 10, `0` = all) — `result.moreAvailable` flags truncation |
| `showCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03"}` — adds serial, validity dates, key size |
| `exportCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03"}` · optional `"format"` (`pem` default / `p12`), `"file"` (server-side path; required for p12), `"password"` (p12) — `result.data` is **b64** |
| `importCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03","usage":"PERSONAL","file":"/u/user01/c.p12","password":"..."}` · optional `"skipRefresh"` — `file` is a **server-side** PKCS#12 path; `usage` is `PERSONAL` or `CERTAUTH` |
| `deleteCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03"}` · or `"database":true` (omit `keyring`) to delete from the ESM DB · optional `"skipRefresh"` |
| `connectCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03","fromRing":"RING01"}` · or `"fromDatabase":true` · optional `"usage"`, `"default"` |
| `setDefaultCertificate` | `{"owner":"USER01","keyring":"RING02","label":"CERT03"}` |
| `trustCertificate` | `{"owner":"USER01","label":"CERT03","status":"NOTRUST"}` — `TRUST`/`HIGHTRUST`/`NOTRUST`; no keyring (operates on the DB record) |
| `renameCertificate` | `{"owner":"USER01","label":"OLD","newLabel":"NEW"}` — no keyring (DB record) |
| `refreshDigtcert` | `{}` — refresh the DIGTCERT class (import/delete usually auto-refresh unless `skipRefresh`) |

Conventions shared by these methods:
- `"*"` as `keyring` is the ESM **virtual key ring** (all of the owner's certificates) — valid for list/count/show/export reads; rejected as a target for create/delete-ring/import/connect (use the `database`/`fromDatabase` booleans instead).
- Failures return `result.success:false` with `message`, `service`, and a structured `safReturns` object (`functionCode`, `safReturnCode`, `esmReturnCode`, `esmReasonCode`); GSK (System SSL) failures add `gskReturnCode`. Non-fatal SAF warnings (rc 4) succeed with a `warning` string.
- `owner`, `keyring`, and `label` are case-sensitive (userids normally uppercase).

`unixCommand` / `tsoCommand` are the escape hatches for anything not covered. MVS console commands have their own method on servers newer than v0.8.0 — `consoleCommand` (see the Console table above). On older servers, run the APF-authorized companion binary directly on the host: `ssh "$ZX_HOST" "zoa console issue ..."` (`zo console issue` only exists on pre-v0.8.x binaries and requires the old fat `zoa` there too).

---

## 4 · zowe-CLI → zx mapping

When porting a workflow that used the `zowe` CLI:

| `zowe` CLI | `zx` | underlying RPC method |
|---|---|---|
| `zowe files ls ds "PAT"` | `zx ds list "PAT"` | `listDatasets` |
| `zowe files ls am "DSN"` | `zx ds members "DSN"` | `listDsMembers` |
| `zowe files view ds "DSN"` | `zx ds read "DSN"` | `readDataset` (b64) |
| `zowe files upload ftds f "DSN"` | `zx ds write "DSN" f` | `writeDataset` |
| `zowe files delete ds "DSN" -f` | `zx ds delete "DSN"` | `deleteDataset` |
| `zowe jobs submit lf f.jcl` | `zx job submit f.jcl` | `submitJcl` (local file) |
| `zowe jobs submit ds "HLQ.JCL(MBR)"` | `zx job submit "HLQ.JCL(MBR)"` | `submitJob` (dataset) |
| `zowe jobs submit uss "/path/job.jcl"` | `zx job submit /path/job.jcl` | `submitUss` (USS path) |
| `zowe jobs view jsbj JOBID` | `zx job status JOBID` | `getJobStatus` → `.status`/`.retcode`/`.phaseName` |
| `zowe jobs list sfbj JOBID` | `zx job spools JOBID` | `listSpools` |
| `zowe jobs view sfbi JOBID N` | `zx job spool JOBID N` | `readSpool` (b64) |
| `zowe jobs cancel job JOBID` | `zx job cancel JOBID` | `cancelJob` |
| (AMBLIST batch job) | `zx tool amblist DSN --cs '<stmts>'` | CLI passthrough |
| (ISRSUPC batch job) | `zx tool search DSN "str" --parms anyc` | CLI passthrough |

Submit-then-poll pattern:

```bash
JID=$(zx job submit f.jcl | grep '^jobId=' | cut -d= -f2)
while [[ $(zx -j job status "$JID" | jq -r .result.status) == ACTIVE ]]; do sleep 2; done
zx job spools "$JID"          # list spool files with their IDs
zx job spool  "$JID" 2        # read spool file #2 (JESMSGLG, etc.)
```

---

## 5 · File transfer

| Command | What | Transport |
|---|---|---|
| `zx uss get <remote> [<local>]` | download one USS file (binary-safe) | sftp |
| `zx uss put <local> <remote>` | upload one USS file (binary-safe) | sftp |
| `zx ds get "<DSN(M)>" <file>` | download one PDS member | RPC `readDataset` (b64) |
| `zx ds get "<DSN>" <localdir>` | download **all** members of a PDS | loops `listDsMembers` + `readDataset` |
| `zx ds put <file> "<DSN(M)>"` | upload one file as a member | RPC `writeDataset` (b64) |
| `zx ds put <localdir> "<DSN>"` | upload directory → PDS (filename → MEMBER, ext stripped, upcased, 8-char) | loops `writeDataset` |

`ds get|put` go through base64-in-JSON, so use them for text members (REXX/JCL/parmlib/source). For load modules or anything large/binary, stage through USS:

```bash
zx uss sh 'cp -B "//'\''SYS1.LINKLIB(IEFBR14)'\''" /tmp/iefbr14.bin'
zx uss get /tmp/iefbr14.bin
```

For whole-PDS `ds get`, run `zx start` first — one persistent session is much faster than N one-shot SSH connects. `ds put` of a directory may hit `zo` DEQ contention on rapid same-PDS writes; failures are reported per-member and the command exits non-zero if any failed.

---

## 6 · Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| password prompt on every `zx` call | `zx` already multiplexes its own ssh calls per `$ZX_STATE` (30min `ControlPersist`), so this is usually only about *manual/raw* `ssh` calls outside `zx`. Prefer `ssh-copy-id <host>` (a key). If the user specifically wants a long-lived multiplexed session across *all* ssh tools to that host, the fix is a host-wide, persistent change to `~/.ssh/config` (`ControlMaster auto` / `ControlPath` / `ControlPersist`) — it keeps an authenticated socket open for the persist duration, reusable by any local process as that user. That's a change outside this session's scope: confirm with the user and get their desired `ControlPersist` before editing `~/.ssh/config`, don't add it unprompted. |
| `mkdir: ... EDC5134I Function not implemented` on deploy | parent dir is an automount root — `zx deploy` now `cd`s to the parent first to trigger the mount; if it still fails, the parent genuinely doesn't exist |
| `pax: FSUM7108 cannot open` | wrong dir / no write perms — pick a different `ZX_DIR` |
| `zo: FSUM7351 not found` | `ZX_BIN` path wrong — re-run the `find` from step 1b |
| `EDC5129I No such file or directory` on `zo --help` | binary not tagged/executable — `chmod +x $ZX_BIN`; if it's a tag issue, `chtag -b $ZX_BIN` |
| server returns nothing then EOF | request wasn't newline-terminated, or JSON was malformed — `zx rpc` always appends `\n` |
| `CEE3501S module not found` | LE runtime not in LIBPATH — prefix server start with `export LIBPATH=$ZX_DIR/c/build-out:$LIBPATH;` |
| `CEE3561S ... was not found in DLL CRTEQCXE` | the target's Language Environment libc++ doesn't export a symbol the binary needs — a *maintenance level* problem, not just a z/OS release. Nothing to fix on the client side: the binary must be built with Open XL 2.1 (not 2.2) for a z/OS 2.5 target, or the target needs LE PTFs. See `doc/troubleshooting.md` |
| every method returns auth-style errors | the SSH user lacks the needed ESM access; zo itself does no auth |
| `ControlPath too long ('...' >= 104 bytes)` | Unix domain socket path limit (macOS: 104 bytes) — `$ZX_STATE/cm-%C` overflowed it. Use a short `ZX_STATE`, e.g. `/tmp/zx-<label>.$UID`, not a long nested path like a session scratch dir |
| need password auth against a second host without disturbing an existing `zx` session/socket | give the second host its own `ZX_STATE` and prime its `ControlMaster` with `sshpass` — see §2c |
| **(Windows)** `zx.ps1 cannot be loaded because running scripts is disabled on this system` | execution policy — call it as `powershell -ExecutionPolicy Bypass -File .agents\skills\zo-ssh\zx.ps1 ...`, or run `install` once and use the `zx.cmd` shim, which already passes that flag |
| **(Windows)** a password prompt on *every* call | expected in one-shot mode: Windows OpenSSH has no `ControlMaster`, so each call is a fresh connection. Run `zx start` (one connection, one prompt, reused by every later call) or set up key auth — see §8 |
| **(Windows)** `zx start` says `server failed to start` | look in `$env:ZX_STATE\err` (session-host failure) and `$env:ZX_STATE\ssh-err` (what ssh/zo printed). A wrong host/bin, a rejected key, or an untrusted host key all land here |
| **(Windows)** redirected output (`zx ds read X > f.txt`) is UTF-16 with doubled newlines | that's PowerShell's `>`, not `zx` — use `zx ds get "X" f.txt` / `zx uss get`, or pipe through `Out-File -Encoding utf8` / `Set-Content` |
| **(Windows)** `sftp` ops (`deploy`, `uss get`/`put`) fail with `Permission denied` while RPC calls work | `sftp -b` normally forces `BatchMode=yes` (no prompting). `zx.ps1` passes `-o BatchMode=no` to allow it — but if `ZX_SSH_OPTS` sets `BatchMode=yes`, that wins (ssh takes the first value). Drop it, or use key auth |

---

## 7 · Cleanup

```bash
zx stop                              # kill persistent session (ControlMaster socket kept for 30m)
zx reset                             # harder: also kills ControlMaster + wipes $ZX_STATE dir
ssh "$ZX_HOST" "rm -rf $ZX_DIR"     # remove the remote binary (only if you deployed it)
```

Only remove `$ZX_DIR` on the remote side if you created it for this session.

On Windows, `zx.ps1 stop` shuts down the session host **and** reaps its `ssh` child (tracked in
`$ZX_STATE\ssh-pid`), and `zx.ps1 reset` additionally wipes the state directory. There is no
`ControlMaster` socket to close.

---

## 8 · Windows notes (`zx.ps1`)

Behavior differences vs. the bash helper — everything else (subcommands, `-j`, `ZX_HOST`/`ZX_BIN`/`ZX_STATE`/`ZX_PAX`/`ZX_TIMEOUT`, `bundle.pin`, method coverage, output shapes) is the same.

- **Requires Windows PowerShell 5.1+** (the version that ships with Windows) or PowerShell 7. No `jq`, `base64`, `curl`, `wget`, or `sha256sum` — JSON, base64, the release download, and the SHA-256 check are done in-process.
- **No connection multiplexing.** Windows OpenSSH doesn't implement `ControlMaster`/`ControlPath`, so every one-shot call is a new SSH connection (slower, and one auth per call with passwords). `zx start` is the stand-in: it launches a hidden-ish background PowerShell host that owns one long-lived `zo server -w 1` over ssh and answers one request per **named-pipe** connection. Every grouped command auto-routes through it when it's live, exactly like the bash version. **Use `zx start` for anything more than a couple of calls, and always for a whole-PDS `ds get`/`ds put`.**
  - The host is started with `-NoNewWindow` so it keeps your console: with password auth, ssh's prompt appears in your terminal during `zx start` (which waits up to 60s, or `ZX_TIMEOUT` if larger, for the ready banner). Key auth (`ssh-keygen` + append your public key to the host's `~/.ssh/authorized_keys`) avoids the prompt entirely.
  - The pipe name is derived from `$env:ZX_STATE`, so per-agent/per-host isolation works the same way (§2c) and there's no socket-path length limit (the bash `ControlPath too long` problem doesn't exist here).
  - `zx info` prints the state dir, config, pid, and pipe name.
- **Output goes through the PowerShell pipeline** (`Write-Output`), so `zx ds list "X.*" | Select-String FOO`, `$rows = zx -j ds list "X.*" | ConvertFrom-Json`, and `Tee-Object` all work. Errors and progress lines go to stderr. Caveat: PowerShell 5.1's `>` writes UTF-16 — use `zx ds get` / `zx uss get` for files, or `Out-File -Encoding utf8`.
- **State/paths:** `$env:ZX_STATE` defaults to `%TEMP%\zx.%USERNAME%` and holds `config.json`, `pid`, `ssh-pid`, `ready`, `err`, `ssh-err`. `$env:ZX_PAX` defaults to `%LOCALAPPDATA%\zx\server.pax.Z`.
- **Extra ssh options:** `$env:ZX_SSH_OPTS` is split on whitespace and prepended to every `ssh`/`sftp` call, e.g. `$env:ZX_SSH_OPTS = '-i C:\keys\zos_id -o StrictHostKeyChecking=accept-new'`. Since ssh honors the *first* value for an option, anything you set here wins over `zx.ps1`'s own defaults.
- **Local paths** may use `\` or `/`; they're normalized for `sftp` (which treats `\` as an escape). Relative paths resolve against the current directory.
- `zo` CLI passthroughs (`zx tool`, `zx system parmlib|subsystems|symbol`, `zx ds copy`) work the same — they run as their own ssh command, not through the session pipe. Everything RPC-backed — including `zx console` (`consoleCommand`) and `zx ds get|put --binary` — goes through the session when one is live.