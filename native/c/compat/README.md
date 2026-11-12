# Runtime compatibility

`zo` is built on one LPAR and the resulting `server.pax.Z` ships to every end-user system. It links
dynamically against the C++ runtime (libc++) that IBM ships **inside Language Environment**, in the DLL
`CRTEQCXE`. If the build references a symbol the target system's LE does not export, `zo` fails at
**load** time, before `main()`:

```
CEE3561S External function _ZNSt5__1_e13__hash_memoryEPKvm was not found in DLL CRTEQCXE
```

Nothing in the binary can diagnose that, because the binary never runs. See
[zowe/zowex#871](https://github.com/zowe/zowex/issues/871) and
[`doc/troubleshooting.md`](../../../doc/troubleshooting.md).

## What determines compatibility

Three separate things, often confused:

| Lever                                                | What it controls                                                                                                                                                                                                                 |
| ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Open XL C/C++ level** used to build                | Which libc++ symbols the binary references. This is the dominant factor. Open XL 2.1 is supported on z/OS 2.4/2.5/3.1; **Open XL 2.2 requires z/OS 3.1+** and references a newer libc++.                                         |
| **LE maintenance (PTF) level** on the target         | Which libc++ symbols the target actually exports. A z/OS release alone does not tell you this.                                                                                                                                   |
| **`-mzos-target`** (`ZosMinLevel` in `toolchain.mk`) | Only the LE **system-header** API level, via `__TARGET_LIB__`. It makes no link-step change and does **not** restrict libc++. It catches a call to a libc function that is too new; it is not a fix for a missing libc++ export. |

The floor itself — `ZosMinLevel=zosv2r5` — is declared in
[`native/c/toolchain.mk`](../toolchain.mk) and nowhere else.

## What keeps the binary loadable

**1. The pinned compiler.** [`.github/workflows/zos-build.yml`](../../../.github/workflows/zos-build.yml)
puts Open XL 2.1 on the `PATH`. This is the dominant factor and it is what zowex#871 came down to:
the build had moved to Open XL 2.2, whose newer libc++ raises the LE level every consumer must carry.
Keep it at 2.1 for as long as z/OS 2.5 is supported.

**2. A source-level lint.** `npm run lint:tidy` ([`scripts/checkNoStringHash.js`](../../../scripts/checkNoStringHash.js))
rejects string-keyed hash containers, which pull in `std::__1_e::__hash_memory` — the specific symbol
behind zowex#871. Runs on any machine in milliseconds, no z/OS needed, and catches the regression at
the point someone writes it.

**3. Documented target requirements.** The floor is z/OS 2.5 _with current LE maintenance_; the APAR
list is in the [top-level README](../../../README.md) and `ZSshUtils` verifies the binary loads at
install time so a mismatch is reported then rather than on some later operation.

## If you need a hard guarantee: bind against floor side decks

Not currently done, and not needed while the compiler stays pinned. Worth knowing about if the floor
ever has to be enforced mechanically — for example if the build LPAR's compiler level starts moving.

The build LPAR is newer than the oldest system we support, so binding against _its_ LE side decks
proves nothing. Copy the side decks off a system at the floor release and point `LDFLAGS` at the
copies; a symbol that release does not export then becomes an unresolved external at bind time, on
the build host, in the right release's terms:

```sh
mkdir -p ~/sidedecks/zosv2r5
for m in CELQS001 CELQS003 CRTDQCXE CRTDQCXG CRTDQCXH CRTDQCXS CRTDQCXP CRTDQCXA CRTDQXLA CRTDQUNW; do
  cp "//'CEE.SCEELIB2($m)'" "~/sidedecks/zosv2r5/$m.x"
done
```

Then point `LDFLAGS` at that directory via `preBuildCmd` (see `config.example.yaml`).

This pins the **maintenance level** too, which a z/OS release number cannot: a 2.5 system with current
PTFs exports symbols that a 2.5 system without them does not. Capture from a system at the maintenance
floor you actually intend to support — capturing from a fully serviced system produces a check that
passes while protecting nobody. Record which system it came from; **a side-deck set without a recorded
maintenance level is not reproducible**, and re-capture whenever the floor moves or that system takes
LE maintenance.

## Inspecting what the binary imports

Informational, not a gate:

```sh
make runtime-imports     # on z/OS, from native/c; writes build-out/runtime-imports.txt
npm run z:imports        # or from a workstation, against the configured build system
```

CI uploads the report as the `zowex-runtime-imports` artifact. Useful when raising the floor, changing
the compiler level, or working out which symbol a `CEE3561S` is complaining about.
