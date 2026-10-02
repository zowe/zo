"""Tests for the zkr_py certificate/key ring bindings.

Certificate and key ring operations require ESM authority that not every build user
has, so mutating tests are gated behind a runtime capability probe (`can_mutate`
fixture) and a PKCS#12 fixture probe (`p12_fixture`): where authority or a fixture is
missing they are skipped, not failed, mirroring the three-tier gating in
native/c/test/zkr.test.cpp (see `itif`/`can_mutate` there). Tier A (no authority) is
deliberately broad so the ASCII/EBCDIC and `bytes` boundaries get CI coverage even for
a user with no certificate authority at all.
"""

import os
import random
import string
import subprocess
import sys

import pytest
import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import zkr_py as zkr

FIXTURES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
ENV_FIXTURE_PATH = os.path.join(FIXTURES_PATH, "env.yml")

with open(ENV_FIXTURE_PATH, "r") as _env_yml:
    _env_parsed = yaml.safe_load(_env_yml)
OWNER = _env_parsed["OWNER"]
KEYRING_PREFIX = _env_parsed.get("KEYRING_PREFIX", "ZKRUT")


def _unique():
    """A collision-resistant suffix for scratch ring/label names (pid + random), capped at 8 chars."""
    pid = str(os.getpid())[-4:]
    return pid + "".join(random.choices(string.ascii_uppercase + string.digits, k=4))


def _env(name, znp_name):
    """Reads an env var that may arrive either under its own name or with the ZNP_
    prefix (buildTools.ts forwards local ZNP_* variables to the remote runner
    verbatim, prefix included)."""
    return os.environ.get(name) or os.environ.get(znp_name) or ""


def _try_del_ring(owner, ring):
    try:
        zkr.delete_keyring(owner, ring)
    except Exception:
        pass


def _try_purge_cert(owner, label):
    try:
        zkr.delete_certificate(owner, "", label, database=True, skip_refresh=True)
    except Exception:
        pass


def _try_delete_dsn(dsn):
    try:
        subprocess.run(f"tsocmd \"DELETE '{dsn}'\"", shell=True, capture_output=True)
    except Exception:
        pass


def _tsocmd(inner_cmd):
    """Runs `tsocmd "<inner_cmd>"` and returns (rc, decoded_output). tsocmd's output is
    EBCDIC, so decode explicitly (cp1047) instead of subprocess's text=True, which
    assumes the process's own codeset."""
    p = subprocess.run(f'tsocmd "{inner_cmd}"', shell=True, capture_output=True)
    out = (p.stdout + p.stderr).decode("cp1047", errors="replace")
    return p.returncode, out


def _generate_p12_fixture(owner, password):
    """Self-provision a throwaway PKCS#12 fixture via RACDCERT (see
    doc/certificates-test-plan.md, item 2): GENCERT a scratch certificate, EXPORT it as
    PKCS12DER to a scratch data set, then DELETE the ESM record (so a later import sees
    a brand-new certificate). Returns the data set name -- import_certificate_from_dsn
    reads it directly, no USS copy hop needed, unlike the C++ fixture this mirrors.
    Returns ("", note) when any step fails (typically missing
    IRR.DIGTCERT.GENCERT/EXPORT authority).
    """
    uniq = "Z" + "".join(random.choices(string.ascii_uppercase, k=6))
    label = KEYRING_PREFIX[:4] + "FIX" + uniq
    dsn = f"{owner}.{KEYRING_PREFIX}.{uniq}.P12"

    rc, out = _tsocmd(f"RACDCERT GENCERT SUBJECTSDN(CN('ZKRUT FIXTURE')) WITHLABEL('{label}') SIZE(2048)")
    if rc != 0:
        return "", f"RACDCERT GENCERT failed: {out}"

    rc, out = _tsocmd(f"RACDCERT EXPORT (LABEL('{label}')) DSN('{dsn}') FORMAT(PKCS12DER) PASSWORD('{password}')")
    _tsocmd(f"RACDCERT DELETE (LABEL('{label}'))")
    if rc != 0:
        return "", f"RACDCERT EXPORT failed: {out}"
    return dsn, ""


def _import_from(source, owner, ring, label, usage, password, skip_refresh=False):
    """Imports via whichever source p12_fixture yielded: a curated file, or a
    self-provisioned data set (import_certificate_from_dsn reads it directly)."""
    if "file" in source:
        return zkr.import_certificate_from_file(owner, ring, label, usage, password, source["file"], skip_refresh)
    return zkr.import_certificate_from_dsn(owner, ring, label, usage, password, source["dsn"], skip_refresh)


