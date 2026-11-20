# zoa: installing and securing the APF-authorized console binary

`zoa` is the APF-authorized alternative to `zo`. It contains only
the `console` command group (plus `version`/help) and exists because z/OS
extended MCS console services (MCSOPER/MGCRE/MCSOPMSG) require an authorized
caller. Everything else — data sets, jobs, USS, TSO, the JSON-RPC server, and
plug-in loading — lives in the unauthorized `zo` binary, which never holds
APF authorization. When a client issues the `consoleCommand` JSON-RPC method,
`zo server` spawns `zoa console issue ...` in a separate address space
per request.

`zoa` is **never installed automatically**. The SDK / VS Code / MCP
auto-deploy path ships only `zo` (to `~/.zowe-server`); marking a file
APF-authorized is a system-programmer action, and installing `zoa` is the
manual procedure described below.

## Security model

| Layer | Enforced by | What it guarantees |
| --- | --- | --- |
| Who can run it | UNIX owner/group/mode on the `zoa` file | Execute permission is the first gate |
| APF authorization | `extattr +ap` set by a sysprog; `TESTAUTH` at every console entry point | Without the attribute, console commands fail closed (`Not authorized`) |
| Address-space isolation | `extattr -s` (set at build) + kernel APF-state rules | `zoa` never runs in a shared address space, regardless of `_BPX_SHAREAS` |
| Console activation | ESM `MVS.MCSOPER.<console>` profiles (OPERCMDS class) | Sites control who may activate a console, per user |
| Command authority | ESM OPERCMDS profiles (`MVS.*`) checked under the **invoking user's identity** | Each command is individually authorized and audited (SMF) |
| Fallback authority | Console `AUTH` attribute from the user's OPERPARM segment (default `INFO`) | With no OPERCMDS decision, only informational commands succeed |
| Privilege drop | Pre-command hook (`ZUTNOAUT`) | Non-console commands (`version`, help, interactive) relinquish authorization before running, fail closed |

## Installation

Prerequisites (system programmer):

- READ access to `BPX.FILEATTR.APF` in the FACILITY class (to run `extattr +ap`).
- The target filesystem must not be mounted `NOSETUID` (extended attributes
  are ignored on NOSETUID mounts).

Both are covered under "Using extended attributes" in *z/OS UNIX System
Services Planning*.

Steps:

1. **Obtain the binary.** `zo` ships inside the release `server.pax.Z`
   from [GitHub releases](https://github.com/zowe/zowex/releases) (the release
   pax contains both `zo` and `zoa`; the SDK-embedded pax contains only
   `zo`). Or build from source: `npm run z:rebuild` produces both in
   `c/build-out` with the extended attributes already set.

2. **Install to a protected directory** — for example `/usr/lpp/zo/bin`.
   Never install `zoa` in a user-writable location such as
   `~/.zowe-server`: anyone who can replace the file contents of an
   APF-authorized binary can run arbitrary authorized code.

   ```sh
   cp zoa /usr/lpp/zo/bin/zoa
   chown SYSPROG:ZOWECONS /usr/lpp/zo/bin/zoa
   chmod 750 /usr/lpp/zo/bin/zoa     # group membership = who may issue console commands
   ```

3. **Set the extended attributes:**

   ```sh
   extattr +ap /usr/lpp/zo/bin/zoa   # APF-authorized
   extattr -s  /usr/lpp/zo/bin/zoa   # never run in a shared address space
   ```

4. **Verify:**

   ```sh
   ls -E /usr/lpp/zo/bin/zoa          # expect 'ap' in the attribute flags, no 's'
   /usr/lpp/zo/bin/zoa console issue "D T"
   ```

   `IEE136I` output means APF and console activation work. `Not authorized`
   means the attribute is missing or the mount is NOSETUID.

**Upgrading:** extended attributes are cleared whenever the file contents are
replaced. Re-run step 3 after every upgrade — a missed `extattr` fails closed
(console commands stop working) rather than open, but it will look like an
outage.

**Uninstalling:** delete the file. Nothing else is registered anywhere.

## How `zo server` finds zoa

Resolution order for each `consoleCommand` request:

1. `ZOA_PATH` environment variable (full path to the binary) — most reliable.
2. A `zoa` file next to the running `zo` binary — appropriate only when
   `zo` itself is in a protected admin-managed directory.
