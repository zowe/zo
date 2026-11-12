---
name: esm-test-user
description: Create, verify, and remove z/OS test users under RACF, ACF2, or CA Top Secret, including the z/OS UNIX identity needed for SSH. Use when asked to "make a test user", "create a logonid/ACID", set a non-expiring password, unsuspend an account, or reproduce an authorization test that needs an underprivileged user. Covers detecting which ESM is active and the per-product refresh steps.
---

# Creating z/OS test users across RACF, ACF2, and Top Secret

The three external security managers are not interchangeable: command syntax,
where the z/OS UNIX identity lives, and how a change is made effective all
differ. Identify the active ESM first, then follow only that product's section.

Commands below are issued through TSO. With the `zo-ssh` skill that is
`zx tso '<command>'`; ACF2 is the exception and needs batch TSO (see below).

Consult the product's own documentation for anything beyond these paths:
*z/OS Security Server RACF Command Language Reference*, *CA ACF2 for z/OS
Administrator Guide*, *CA Top Secret for z/OS Command Functions Guide*.

> **Site policy — warn the user before creating anything.** These paths
> deliberately create accounts with non-expiring passwords (`NOINTERVAL`,
> `PASSWORD(pw,0)`, `MAXDAYS(0)`/`NOPSWD-EXP`) so non-interactive SSH keeps
> working. That very likely deviates from site password policy and can be
> flagged by security audits. Say so up front, confirm the site's security
> administration allows it (or that the user accepts the deviation on a test
> system), and treat the account as temporary: remove it with the product's
> removal steps below when testing ends.

---

## 1 - Identify the active ESM

Run these in order and stop at the first that succeeds:

| Probe | Active ESM | Evidence |
| --- | --- | --- |
| `TSS WHOAMI` | Top Secret | `TSS0303I ACIDNAME(...) TYPE(...) MODE(...)` |
| `LU <yourid>` | RACF | normal `USER=... ATTRIBUTES=...` listing |
| `ACF` | ACF2 | `ACF0C038 ACF2 LOGONID ATTRIBUTES HAVE REPLACED DEFAULT USER ATTRIBUTES` |

Confirm the enforcement mode before trusting any *denied* result — a permissive
mode makes a denial test meaningless:

- RACF: `SETROPTS LIST` (check the class is active and RACLISTed)
- ACF2: `SHOW STATE` -> expect `MODE = ABORT`
- Top Secret: `TSS MODIFY(STATUS)` or the `MODE(...)` value in `TSS WHOAMI`

Also confirm your own authority: RACF `SPECIAL`, ACF2 `SECURITY` privilege, or
Top Secret `TYPE(SCA)`/`MSCA`.

---

## 2 - Pick a free UID and check the account does not exist

A z/OS UNIX UID must be unique. Reusing a UID left behind by a previous ESM
makes existing files appear to belong to the new user.

```
RACF:  SEARCH CLASS(USER) UID(nnnn)          /* expect NO ENTRIES MEET SEARCH CRITERIA */
TSS:   TSS LIST(acid)                        /* expect TSS0314E ACID DOES NOT EXIST */
ACF2:  SET PROFILE(USER) DIVISION(OMVS) then LIST LIKE(-)
```

From USS, `id nnnn` reporting `invalid user name` also indicates the UID is
unused.

---

## 3 - RACF

```
ADDUSER  TESTUSER NAME('TEST USER') OWNER(owner) DFLTGRP(grp) PASSWORD(pw) +
         OMVS(UID(nnnn) HOME(/u/users/testuser) PROGRAM(/bin/sh))
PASSWORD USER(TESTUSER) NOINTERVAL          /* password never expires */
ALTUSER  TESTUSER PASSWORD(pw) NOEXPIRED    /* do not force a change at first logon */
LU       TESTUSER OMVS NORACF               /* verify UID / HOME / PROGRAM */
```

Removal: `DELUSER TESTUSER`.

Notes:
- The GID is not set on the user — the UNIX group identity comes from the
  default group's OMVS segment. Verify with `LG grp OMVS NORACF`; if the group
  has no GID, assign one once with `ALTGROUP grp OMVS(GID(nnnn))`. Without it
  the UNIX identity is incomplete and SSH login fails. (Sites with
  `BPX.UNIQUE.USER` auto-assignment may not need explicit values; the explicit
  form works everywhere.)
- `PASS-INTERVAL` in `LU` output shows the expiry interval; `NOINTERVAL` sets it
  to none.
- An account revoked by failed logons is cleared with
  `ALTUSER TESTUSER RESUME`.
- Resource-class changes need `SETROPTS RACLIST(<class>) REFRESH` when the class
  is RACLISTed. A refresh takes effect immediately in existing sessions.

---

## 4 - Top Secret

```
TSS CREATE(TESTUSER) TYPE(USER) NAME('TEST USER') DEPT(dept) PASSWORD(pw,0)
TSS ADDTO(TESTUSER) FAC(OPENMVS)            /* required for SSH / USS signon */
TSS ADDTO(TESTUSER) FAC(BATCH)
TSS ADDTO(TESTUSER) UID(nnnn) GROUP(grp) DFLTGRP(grp)
TSS ADDTO(TESTUSER) HOME(/u/users/testuser) OMVSPGM(/bin/sh)
TSS LIST(TESTUSER) DATA(ALL)
```

Removal: `TSS DELETE(TESTUSER)`.