@pytest.fixture(scope="module")
def can_mutate():
    """Probes key ring create authority once for the whole module (mirrors
    zkr.test.cpp's can_mutate). Tier B/C tests skip when this is False."""
    ring = f"{KEYRING_PREFIX}.PROBE." + _unique()
    try:
        zkr.create_keyring(OWNER, ring)
    except zkr.ZkrError as e:
        # Not authorized (or otherwise unavailable): the failure must be a clean SAF
        # diagnostic (>= 8), not a crash or empty message.
        assert str(e)
        assert e.saf_rc >= 8
        return False
    zkr.delete_keyring(OWNER, ring)
    return True


@pytest.fixture(scope="module")
def p12_fixture(can_mutate):
    """Yields (source, password) where source is {"file": path} or {"dsn": dsn},
    consumed by _import_from. Uses the curated ZKR_TEST_P12 / ZKR_TEST_P12_PASS fixture
    when available, otherwise self-provisions one via RACDCERT. Skips when neither is
    available (e.g. no GENCERT/EXPORT authority)."""
    p12_path = _env("ZKR_TEST_P12", "ZNP_ZKR_TEST_P12")
    p12_pass = _env("ZKR_TEST_P12_PASS", "ZNP_ZKR_TEST_P12_PASS") or "password"

    if p12_path and os.access(p12_path, os.R_OK):
        yield {"file": p12_path}, p12_pass
        return

    if not can_mutate:
        pytest.skip("no ZKR_TEST_P12 fixture and no key ring create authority to self-provision one")

    gen_pass = KEYRING_PREFIX[:4] + "GEN" + _unique()
    dsn, note = _generate_p12_fixture(OWNER, gen_pass)
    if not dsn:
        pytest.skip(f"could not self-provision a PKCS#12 fixture via RACDCERT: {note}")
    try:
        yield {"dsn": dsn}, gen_pass
    finally:
        _try_delete_dsn(dsn)


@pytest.fixture
def cleanup():
    """Per-test teardown registry for scratch rings/labels/data sets. Populated up
    front by each test so a mid-test failure still cleans up, mirroring the
    afterAll-based cleanup in zkr.test.cpp."""
    registry = {"rings": [], "labels": [], "dsns": []}
    yield registry
    for label in registry["labels"]:
        _try_purge_cert(OWNER, label)
    for ring in registry["rings"]:
        _try_del_ring(OWNER, ring)
    for dsn in registry["dsns"]:
        _try_delete_dsn(dsn)


# ---------------------------------------------------------------------------
# Tier A -- no authority required. Deterministic; these are the ones that will
# actually run in CI.
# ---------------------------------------------------------------------------


class TestModuleShape:
    def test_module_exposes_all_functions_and_error_type(self):
        expected = [
            "create_keyring", "delete_keyring", "list_rings", "count_ring", "refresh_digtcert",
            "list_certificates", "show_certificate", "set_default_certificate", "connect_certificate",
            "delete_certificate", "trust_certificate", "rename_certificate",
            "export_certificate", "export_certificate_to_file", "export_certificate_to_dsn",
            "import_certificate", "import_certificate_from_file", "import_certificate_from_dsn",
        ]
        for name in expected:
            assert hasattr(zkr, name), f"zkr_py is missing {name}"
            assert callable(getattr(zkr, name))
        assert hasattr(zkr, "ZkrError")
        assert issubclass(zkr.ZkrError, Exception)

    def test_struct_field_access(self):
        """Constructing the redeclared ZKR* structs directly and reading/writing their
        fields guards against a silent opaque-pointer failure if the struct
        redeclaration in zkr_py.i ever drifts from zkr.hpp's actual layout."""
        detail = zkr.ZKRCertDetail()
        detail.label = "TESTLABEL"
        detail.owner = "TESTUSER"
        assert detail.label == "TESTLABEL"
        assert detail.owner == "TESTUSER"

        entry = zkr.ZKRRingEntry()
        entry.owner = "TESTUSER"
        entry.name = "TESTRING"
        assert entry.name == "TESTRING"
        assert list(entry.certs) == []

        ring_cert = zkr.ZKRRingCert()
        ring_cert.label = "X"
        assert ring_cert.label == "X"


