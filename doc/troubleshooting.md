# Troubleshooting

## go: FSUM7351 not found

Ensure go is part of PATH

## Client connection error - Error: All configured authentication methods failed

Check that your username and password are correct.<br/>
For private keys, confirm that you can ssh into the LPAR/zVDT using it.

## Certificate refresh fails with SAF 8, ESM 8, reason 92

The certificate lifecycle test in `native/c/test/zkr.test.cpp`, or a certificate
operation that refreshes DIGTCERT, can fail with:

```text
IRRSDL64 REFRESH failed: SAF rc: 8, ESM rc: 8, ESM rsn: 92
```

On a RACF system, an inactive RACF subsystem address space is a known cause of
this failure. `zkr_refresh` calls the R_datalib DataRefresh function, which
requires the RACF subsystem to be running. Successful key ring creation or
certificate operations earlier in the test do not establish that refresh is
available. See IBM's [R_datalib function descriptions](https://www.ibm.com/docs/en/zos/3.2.0?topic=parameters-general-parameter-list-r-datalib).

From an operator console, check the RACF address space (substitute the site's
started task name if it differs):

```text
D A,RACF
```

If it is inactive, have an authorized operator start it using the site's RACF
procedure. For a subsystem named `RACF`, the command is:

```text
S RACF,SUB=MSTR
```

`SUB=MSTR` runs the task under the master subsystem. Confirm successful
initialization in the task log, then rerun the certificate suite:

```sh
npm run z:test -- certificates
```

For recurring failures after an IPL, ask the system programmer to review the
RACF subsystem definition in `IEFSSNxx` and the site's startup configuration.
See IBM's [Starting RACF](https://www.ibm.com/docs/en/zos/3.2.0?topic=command-starting-racf).

Reason `92` (decimal, `0x5C`) is a general internal error, so an inactive
subsystem is not its only possible cause. If RACF is already running or the
error persists, review SYSLOG and the RACF task log with the system programmer.
Check refresh authority separately: when DIGTCERT is RACLISTed, DataRefresh
requires RACF SPECIAL or class authority for DIGTCERT; permission to create a
ring alone is insufficient. See IBM's [return and reason codes](https://www.ibm.com/docs/en/zos/3.1.0?topic=library-return-reason-codes)
and [DataRefresh authorization](https://www.ibm.com/docs/en/zos/latest?topic=library-racf-authorization).

## FSUM9383 Configuration file `/etc/startup.mk' not found

You should be able to find the `startup.mk` file in `/samples`

- `cp /samples/startup.mk /etc/startup.mk` <br/>
  _source:_ https://www.ibm.com/support/pages/fsum9383-configuration-file-etcstartupmk-not-found

## Building zut.o - FSUM3221 xlc: Cannot spawn program /usr/lpp/cbclib/xlc/exe/ccndrvr

> **Note:** Since the project uses `ibm-clang++64` for C++ compilation, this issue should only occur for Metal C builds which use `xlc`.

One workaround is to add `CBC.SCCNCMP` to your system LINKLIST concatenation. Below is an example of doing this via SYSVIEW commands.

:warning: These commands could ruin your system if the linklist is corrupted. Do not modify the linklist unless you know what you are doing. :warning:

```
linklist
linkdef zo from current
linklist zo
add CBC.SCCNCMP
linkact zo
set asid 1
linkupd *
```

Note 1: You may need to run `linkact zo` after an IPL if your linklist has been reset.<br/>
Note 2: Depending on your z/OS configuration, you may need to replace `*` with your mask character. For example, `linkact zo =`

## Downloading Go dependencies: tls: failed to verify certificate: x509: certificate signed by unknown authority

Update your `config.yaml` to include this property:

- `goBuildEnv: 'GOINSECURE="*" GOPROXY=direct GIT_SSL_NO_VERIFY=true'  # Allow fetching Go dependencies`

## CEE3561S ... was not found in DLL CRTEQCXE

```
CEE3561S External function _ZNSt5__1_e13__hash_memoryEPKvm was not found in DLL CRTEQCXE
```

`zo` links dynamically against the C++ runtime (libc++) that IBM ships **inside Language Environment**, in the DLL
`CRTEQCXE`. This message means the system running the binary does not export a function the binary needs, so z/OS
cannot load it. It happens before `main()`, which is why `zo` cannot report anything useful itself.

The deciding factor is the **Language Environment maintenance (PTF) level** of the system running the binary, not just
its z/OS release — a system at a supported release can still be missing the required service. Three things contribute:

| Lever | Effect |
|---|---|
| Open XL C/C++ level used to build | Determines which libc++ symbols the binary references. Dominant factor. Open XL 2.1 is supported on z/OS 2.4/2.5/3.1; **Open XL 2.2 requires z/OS 3.1+** and references a newer libc++. |
| LE maintenance level on the target | Determines which libc++ symbols the target exports. |
| `-mzos-target` (`ZosMinLevel`) | Only the LE **system-header** API level, via `__TARGET_LIB__`. Makes no link-step change and does **not** restrict libc++, so it will not resolve this message on its own. |

### If you are running a released build

Ask your system programmer to confirm Language Environment maintenance is applied:

- **z/OS 2.5** — the LE C++ runtime service for Open XL C/C++ (the `PH45516` APAR family).
- **z/OS 3.1** — the IBM Z Distribution for Zowe program directory lists `PH53938`, `PH60056`, `PH62468` and `PH68179`
  as required for Language Environment.

### If you are building it yourself

Build with the Open XL level that matches your oldest target system. `.github/workflows/zos-build.yml` pins
`/usr/lpp/IBM/cnw/v2r1/openxl/bin` (Open XL 2.1) for exactly this reason; put the same on your `PATH` via
`preBuildCmd` in `config.yaml`.

Then make the mismatch fail at **bind** time on your build host rather than at load time on someone else's system: copy
the LE C++ side decks from a system at your oldest supported release and point `LDFLAGS` at those copies. A symbol that
release does not export becomes an unresolved external during the bind. See
[`native/c/compat/README.md`](../native/c/compat/README.md) for the member list and staging steps, and the commented
`preBuildCmd` examples in `config.example.yaml`.

To see what the binary imports today:

```sh
npm run z:imports
```

That writes `listings/runtime-imports.txt`. It reports; it does not gate.

## Every compile fails with: error: unknown argument '-mzos-target=...'

Your Open XL C/C++ level does not support the option. Clang treats an unknown `-m` argument as an error, not a warning,
so this breaks every compile. Open XL 2.1 and later accept it, and the project requires 2.1 — so this usually means the
wrong compiler is on your `PATH`. Check that first.

If you genuinely need to build without it, drop `-mzos-target` from `TARGET_FLAGS` in `native/c/toolchain.mk`. That only
removes the system-header API pin; it does not change which libc++ symbols the binary references, so it does not affect
the `CEE3561S` class of failure above. Note Open XL 2.x also rejects target levels older than `zosv2r4`.
