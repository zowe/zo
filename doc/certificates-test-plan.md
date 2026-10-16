# Test plan: `system cert` / `system keyring` (certificates command group)

Status: living document, introduced with [PR #1079](https://github.com/zowe/zowex/pull/1079)
(migration of [`keyring-utilities`](https://github.com/zowe/keyring-utilities) into `zo`).

Scope: the `zkr` service layer (R_datalib / IRRSDL64 + System SSL GSKCMS), the
`system cert` / `system keyring` CLI groups, the 14 certificate JSON-RPC methods,
and their SDK/CLI TypeScript surface.

## 1. How the automation is tiered

The C++ suite (`native/c/test/zkr.test.cpp`) runs on a real z/OS system via
`npm run z:test` (CI: `.github/workflows/zos-build.yml`). It has three tiers:

| Tier | Gate | What runs |
|---|---|---|
| Unconditional | none | CLI argument/virtual-ring validation, SAF error diagnostics, `zkr_filter_certs` filter-semantics unit tests (RACDCERT LABEL parity) |
| Authority-gated | `can_mutate` probe: the test user can `zkr_new_ring` | key ring lifecycle (create / list empty / GetRingInfo / delete; delete-nonexistent diagnostics) |
| Authority + fixture | `can_mutate` **and** a PKCS#12 fixture | certificate lifecycle: import → list (incl. `--label` filter parity) → show → export PEM → set-default → connect (from ring) → trust → rename → delete from DB |

The fixture is supplied out-of-band (`ZKR_TEST_P12` = USS path to a PKCS#12
file, `ZKR_TEST_P12_PASS` = its passphrase) so no key material is committed to
the repo. Gated tests **skip** (not fail) when their gate is closed, so the
suite passes for any user — but skipped tests provide no coverage. CI currently
runs with both gates closed.

The Python bindings suite (`native/python/bindings/test/test_zkr.py`, part of
`npm run z:python:test` / `.github/workflows/zos-py-build.yml`) mirrors the same
three tiers against the `zkr_py` module (`create_keyring`, `list_certificates`,
`export_certificate`, etc. — see
[native/python/bindings/README.md](../native/python/bindings/README.md#zkr_py--certificates-and-key-rings)).
It calls the same `zkr.hpp`/`zkr.cpp` service layer **in-process** (no JSON-RPC, no
`zowex server`), so it is complementary to, not a substitute for, Item 4 below, which
drives the RPC layer out-of-process over JSON-RPC. Same gates, same expectation in
CI: the CI user has no certificate authority, so Tiers B and C skip there too.

## 2. Coverage matrix

"zowe-mcp" marks methods exercised on a real system by
[zowe-mcp#45](https://github.com/zowe/zowe-mcp/pull/45) — the field-proven
"don't break" set.

| RPC method | zowe-mcp | Automation today | Gap |
|---|---|---|---|
| `showCertificate` | ✅ | gated lifecycle | — |
| `connectCertificate` | ✅ | gated (`--from-ring`) | `--from-database` never exercised anywhere |
| `deleteCertificate` | ✅ | gated (database delete only) | ring-scoped disconnect + auto-refresh (SAF 4/4/12) untested |
| `exportCertificate` | ✅ | gated (PEM + p12, `--file` and `--dsn` incl. PDS/E member) | byte-for-byte parity against `keyring-util`/RACDCERT DSN output untested |
| `importCertificate` | ✅ | gated happy path, incl. `--dsn` (sequential + PDS/E member) | warning branches untested: already-exists (rsn 8/12/16), refresh-failed fallback |
| `setDefaultCertificate` | ✅ | gated lifecycle | — |
| `trustCertificate` | ✅ | gated (NOTRUST↔TRUST) | HIGHTRUST never exercised |
| `renameCertificate` | ✅ | gated (rename + back) | — |
| `refreshDigtcert` | ✅ | indirect only (delete auto-refresh) | no standalone test |
| `listCertificates` | — | gated list + unconditional filter unit suite | cap/`moreAvailable` never hit against a real >10-cert ring |
| `listRings` | — | gated (one call) | binary result-area parser + 256 KB truncation path: zero real exposure |
| `countRing` | — | **none** | both branches (virtual-ring enumeration, GetRingInfo sum) |
| `createKeyring` | — | gated ring lifecycle | — |
| `deleteKeyring` | — | gated (incl. error path) | — |

Layers with **no automation at any tier**: JSON-RPC dispatch + schema
validation for these 14 methods, the SDK TypeScript client, the CLI TypeScript
handlers.

## 3. Plan

### Item 1 — unlock the gated suites in CI (one-time infra)

Grant the CI user (Marist system, `SSH_MARIST_RACF_HOST`) authority to manage
**its own** rings and certificates. READ access to the FACILITY-class profiles:

```
IRR.DIGTCERT.ADD        IRR.DIGTCERT.ADDRING    IRR.DIGTCERT.ALTER
IRR.DIGTCERT.CONNECT    IRR.DIGTCERT.DELETE     IRR.DIGTCERT.DELRING
IRR.DIGTCERT.EXPORT     IRR.DIGTCERT.GENCERT    IRR.DIGTCERT.LIST
IRR.DIGTCERT.LISTRING   IRR.DIGTCERT.REMOVE
```

(or an equivalent `RDATALIB <ciuser>.**` profile). The suites are designed for
a shared system: scratch rings are namespaced `ZKRUT.*` with pid+random
suffixes and purged in `afterAll`.

The fixture can alternatively be provided per-environment via the `testEnv:`
map in `config.yaml` (already plumbed through `scripts/buildTools.ts` into the
remote `ztest_runner` invocation) — but item 2 makes that unnecessary.

### Item 2 — self-provisioning fixture (no out-of-band secret)

When `ZKR_TEST_P12` is **not** set and the authority probe passed, the suite
provisions its own throwaway fixture at run time via `tsocmd`:

1. `RACDCERT GENCERT SUBJECTSDN(CN('ZKRUT <unique>')) WITHLABEL('<unique>') SIZE(2048)`
2. `RACDCERT EXPORT (LABEL('<unique>')) DSN('<user>.ZKRUT.<unique>.P12') FORMAT(PKCS12DER) PASSWORD('<generated>')`
3. `RACDCERT DELETE (LABEL('<unique>'))` — so the exported cert is unknown to
   the RACF DB again and the subsequent import is a genuinely new certificate
4. `cp -B "//'<dsn>'" <uss tmp file>` — binary copy of the DER stream
5. `DELETE '<dsn>'` + remove the USS file in cleanup

Degrades gracefully: any step failing (typically missing
`IRR.DIGTCERT.GENCERT`/`EXPORT` authority) logs the reason and the lifecycle
suite skips exactly as it does today. An explicit `ZKR_TEST_P12` still takes
precedence, so environments with a curated fixture keep using it.

The Python lifecycle suite always generates fresh certificate content for each
test and requires successful removal of the generated RACF record before import.
It does not use curated `ZKR_TEST_P12` files for these mutating tests. Import must
return the requested label and owner without an already-existing warning before
the certificate is registered for database cleanup. Without GENCERT/EXPORT
authority, these Python tests skip.

### Item 3 — new gated tests for the uncovered paths

In priority order (zowe-mcp-used first):

1. **p12 export → re-import round trip** — export with a password, import
   under a new label, `show` both. This is also the missing PEM/PKCS#12 parity
   coverage and pins the export-password behavior.
2. **Import the same p12 twice** (different label) — deterministically hits
   the already-exists warning path (SAF 4, RACF rsn 8/12/16); assert the
   `warning` diagnostic.
3. **Ring-scoped delete** — connect a cert to two rings, disconnect from one,
   assert it remains on the other (exercises the 4/4/12 auto-refresh).
4. **`connect --from-database`** — settles empirically whether DataGetFirst on
   the virtual ring can source certificate bytes (code comment and CLI help
   currently disagree); whichever way, the test documents real behavior.
5. **`countRing`** — real ring after one import (=1) and virtual ring (>=1).
6. **Standalone `refresh`**.
7. **Real pagination** — loop `RACDCERT GENCERT` to put >10 distinct certs on
   one ring; assert `--max-entries 10` sets `moreAvailable` and `--label`
   finds the last cert. Slow (~12 RACDCERT calls): use a long `TEST_OPTIONS`
   timeout.

Stays manual / unit-level: the `listRings` 256 KB truncation path (would need
thousands of rings; better served by making the result-area size injectable
and unit-testing the parser on synthetic buffers — no z/OS needed).

### Item 4 — RPC-layer tests

The C++ tests bypass JSON-RPC dispatch, so schema/validation bugs are
invisible to them (e.g. a handler emitting a field the response schema
rejects). Add a python test (the `z:test:python` harness,
`.github/workflows/zos-py-build.yml` — infrastructure that already runs in CI)
that starts `zo server`, invokes the certificate methods, and asserts:

- request validation rejects missing required fields (`-32602`-class errors)
- responses pass their schemas (server-side response validation not tripped)
- error responses carry `safReturns` with the SAF/ESM codes
- read-only methods (`listRings`, `listCertificates`, `countRing`) work
  without mutation authority

### Manual test checklist (until items 2–4 land)

For release validation on a real system, in addition to the automated suites:

- [ ] `cert export -F p12` with `--password` (and confirm omitting `--password` is rejected)
- [ ] `cert import` over an existing certificate (label ignored warning)
- [ ] `cert delete <ring>` (disconnect) and `--database` variants
- [ ] `cert connect --from-database`
- [ ] `keyring count` for a real ring and `'*'`
- [ ] `keyring list` on a ring with more than 10 certificates, with and
      without `--label`/`--usage`
- [ ] `cert trust -s HIGHTRUST` on a CERTAUTH certificate
- [ ] byte-parity of exported PEM/p12 against `keyring-util` output for the
      same certificate
- [ ] `cert export --dsn` creates a sequential data set with RACDCERT-parity
      attributes (PS/VB/LRECL 84/BLKSIZE 27998) when it does not already exist
- [ ] `cert export --dsn LIB(MEMBER)` creates the PDS/E and member (BPAM path),
      and a second export to the same member succeeds without hanging
- [ ] `cert import --dsn` reads a PKCS#12 data set (sequential and member) and
      byte-parity of the DSN bytes against `cp -B "//'DSN'"` output
- [ ] `cert export`/`cert import` reject `--file` and `--dsn` together, and
      reject providing neither
- [ ] `cert export --dsn`/`cert import --dsn` against a password-protected data
      set returns a clean error (not a hang) -- see step 5 of the design

## 4. Running the suites

```bash
npm run z:test                       # full C++ suite on the configured system
npm run z:test -- certificates       # only the certificate suites
ZNP_ZKR_TEST_P12=/u/user/f.p12 ZNP_ZKR_TEST_P12_PASS=secret npm run z:test   # curated fixture
```

`config.yaml` (repo root, gitignored) supplies the target system. Extra env
for the remote test runner comes from the `testEnv:` map in `config.yaml`
(exact names) or local `ZNP_`-prefixed variables, which are forwarded
**verbatim** (prefix included) — the certificate suite therefore accepts both
`ZKR_TEST_P12` and `ZNP_ZKR_TEST_P12` (and the same for `..._P12_PASS`).