class TestValidation:
    """Direct analogue of zkr.test.cpp's "CLI input validation" block: every
    validation rule from the Python glue (zkr_py.cpp) raises before any ESM call."""

    def test_create_keyring_rejects_virtual_ring(self):
        with pytest.raises(ValueError, match="virtual key ring"):
            zkr.create_keyring(OWNER, "*")

    def test_delete_keyring_rejects_virtual_ring(self):
        with pytest.raises(ValueError, match="virtual key ring"):
            zkr.delete_keyring(OWNER, "*")

    def test_set_default_certificate_rejects_virtual_ring(self):
        with pytest.raises(ValueError, match="virtual key ring"):
            zkr.set_default_certificate(OWNER, "*", "LBL")

    def test_connect_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.connect_certificate(OWNER, "RING01", "", from_ring="RING02")

    def test_connect_certificate_rejects_virtual_ring(self):
        with pytest.raises(ValueError, match="virtual key ring"):
            zkr.connect_certificate(OWNER, "*", "LBL", from_ring="RING02")

    def test_connect_certificate_rejects_from_ring_star(self):
        with pytest.raises(ValueError, match="from_database"):
            zkr.connect_certificate(OWNER, "RING01", "LBL", from_ring="*")

    def test_connect_certificate_rejects_from_ring_and_from_database_together(self):
        with pytest.raises(ValueError, match="not both"):
            zkr.connect_certificate(OWNER, "RING01", "LBL", from_ring="RING02", from_database=True)

    def test_connect_certificate_requires_from_ring_or_from_database(self):
        with pytest.raises(ValueError, match="from_ring"):
            zkr.connect_certificate(OWNER, "RING01", "LBL")

    def test_delete_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.delete_certificate(OWNER, "RING01", "")

    def test_delete_certificate_rejects_keyring_star(self):
        with pytest.raises(ValueError, match="database=True"):
            zkr.delete_certificate(OWNER, "*", "LBL")

    def test_delete_certificate_rejects_keyring_and_database_together(self):
        with pytest.raises(ValueError, match="not both"):
            zkr.delete_certificate(OWNER, "RING01", "LBL", database=True)

    def test_delete_certificate_requires_keyring_or_database(self):
        with pytest.raises(ValueError, match="database=True"):
            zkr.delete_certificate(OWNER, "", "LBL")

    def test_trust_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.trust_certificate(OWNER, "", "TRUST")

    def test_trust_certificate_requires_status(self):
        with pytest.raises(ValueError, match="status is required"):
            zkr.trust_certificate(OWNER, "LBL", "")

    def test_rename_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.rename_certificate(OWNER, "", "NEWLBL")

    def test_rename_certificate_requires_new_label(self):
        with pytest.raises(ValueError, match="new_label is required"):
            zkr.rename_certificate(OWNER, "LBL", "")

    def test_show_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.show_certificate(OWNER, "RING01", "")

    def test_export_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.export_certificate(OWNER, "RING01", "")

    def test_export_certificate_p12_requires_password(self):
        with pytest.raises(ValueError, match="password is required"):
            zkr.export_certificate(OWNER, "RING01", "LBL", format="p12")

    def test_list_certificates_rejects_negative_max_entries(self):
        with pytest.raises(ValueError, match="max_entries"):
            zkr.list_certificates(OWNER, "RING01", max_entries=-1)

    def test_import_certificate_requires_label(self):
        with pytest.raises(ValueError, match="label is required"):
            zkr.import_certificate(OWNER, "RING01", "", "PERSONAL", "pw", b"data")

    def test_import_certificate_requires_usage(self):
        with pytest.raises(ValueError, match="usage is required"):
            zkr.import_certificate(OWNER, "RING01", "LBL", "", "pw", b"data")

    def test_import_certificate_requires_password(self):
        with pytest.raises(ValueError, match="password is required"):
            zkr.import_certificate(OWNER, "RING01", "LBL", "PERSONAL", "", b"data")

    def test_import_certificate_rejects_virtual_ring(self):
        with pytest.raises(ValueError, match="virtual key ring"):
            zkr.import_certificate(OWNER, "*", "LBL", "PERSONAL", "pw", b"data")

    def test_import_certificate_rejects_non_bytes_data(self):
        """Proves the `in` typemap for ZkrBytes is wired, not a `str` fallback: SWIG's
        typecheck rejects a str argument before the C++ side ever runs."""
        with pytest.raises(TypeError):
            zkr.import_certificate(OWNER, "RING01", "LBL", "PERSONAL", "pw", "not bytes")

    def test_import_certificate_from_file_rejects_empty_file(self, tmp_path):
        p12_file = tmp_path / "empty.p12"
        p12_file.write_bytes(b"")
        with pytest.raises(zkr.ZkrError, match="(?i)empty"):
            zkr.import_certificate_from_file(OWNER, "RING01", "LBL", "PERSONAL", "x", str(p12_file))