Notes:
- The group ACID must carry a GID for the UNIX identity to be complete —
  check `TSS LIST(grp)` and assign once with `TSS ADDTO(grp) GID(nnnn)` if
  missing.
- `PASSWORD(pw,0)` — the trailing `0` is the expiry interval and means never.
- A DEPARTMENT is mandatory. Find one with `TSS LIST(deptname)`, or read the
  `OWNER(...)` value from `TSS WHOHAS OPERCMDS(MVS.)`.
- Clear a password-threshold suspension with `TSS REMOVE(acid) PSUSPEND`, and
  reset a password with `TSS REPLACE(acid) PASSWORD(pw,0)`.
- Passwords assigned by an administrator are not subject to the `NEWPW` syntax
  rules that apply to user-initiated changes, so a password that a rule such as
  `ID` would reject can still be assigned.
- Console attributes live in a segment displayed as `OPERPARM`, but the keyword
  is `MCSAUTH` — `TSS ADDTO(acid) MCSAUTH(INFO)`. `OPERPARM(...)` is rejected
  with `TSS0242E`.
- Authorization is cached per session. After a `TSS PERMIT` or `TSS REVOKE`,
  sign on again before retesting.

---

## 5 - ACF2

ACF2 needs **two** records: a logonid, and a USER profile record in the OMVS
division that carries the z/OS UNIX identity. Without the second, SSH login
fails with no useful diagnostic.

The `ACF` command enters a subcommand environment and cannot be driven by
`tsocmd`, so run it as batch TSO:

```jcl
//ACFCMD   JOB ,'ACF2',CLASS=A,MSGCLASS=X
//STEP1    EXEC PGM=IKJEFT01,DYNAMNBR=20
//SYSTSPRT DD  SYSOUT=*
//SYSTSIN  DD  *
 ACF
 SET LID
 INSERT TESTUSER NAME('TEST USER') PASSWORD(pw) GROUP(grp) TSO MAXDAYS(0)
 CHANGE TESTUSER NOPSWD-EXP
 SET PROFILE(USER) DIVISION(OMVS)
 INSERT TESTUSER UID(nnnn) HOME(/u/users/testuser) OMVSPGM(/bin/sh)
 LIST TESTUSER
 END
/*
```

Broadcom documents OMVS profile updates as dynamically activated, so a
directory rebuild is normally unnecessary for this record (see
[USER profile records](https://techdocs.broadcom.com/us/en/ca-mainframe-software/security/ca-acf2-for-z-os/16-0/administrating/administer-records/user-profile-records.html)).
It is cheap and harmless though — if the new user's SSH login fails with a
UID/identity lookup error, run it before debugging further:

```
F ACF2,REBUILD(USR),CLASS(P)
```

Removal: `SET LID` then `DELETE TESTUSER`, and `SET PROFILE(USER)
DIVISION(OMVS)` then `DELETE TESTUSER`.

Notes:
- The group needs a GID as well for a complete UNIX identity — if missing:
  `SET PROFILE(GROUP) DIVISION(OMVS)` then `INSERT grp GID(nnnn)`.
- `MAXDAYS(0)` means the password does not expire.
- The GSO `PSWDFRC` option forces a new password at next signon, which breaks
  non-interactive SSH. `CHANGE TESTUSER NOPSWD-EXP` clears the flag after the
  password is assigned; confirm `PSWD-EXP` is absent from the `LIST` output.
- Resource-rule changes need `F ACF2,REBUILD(<directory>)` — `OPR` for the
  resource type mapped to OPERCMDS, and so on. `SHOW CLASMAP` gives the
  mapping, which is site-defined.
- Rule changes are made with `RECKEY <key> ADD(...)` / `DELETE(...)`, which
  edits single lines. Prefer it over decompiling and recompiling a rule set:
  shared rule sets often carry access for automation IDs, and a surgical add
  can be reversed exactly.

---

## 6 - Home directory

The OMVS/logonid record only names the home directory; it does not create it.

```sh
mkdir -p /u/users/testuser
chown <uid> /u/users/testuser
chmod 700 /u/users/testuser
```

Superuser authority is usually required for the `chown`. Where the invoking
user holds it, `echo '<commands>' | su` runs a batch of commands as UID 0
without a TTY.

---

## 7 - Verify

```sh
ssh testuser@host 'id; echo $HOME'
```

`id` must report the intended UID and the home directory must resolve. Check
the effective identity rather than the definition: Top Secret's `OMVSUSR`
control option supplies a default OMVS identity to users who have no segment
of their own, so a login can succeed while the intended record is missing.

Non-interactive SSH does not source `/etc/profile` or `~/.profile`, so `PATH`
additions made there apply only to login shells. Invoke programs by full path
in scripted sessions.

---

## 8 - Cautions

- **Failed-logon thresholds revoke accounts.** RACF uses `SETROPTS` revoke
  count, Top Secret `PTHRESH`, ACF2 `MAXTRY`/`PASSLMT`. Scripted password
  authentication that retries will lock the account out; check the threshold
  before automating, and pass a single-attempt option to the SSH client.
- **Verify a denial by attempting the action as the target user.** Do not infer
  it from the definition. A permissive site default — a broad RACF `UACC`, an
  ACF2 rule granting `ROLE(-)`, an unowned Top Secret resource — will silently
  allow what the definition appears to forbid.
- **Do not reuse a UID from a previous ESM configuration** without checking
  existing file ownership; the files stay behind and reattach to whoever gets
  that UID next.