3. `zoa` on the server process's `PATH`.

The server runs under an SSH exec channel — a **non-login shell** — so
`/etc/profile` and `~/.profile` are typically not sourced and `PATH` is the
sshd default. A directory added to `PATH` by a profile script will not be
visible. Verify from the server's perspective:

```sh
ssh user@host 'echo $PATH; command -v zoa'
```

## ESM configuration

Two resource families control console use, both in the **OPERCMDS** class:

### `MVS.MCSOPER.<console-name>` — who may activate a console

An EMCS console name is 1-8 characters. Names are folded to uppercase, so
`zowecon1` and `ZOWECON1` name the same console. `USER8888`, `ZOWEZOWE`, and
`ZOWECON1` are all valid; refer to *z/OS MVS Planning: Operations* for the
permitted character set and for EMCS limits.

The name a request uses determines the profile that is checked. Both the CLI
and the JSON-RPC method derive a default name from the invoking user ID,
truncated to 7 characters, plus one digit 0-9 (see
`zcn_build_default_console_name` in `native/c/zcn.cpp`). The profile stem is
therefore the user ID truncated to 7 characters, not the full user ID:

| User ID | Console names used | Profile stem to authorize |
| --- | --- | --- |
| `PRODUSR` (7) | `PRODUSR0` - `PRODUSR9` | `MVS.MCSOPER.PRODUSR*` |
| `LONGUSER` (8) | `LONGUSE0` - `LONGUSE9` | `MVS.MCSOPER.LONGUSE*` |

For an 8-character user ID, a profile named after the full user ID does not
cover the names actually used, and activation fails with
`MCSOPER_ACTIVATE ... service_rc: 12` even when that profile carries an
explicit permit. Check the ESM's generic or masking rules for how a prefix
expands.

A site may use its own naming scheme instead by authorizing a prefix and
passing `--console-name` (CLI) or `consoleName` (RPC):

```
RACF:  RDEFINE OPERCMDS MVS.MCSOPER.ZOWE* UACC(NONE)
       PERMIT  MVS.MCSOPER.ZOWE* CLASS(OPERCMDS) ID(ZOWEGRP) ACCESS(READ)
ACF2:  RECKEY MVS ADD(MCSOPER.ZOWE- ROLE(ZOWEGRP) SERVICE(READ) ALLOW)
TSS:   TSS PERMIT(ZOWEGRP) OPERCMDS(MVS.MCSOPER.ZOWE) ACCESS(READ)
```

Give each concurrent request a distinct name. A console name identifies one
console and cannot name two active consoles at once, so a single fixed name
serializes requests; requests that lose the race fail activation with
`service_rc: 4`, which is distinct from the `service_rc: 12` of a missing
permit. Varying a suffix inside the authorized prefix (`ZOWECON1`,
`ZOWECON2`, ...) keeps one profile covering the whole pool, which is what the
user-derived default does with its trailing digit.

Do not pass `--console-name <userid>`. A TSO session holding SDSF ULOG or TSO
CONSOLE already owns an EMCS console named for the user ID, and activation
fails while it does; `D EMCS,FULL,CN=<name>` identifies the holder.

### `MVS.<command>.<qualifier>` — which commands may be issued

Each command is authorized separately, under the identity of the user who
issued it, against a profile named for the command:

| Command | Profile | Typical use |
| --- | --- | --- |
| `DISPLAY` (`D`) | `MVS.DISPLAY.*` | informational queries |
| `START` (`S`) | `MVS.START.*` | start a started task |
| `STOP` (`P`) | `MVS.STOP.*` | stop a started task |
| `MODIFY` (`F`) | `MVS.MODIFY.*` | pass a command to a running task |
| `CANCEL` (`C`) | `MVS.CANCEL.*` | cancel a job or address space |
| `REPLY` (`R`) | `MVS.REPLY.*` | answer an outstanding WTOR |
| `VARY` (`V`) | `MVS.VARY.*` | device and path state changes |
| `SLIP` | `MVS.SLIP` | set, modify, or delete SLIP traps |
| `ROUTE` (`RO`) | `MVS.ROUTE.*` | route a command to another system |
| `FORCE` | `MVS.FORCE.*` | force an address space |
| `HALT` (`Z`) | `MVS.HALT.*` | halt system activity |

