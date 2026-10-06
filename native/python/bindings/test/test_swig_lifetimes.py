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
