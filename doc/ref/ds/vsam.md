# VSAM Data Sets

## Overview

**VSAM** (Virtual Storage Access Method) organizes data in clusters. Each cluster has a data component and, for keyed types, an index component. Two additional catalog entry types provide alternate key access:

- **Alternate Index (AIX)**: defines a secondary key over the base cluster records, allowing access by a different field without duplicating data.
- **Path**: associates an AIX with the base cluster so applications can open the path name to access records through the alternate key.

## KSDS (Key-Sequenced)

A KSDS stores records ordered by a unique primary key at a fixed offset. VSAM maintains an index for fast random and sequential access.

```
//VSAMSET$ JOB IZUACCT
//STEP1    EXEC PGM=IDCAMS
//SYSPRINT DD SYSOUT=*
//SYSIN    DD *
  DEFINE CLUSTER ( -
    NAME(IBMUSER.DATA.KSDS) -
    INDEXED -
    KEYS(8 0) -
    RECORDSIZE(80 80) -
    TRACKS(5 5) -
    SHAREOPTIONS(2 3) )
  DEFINE AIX ( -
    NAME(IBMUSER.DATA.KAIX) -
    RELATE(IBMUSER.DATA.KSDS) -
    KEYS(8 32) -
    RECORDSIZE(80 80) -
    TRACKS(5 5) -
    SHAREOPTIONS(2 3) -
    UPGRADE )
  DEFINE PATH ( -
    NAME(IBMUSER.DATA.KPTH) -
    PATHENTRY(IBMUSER.DATA.KAIX) -
    UPDATE )
/*
```

`INDEXED` and `KEYS` on the cluster distinguish a KSDS from other VSAM types.

If the base cluster already contains records, run `BLDINDEX` after defining the AIX to populate it:

```
  BLDINDEX -
    INDATASET(IBMUSER.DATA.KSDS) -
    OUTDATASET(IBMUSER.DATA.KAIX)
```

`BLDINDEX` requires at least one record in the base cluster. It fails on an empty KSDS with VSAM OPEN return code 160.

## ESDS (Entry-Sequenced)

An ESDS stores records in the order they are inserted; there is no primary key or index component. Records are addressed by their RBA (Relative Byte Address). An AIX can still be defined over an ESDS to provide keyed access.

```
//VSAMSET$ JOB IZUACCT
//STEP1    EXEC PGM=IDCAMS
//SYSPRINT DD SYSOUT=*
//SYSIN    DD *
  DEFINE CLUSTER ( -
    NAME(IBMUSER.DATA.ESDS) -
    NONINDEXED -
    RECORDSIZE(80 80) -
    TRACKS(5 5) -
    SHAREOPTIONS(2 3) )
  DEFINE AIX ( -
    NAME(IBMUSER.DATA.EAIX) -
    RELATE(IBMUSER.DATA.ESDS) -
    KEYS(8 0) -
    RECORDSIZE(80 80) -
    TRACKS(5 5) -
    SHAREOPTIONS(2 3) -
    UPGRADE )
  DEFINE PATH ( -
    NAME(IBMUSER.DATA.EPTH) -
    PATHENTRY(IBMUSER.DATA.EAIX) -
    UPDATE )
/*
```

`NONINDEXED` and no `KEYS` on the cluster define an ESDS. The AIX still requires `KEYS` since it defines the alternate key over the ESDS records.

## Parameters

| Parameter | Description |
| --- | --- |
| `INDEXED` / `NONINDEXED` | KSDS or ESDS cluster type |
| `KEYS(length offset)` | Primary/alternate key length and byte offset in the record |
| `RECORDSIZE(avg max)` | Average and maximum record size (equal for fixed-length) |
| `TRACKS(primary secondary)` | Space allocation in tracks |
| `SHAREOPTIONS(cross-region cross-system)` | Concurrent access rules |
| `RELATE` | Base cluster the AIX is built over |
| `UPGRADE` | Keeps the AIX in sync when the base cluster is updated |
| `PATHENTRY` | AIX the path provides access through |

## Verify

When listed with `zo`, each component reports distinct dsorg and volser values:

```bash
zo ds list "IBMUSER.DATA.*" --attributes
```

| Entry | dsorg | volser |
| --- | --- | --- |
| Cluster (KSDS or ESDS) | `VS` | `*VSAM*` |
| Alternate index | _(empty)_ | `*AIX *` |
| Path | _(empty)_ | `*PATH*` |

## Cleanup

Deleting the base cluster with `PURGE` also deletes the AIX and path:

```
//VSAMDEL$ JOB IZUACCT
//STEP1    EXEC PGM=IDCAMS
//SYSPRINT DD SYSOUT=*
//SYSIN    DD *
  DELETE IBMUSER.DATA.KSDS CLUSTER PURGE
  DELETE IBMUSER.DATA.ESDS CLUSTER PURGE
/*
```