Required access varies by command and by the third qualifier — informational
DISPLAY commands need READ, while state-changing commands need UPDATE or
CONTROL. Take the exact resource name and access authority for every command
from "MVS commands, RACF access authorities, and resource names" in *z/OS MVS
System Commands*; that table, not this page, is the reference for what to
define.

Defining these profiles is how a site grants and audits command authority.
The absence of a profile is not by itself a control: where no profile
matches, authorization falls back to the console's authority attribute
described below, whose default value permits informational commands.
Restricting a command means defining a profile that denies it, or lowering
the fallback authority.

When the ESM returns "no decision" (class inactive or no matching profile),
z/OS falls back to the console's authority attribute, which comes from the
user's **OPERPARM segment** (default `INFO` — informational commands only).
Neither the `zoa` CLI nor the `consoleCommand` RPC requests an authority
override: both leave `ZCN.authcmdx` at zero, which selects the
ESM/console-attribute path. Raising a user's OPERPARM `AUTH` value is
therefore not a dependable way to widen what that user can issue through
`zoa` — grant command authority with OPERCMDS profiles instead. (Programs
linking `libzcn` directly — already APF-authorized code — may opt in to an
explicit override through `ZCN.authcmdx`, whose values are defined as
`ZCN_AUTHCMDX_MASTER`, `_SYS`, `_IO`, and `_CONS` in `zcntype.h`, after
performing their own SAF checks.)

Two failure messages distinguish where a rejection came from:
`AUTHORITY INVALID, FAILED BY SECURITY PRODUCT` means a profile matched and
the ESM denied the request, while `AUTHORITY INVALID, FAILED BY MVS` means no
profile matched and the console's own authority attribute was insufficient.
The first is fixed by changing the profile or the permit; the second by
defining a profile that grants the command.

> **These examples are a starting point, not a specification.** They show the
> minimum shape of what `zoa` needs; they are not a substitute for your
> ESM's own documentation, and command syntax, resource-class mappings, and
> required access levels all vary by product version and site convention.
> Have an administrator of the ESM in question review them against:
>
> - RACF — *z/OS Security Server RACF Security Administrator's Guide* and
>   *RACF Command Language Reference* (OPERCMDS class, generic profiles).
> - ACF2 — *CA ACF2 for z/OS Administrator Guide*. Confirm the resource type
>   mapped to OPERCMDS with `SHOW CLASMAP` before writing any `RECKEY`; the
>   mapping is site-defined.
> - Top Secret — *CA Top Secret for z/OS Command Functions Guide* and the
>   keyword reference. TSS protects a resource only when it is owned, so
>   confirm ownership (for example with `TSS WHOHAS`) before assuming a
>   denial is in effect.
>
> Common to all three: after changing authorization, make the change visible
> before retesting. Each product has its own step — RACF needs
> `SETROPTS RACLIST(<class>) REFRESH` for a RACLISTed class, ACF2 needs
> `F ACF2,REBUILD(<directory>)`, and Top Secret caches authorization per
> session. If a change still appears to have no effect after the product's
> refresh, sign on again: a reused connection (including a multiplexed SSH
> `ControlMaster` socket) can retain the earlier decision.

### RACF

```
/* Activate the class (once) */
SETROPTS CLASSACT(OPERCMDS) GENERIC(OPERCMDS) RACLIST(OPERCMDS)

/* Console activation, per user.  STEM = userid truncated to 7 characters
   (8-char LONGUSER -> STEM LONGUSE -> consoles LONGUSE0-LONGUSE9)      */
RDEFINE OPERCMDS MVS.MCSOPER.STEM* UACC(NONE)
PERMIT MVS.MCSOPER.STEM* CLASS(OPERCMDS) ID(USERID) ACCESS(READ)

/* Informational displays */
RDEFINE OPERCMDS MVS.DISPLAY.** UACC(NONE) AUDIT(ALL(READ))
PERMIT MVS.DISPLAY.** CLASS(OPERCMDS) ID(OPERGRP) ACCESS(READ)

/* WTOR replies (check the command table for the required access level) */
RDEFINE OPERCMDS MVS.REPLY.** UACC(NONE) AUDIT(ALL(READ))
PERMIT MVS.REPLY.** CLASS(OPERCMDS) ID(OPERGRP) ACCESS(UPDATE)

/* Fallback console authority for a user without OPERCMDS coverage */
ALTUSER USERID OPERPARM(AUTH(INFO))

/* Installer authority for extattr +ap */
RDEFINE FACILITY BPX.FILEATTR.APF UACC(NONE)
PERMIT BPX.FILEATTR.APF CLASS(FACILITY) ID(SYSPROG) ACCESS(READ)

SETROPTS RACLIST(OPERCMDS) REFRESH
SETROPTS RACLIST(FACILITY) REFRESH
```

