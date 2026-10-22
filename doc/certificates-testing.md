# Testing certificates and key rings

The native suite in [`native/c/test/zkr.test.cpp`](../native/c/test/zkr.test.cpp)
tests the `zkr` service layer and the `system cert` / `system keyring` commands.
The [JSON-RPC suite](../native/c/test/zo.cert.server.test.cpp) checks certificate
requests, responses, and structured errors through `zo server`. The Python suite in
[`native/python/bindings/test/test_zkr.py`](../native/python/bindings/test/test_zkr.py)
tests the same service through `zkr_py`.

## Run the suites

After building the native code and bindings on the target z/OS system, run from
the repository root:

```sh
npm run z:test -- certificate
npm run z:python:test
```

The first command selects the native certificate and JSON-RPC suites; the second
runs all Python binding suites. For installed-wheel testing,
use the [bindings test runner](../native/python/bindings/README.md#verify-the-installed-wheel)
with `--functional --owner YOURID`.

## Authority and skipped tests

| Tests | Requirements |
| --- | --- |
| Input validation, filter semantics, and error diagnostics | No certificate mutation authority |
| Read-only JSON-RPC calls | The test user can enumerate its certificates and key rings |
| Key ring lifecycle | The test user can create and remove its own key rings |
| Certificate lifecycle | Key ring authority, certificate operation authority, and a PKCS#12 fixture |

The suites probe key ring creation before running mutation tests. Missing
authority or an unavailable fixture causes the corresponding tests to skip.
Check the skipped counts and diagnostics when assessing coverage.

## Certificate fixtures

The native suite accepts a dedicated PKCS#12 fixture through `ZKR_TEST_P12`
(a USS path) and `ZKR_TEST_P12_PASS`. Local variables named `ZNP_ZKR_TEST_P12`
and `ZNP_ZKR_TEST_P12_PASS` are forwarded by `npm run z:test`; the suite accepts
both forms. The selected build profile's `testEnv` map can also supply the
unprefixed variables. Keep fixture passwords outside the repository.

When no readable fixture is supplied, the native suite attempts to generate a
temporary certificate with RACDCERT, export it as PKCS12DER, remove the generated
certificate record, and copy the exported data set to a USS file. This requires
GENCERT and EXPORT authority in addition to the lifecycle operations.

The Python suite generates fresh certificate content for each mutation test.
It requires the generated record to be removed before importing the fixture,
then verifies the imported label and owner before registering it for cleanup.
It does not use externally supplied certificates for these tests.

Use a dedicated native fixture: the lifecycle suite imports, modifies, and
deletes certificate records. Scratch rings, labels, and data sets use unique
names and are cleaned up by the suites.

## Testing other interfaces

The native and Python suites do not exercise TypeScript client code. Include SDK
and CLI integration tests when changing those interfaces. The
[API reference](apis.md#certificates) lists the certificate methods.