class TestDiagnostics:
    """Proves the diagnostic conversion (no EBCDIC leakage, no empty message) and that
    the returned proxies survive basic field access, without needing any authority."""

    def test_list_certificates_nonexistent_ring_raises_zkrerror(self):
        ring = f"{KEYRING_PREFIX}.NOPE." + _unique()
        with pytest.raises(zkr.ZkrError) as exc_info:
            zkr.list_certificates(OWNER, ring)
        e = exc_info.value
        assert str(e)
        assert e.saf_rc >= 8
        assert isinstance(e.esm_rc, int)
        assert isinstance(e.esm_rsn, int)
        assert isinstance(e.function_code, int)

    def test_list_certificates_virtual_ring_does_not_crash(self):
        # "*" (the virtual key ring) may succeed (possibly empty) or be denied; either
        # way it must return a coherent result with a populated diagnostic on failure.
        try:
            result = zkr.list_certificates(OWNER, "*")
            assert hasattr(result, "items")
            assert hasattr(result, "more_available")
            for cert in result.items:
                assert isinstance(cert.label, str)
        except zkr.ZkrError as e:
            assert str(e)

    def test_list_rings_does_not_crash(self):
        try:
            result = zkr.list_rings(OWNER)
            assert hasattr(result, "items")
            assert hasattr(result, "warning")
            for ring in result.items:
                assert isinstance(ring.name, str)
                assert isinstance(ring.owner, str)
                for cert in ring.certs:
                    assert isinstance(cert.label, str)
        except zkr.ZkrError as e:
            assert str(e)


# ---------------------------------------------------------------------------
# Tier B -- requires key ring create authority (the `can_mutate` probe). Skipped,
# not failed, where authority is missing.
# ---------------------------------------------------------------------------


class TestKeyRingLifecycle:
    def test_creates_lists_and_deletes_a_key_ring(self, can_mutate, cleanup):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        ring = f"{KEYRING_PREFIX}.LIFE." + _unique()
        cleanup["rings"].append(ring)

        warning = zkr.create_keyring(OWNER, ring)
        assert isinstance(warning, str)

        # A brand-new ring has no certificates (empty list, not an error).
        certs = zkr.list_certificates(OWNER, ring)
        assert len(certs.items) == 0
        assert zkr.count_ring(OWNER, ring) == 0

        # GetRingInfo enumeration should succeed for the owner's rings.
        rings = zkr.list_rings(OWNER, ring)
        assert any(r.name.upper() == ring.upper() for r in rings.items)

        zkr.delete_keyring(OWNER, ring)

    def test_deleting_nonexistent_key_ring_raises_clean_error(self, can_mutate):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        ring = f"{KEYRING_PREFIX}.GHOST." + _unique()
        with pytest.raises(zkr.ZkrError) as exc_info:
            zkr.delete_keyring(OWNER, ring)
        assert str(exc_info.value)


# ---------------------------------------------------------------------------
# Tier C -- requires key ring create authority AND a PKCS#12 fixture (curated via
# ZKR_TEST_P12, or self-provisioned via RACDCERT). This is the new coverage for the
# zkr_py module: byte-exactness of the `bytes` boundary, PEM ASCII conversion vs.
# EBCDIC-on-disk parity, and DSN/PDS-E round trips.
# ---------------------------------------------------------------------------