### ACF2

Confirm the resource type mapped to the OPERCMDS class first (`SHOW CLASMAP`;
commonly `OPR`) and to FACILITY (commonly `FAC`).

ACF2 keeps a user's z/OS UNIX identity in a USER profile record rather than on
the logonid. An SSH user therefore needs both records before `zoa` can run:

```
ACF
SET LID
INSERT USERID NAME('...') PASSWORD(...) GROUP(grp)
SET PROFILE(USER) DIVISION(OMVS)
INSERT USERID UID(nnnn) HOME(/path) OMVSPGM(/bin/sh)
END
F ACF2,REBUILD(USR),CLASS(P)
```

`SHOW STATE` reports the enforcement mode; denials are only meaningful when
it is `ABORT`. See the *Administrator Guide* for logonid and profile
administration, and for the GSO `PSWDFRC` option, which can force a password
change at next signon and so block non-interactive logins.

```
ACF
SET RESOURCE(OPR)
* STEM = userid truncated to 7 characters (LONGUSER -> LONGUSE).
* USER(logonid) and ROLE(rolename) are accepted as well as the classic
* UID(uidstring) form used here; match whichever your site's rules use.
RECKEY MVS ADD(MCSOPER.STEM- UID(uidstring-for-USERID) SERVICE(READ) ALLOW)
RECKEY MVS ADD(DISPLAY.- UID(uidstring-for-opergrp) SERVICE(READ) ALLOW)
RECKEY MVS ADD(REPLY.- UID(uidstring-for-opergrp) SERVICE(UPDATE) ALLOW)

* To deny a specific user where a broad rule already allows access, add a
* PREVENT line.  ACF2 orders USER()/UID() entries ahead of ROLE() entries
* within a resource, so a user-specific PREVENT takes precedence over a
* ROLE(-) ALLOW without manual reordering.
RECKEY MVS ADD(MCSOPER.- USER(USERID) PREVENT)

F ACF2,REBUILD(OPR)

* Fallback console authority (OPERPARM user profile record)
SET PROFILE(USER) DIVISION(OPERPARM)
INSERT USERID AUTH(INFO)
F ACF2,REBUILD(USR),CLASS(P)

* Installer authority for extattr +ap
SET RESOURCE(FAC)
RECKEY BPX ADD(FILEATTR.APF UID(uidstring-for-sysprog) SERVICE(READ) ALLOW)
F ACF2,REBUILD(FAC)
```

### Top Secret (TSS)

```
* Ownership (once)
TSS ADDTO(opsdept) OPERCMDS(MVS.)

* Console activation, per user.  STEM = userid truncated to 7 characters
* (8-char LONGUSER -> STEM LONGUSE -> consoles LONGUSE0-LONGUSE9).
* TSS resource names are prefix matches, so the stem covers all 10 digits.
TSS PERMIT(USERID) OPERCMDS(MVS.MCSOPER.STEM) ACCESS(READ)

* Informational displays
TSS PERMIT(opergrp) OPERCMDS(MVS.DISPLAY.) ACCESS(READ)

* WTOR replies (check the command table for the required access level)
TSS PERMIT(opergrp) OPERCMDS(MVS.REPLY.) ACCESS(UPDATE)

* Fallback console authority.  TSS holds the MCS console attributes in a
* segment it displays as OPERPARM, but the administrative keyword is MCSAUTH
* (see the MCS console facility topic in the TSS documentation).
TSS ADDTO(USERID) MCSAUTH(INFO)

* Broad MVS command authority for one user, if a site wants it.  Requires the
* MVS. prefix to be owned (above).  Review the command authorization table in
* z/OS MVS System Commands before granting this rather than per-command
* profiles -- it covers destructive commands such as FORCE and VARY.
TSS PERMIT(USERID) OPERCMDS(MVS.) ACCESS(ALL)

* Installer authority for extattr +ap
TSS PERMIT(sysprog) IBMFAC(BPX.FILEATTR.APF) ACCESS(READ)
```
