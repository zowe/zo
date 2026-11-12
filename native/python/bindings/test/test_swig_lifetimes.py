"""Regression checks for objects shared between Python proxies and native DLLs."""

import gc
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import zusf_py
import zjb_py as jobs
import zds_py as ds
import zkr_py as certs


@pytest.mark.parametrize("vector_type,item_type,field", [
    (jobs.ZJobVector, jobs.ZJob, "jobid"),
    (ds.ZDSEntryVector, ds.ZDSEntry, "name"),
    (certs.ZKRCertInfoVector, certs.ZKRCertInfo, "label"),
])
def test_iteration_with_all_modules_loaded(vector_type, item_type, field):
    vector = vector_type()
    item = item_type()
    setattr(item, field, "SAMPLE")
    vector.push_back(item)
    assert [getattr(entry, field) for entry in vector] == ["SAMPLE"]
    assert list(vector_type()) == []


@pytest.mark.parametrize("parent_type,field,vector_type,item_type,item_field", [
    (certs.ZkrCertList, "items", certs.ZKRCertInfoVector, certs.ZKRCertInfo, "label"),
    (certs.ZkrRingList, "items", certs.ZKRRingEntryVector, certs.ZKRRingEntry, "name"),
    (certs.ZKRRingEntry, "certs", certs.ZKRRingCertVector, certs.ZKRRingCert, "label"),
])
def test_member_vector_retains_parent(parent_type, field, vector_type, item_type, item_field):
    parent = parent_type()
    item = item_type()
    setattr(item, item_field, "SAMPLE")
    initial = vector_type()
    initial.push_back(item)
    setattr(parent, field, initial)
    vector = getattr(parent, field)
    del parent
    gc.collect()
    assert [getattr(entry, item_field) for entry in vector] == ["SAMPLE"]
    vector.push_back(item)
    assert len(getattr(vector._zkr_owner, field)) == 2


@pytest.mark.parametrize("keep_element", [False, True])
def test_nested_ring_result_retains_storage(keep_element):
    def make_result():
        result = certs.ZkrRingList()
        ring = certs.ZKRRingEntry()
        ring.name = "TESTRING"
        certificate = certs.ZKRRingCert()
        certificate.label = "SAMPLE"
        ring.certs.push_back(certificate)
        result.items.push_back(ring)
        return result

    nested = make_result().items[0].certs
    if keep_element:
        nested = nested[0]
    gc.collect()
    if keep_element:
        assert nested.label == "SAMPLE"
    else:
        assert [entry.label for entry in nested] == ["SAMPLE"]


def test_error_diagnostic_releases_temporary_reference(tmp_path):
    empty_file = tmp_path / "empty.p12"
    empty_file.write_bytes(b"")
    with pytest.raises(certs.ZkrError, match="(?i)empty") as exc_info:
        certs.import_certificate_from_file("", "RING01", "LBL", "PERSONAL", "x", str(empty_file))
    error = exc_info.value
    assert isinstance(error.service, str)
    assert error.service
    for field in ("function_code", "saf_rc", "esm_rc", "esm_rsn", "gsk_rc"):
        assert isinstance(getattr(error, field), int)
    # Only the exception attribute and getrefcount's argument should own the string.
    reference_count = sys.getrefcount(error.service)
    assert reference_count == 2


def test_error_diagnostic_preserves_assignment_failure(tmp_path, monkeypatch):
    def reject_attribute(self, name, value):
        raise MemoryError("diagnostic assignment failed")

    monkeypatch.setattr(certs.ZkrError, "__setattr__", reject_attribute)
    empty_file = tmp_path / "empty.p12"
    empty_file.write_bytes(b"")
    with pytest.raises(MemoryError, match="diagnostic assignment failed"):
        certs.import_certificate_from_file("", "RING01", "LBL", "PERSONAL", "x", str(empty_file))