class TestCertificateLifecycle:
    def test_full_lifecycle(self, can_mutate, p12_fixture, cleanup):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        source, password = p12_fixture

        ring1 = f"{KEYRING_PREFIX}.CRT1." + _unique()
        ring2 = f"{KEYRING_PREFIX}.CRT2." + _unique()
        label = KEYRING_PREFIX + _unique()
        cleanup["rings"] += [ring1, ring2]
        cleanup["labels"].append(label)

        zkr.create_keyring(OWNER, ring1)
        zkr.create_keyring(OWNER, ring2)

        _import_from(source, OWNER, ring1, label, "PERSONAL", password)

        # The ESM DB may already hold this cert content under an earlier label; use
        # whatever label actually landed on the ring for the rest of the flow.
        certs = zkr.list_certificates(OWNER, ring1)
        assert len(certs.items) >= 1
        real_label = certs.items[0].label
        cleanup["labels"].append(real_label)

        # 6. Filter parity: RACDCERT LABEL is exact and case-sensitive, so an exact
        # --label filter finds it; a lowercased one does not (both succeed -- an
        # empty result is not an error).
        exact = zkr.list_certificates(OWNER, ring1, label=real_label)
        assert len(exact.items) == 1
        lower_label = real_label.lower()
        if lower_label != real_label:
            miss = zkr.list_certificates(OWNER, ring1, label=lower_label)
            assert len(miss.items) == 0

        # 7. show_certificate: fields are readable ASCII.
        detail = zkr.show_certificate(OWNER, ring1, real_label)
        assert detail.label == real_label
        assert isinstance(detail.subject, str)
        assert isinstance(detail.serial_number, str)
        assert isinstance(detail.not_before, str)
        assert isinstance(detail.not_after, str)

        # 4. PEM export is portable ASCII (catches a missing e2a on the PEM path).
        pem = zkr.export_certificate(OWNER, ring1, real_label)
        assert isinstance(pem, bytes)
        assert pem.startswith(b"-----BEGIN CERTIFICATE-----")
        assert pem.endswith(b"-----END CERTIFICATE-----\n")

        # set_default_certificate -> is_default flips in list_certificates.
        zkr.set_default_certificate(OWNER, ring1, real_label)
        refreshed = zkr.list_certificates(OWNER, ring1, label=real_label)
        assert refreshed.items[0].is_default is True

        # connect_certificate from a second ring.
        zkr.connect_certificate(OWNER, ring2, real_label, from_ring=ring1, usage="PERSONAL")
        assert len(zkr.list_certificates(OWNER, ring2).items) >= 1

        # trust_certificate: NOTRUST <-> TRUST.
        zkr.trust_certificate(OWNER, real_label, "NOTRUST")
        zkr.trust_certificate(OWNER, real_label, "TRUST")

        # rename_certificate and back, so cleanup by real_label still works.
        new_label = "ZKRUTREN" + _unique()
        zkr.rename_certificate(OWNER, real_label, new_label)
        zkr.rename_certificate(OWNER, new_label, real_label)

        # delete_certificate(database=True): removes it from the DB (and every ring).
        zkr.delete_certificate(OWNER, "", real_label, database=True)

    def test_pem_export_to_file_stays_ebcdic_and_private(self, can_mutate, p12_fixture, cleanup, tmp_path):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        source, password = p12_fixture

        ring = f"{KEYRING_PREFIX}.FIL." + _unique()
        label = KEYRING_PREFIX + "FIL" + _unique()
        cleanup["rings"].append(ring)
        cleanup["labels"].append(label)

        zkr.create_keyring(OWNER, ring)
        _import_from(source, OWNER, ring, label, "PERSONAL", password)
        certs = zkr.list_certificates(OWNER, ring)
        real_label = certs.items[0].label
        cleanup["labels"].append(real_label)

        # 5. PEM on disk stays EBCDIC -- deliberate keyring-util parity (D11), locked
        # in so nobody "fixes" it later, plus the private (0600) file mode.
        out_file = str(tmp_path / "cert.pem")
        n = zkr.export_certificate_to_file(OWNER, ring, real_label, out_file)
        assert n > 0

        with open(out_file, "rb") as f:
            raw = f.read()
        assert not raw.startswith(b"-----BEGIN CERTIFICATE-----")
        assert oct(os.stat(out_file).st_mode & 0o777) == "0o600"

    def test_p12_export_is_binary_and_round_trips(self, can_mutate, p12_fixture, cleanup):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        source, password = p12_fixture

        ring1 = f"{KEYRING_PREFIX}.RT1." + _unique()
        ring2 = f"{KEYRING_PREFIX}.RT2." + _unique()
        label = KEYRING_PREFIX + "RT" + _unique()
        cleanup["rings"] += [ring1, ring2]
        cleanup["labels"].append(label)

        zkr.create_keyring(OWNER, ring1)
        zkr.create_keyring(OWNER, ring2)
        _import_from(source, OWNER, ring1, label, "PERSONAL", password)

        certs = zkr.list_certificates(OWNER, ring1)
        real_label = certs.items[0].label
        cleanup["labels"].append(real_label)

        export_password = "ZKRUTEXP" + _unique()
        data = zkr.export_certificate(OWNER, ring1, real_label, format="p12", password=export_password)

        # 1. Byte-exactness: DER PKCS#12 (SEQUENCE tag 0x30), never valid UTF-8/text.
        assert isinstance(data, bytes)
        assert data[0] == 0x30
        assert b"\x00" in data
        assert len(data) > 1000

        # 2. bytes round trip: import the exported bytes under a new label onto ring2.
        # The ESM already holds this certificate, so the warning is non-empty
        # (already-exists case) or empty, either is a clean outcome.
        new_label = "ZKRUTN" + _unique()
        cleanup["labels"].append(new_label)
        warning = zkr.import_certificate(OWNER, ring2, new_label, "PERSONAL", export_password, data)
        assert isinstance(warning, str)

        # 3. DSN parity: a sequential data set and a PDS/E member (the BPAM path).
        # PKCS#12 encryption is salted (two exports are never byte-identical), so
        # `n == len(data)` is what proves the bytes survived the data set unmangled --
        # not a byte comparison against `data` itself.
        dsn_label = "ZKRUTN2" + _unique()
        seq_dsn = f"{OWNER}.{KEYRING_PREFIX}." + _unique() + ".P12"
        cleanup["dsns"].append(seq_dsn)
        cleanup["labels"].append(dsn_label)
        n = zkr.export_certificate_to_dsn(OWNER, ring1, real_label, seq_dsn, format="p12", password=export_password)
        assert n == len(data)
        zkr.import_certificate_from_dsn(OWNER, ring2, dsn_label, "PERSONAL", export_password, seq_dsn)

        lib_label = "ZKRUTN3" + _unique()
        lib_dsn = f"{OWNER}.{KEYRING_PREFIX}." + _unique() + ".LIB"
        member_dsn = f"{lib_dsn}(CERT01)"
        cleanup["dsns"].append(lib_dsn)
        cleanup["labels"].append(lib_label)
        n = zkr.export_certificate_to_dsn(
            OWNER, ring1, real_label, member_dsn, format="p12", password=export_password
        )
        assert n == len(data)
        zkr.import_certificate_from_dsn(OWNER, ring2, lib_label, "PERSONAL", export_password, member_dsn)

        # A subsequent export to the same member must succeed, not hang -- proves the
        # BPAM ENQ/RESERVE from the first export released.
        n = zkr.export_certificate_to_dsn(
            OWNER, ring1, real_label, member_dsn, format="p12", password=export_password
        )
        assert n == len(data)

    def test_count_refresh_disconnect_and_connect_from_database(self, can_mutate, p12_fixture, cleanup):
        if not can_mutate:
            pytest.skip("no key ring create authority")
        source, password = p12_fixture

        ring1 = f"{KEYRING_PREFIX}.CNT1." + _unique()
        ring2 = f"{KEYRING_PREFIX}.CNT2." + _unique()
        label = KEYRING_PREFIX + "CNT" + _unique()
        cleanup["rings"] += [ring1, ring2]
        cleanup["labels"].append(label)

        zkr.create_keyring(OWNER, ring1)
        zkr.create_keyring(OWNER, ring2)
        _import_from(source, OWNER, ring1, label, "PERSONAL", password)

        certs = zkr.list_certificates(OWNER, ring1)
        real_label = certs.items[0].label
        cleanup["labels"].append(real_label)

        assert zkr.count_ring(OWNER, ring1) == 1
        # Virtual ring count (DataGetFirst/GetNext enumeration) must not crash.
        assert zkr.count_ring(OWNER, "*") >= 1

        # Ring-scoped delete: disconnect from ring1 only (exercises the 4/4/12
        # auto-refresh); the DB record survives (it is still reachable from "*").
        zkr.delete_certificate(OWNER, ring1, real_label)
        assert len(zkr.list_certificates(OWNER, ring1).items) == 0

        # Re-connect to ring1, sourcing the bytes from the virtual key ring -- the
        # service path behind `cert connect --from-database`.
        zkr.connect_certificate(OWNER, ring1, real_label, from_database=True)
        assert len(zkr.list_certificates(OWNER, ring1).items) >= 1

        warning = zkr.refresh_digtcert()
        assert isinstance(warning, str)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
