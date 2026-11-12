# Testing CA Disk Archival and Restore

CA Disk is a Broadcom archival product that moves data sets off primary DASD into an archive. It is one of two supported archival mechanisms — the other being IBM DFSMShsm (see [Future Automation](#future-automation) for DFSMShsm coverage plans).

Because CA Disk processes archive requests on a deferred schedule, the end-to-end flow (archive → wait → restore) cannot be fully exercised in the automated CI suite. Manual verification should be performed a few times before each release to confirm archival and restore are working correctly.

## Prerequisites

- CA Disk must be installed and active on the target LPAR
- The `DARCHIVE` TSO command must be available in your STEPLIB or LNKLST
- `zo` must be deployed and on `PATH` (or set `ZO=/path/to/zo`)

## Running the Manual Test

A shell script is provided at `native/c/test/test.cadisk.sh`. Run it directly on the z/OS system after SSH-ing in:

```bash
./test.cadisk.sh [HLQ]
```

`HLQ` is optional and defaults to your TSO user ID. The test data set is named `<HLQ>.CADISK.TEST` and is deleted automatically when the script exits.

To use a specific `zo` binary:

```bash
ZO=/path/to/zo ./test.cadisk.sh
```

## What the Script Does

| Step | Action |
|------|--------|
| 1 | Creates a sequential data set |
| 2 | Writes content to it |
| 3 | Submits a deferred archive request via `DARCHIVE <dsn>` |
| 4 | Polls every 2 minutes until the volser changes to `ARCIVE` (CA Disk's pseudo-volume), up to ~46 minutes |
| 5 | Restores the data set with `zo data-set restore` |
| 6 | Views the restored data set and verifies the content is intact |

## Expected Output

```
==========================================
 CA Disk Archival / Restore — Manual Test
==========================================
  Data set : MYUSER.CADISK.TEST
  Max wait : 46 minutes

[1/6] Creating data set...
  ..passed!
[2/6] Writing content...
  ..passed!
[3/6] Submitting DARCHIVE request...
  ..passed!
[4/6] Polling for archival completion (CA Disk deferred — may take ~30 min)...
  Poll 1/23... not yet
  ...
  Poll 8/23... ARCIVE volser detected
  ..passed!
[5/6] Restoring archived data set...
  Data set 'MYUSER.CADISK.TEST' restored
  ..passed!
[6/6] Verifying restored content...
  ..passed!

==========================================
 All CA Disk tests passed!
==========================================
```

## Known Issues

- **`zo data-set restore` returns success for non-archived data sets** — a data set that was never archived incorrectly reports "Data set restored". Tracked in [#1007](https://github.com/zowe/zowex/issues/1007). A placeholder test exists in `zo.ds.test.cpp` (`should fail to restore a data set that was never archived`) and will be activated once the fix is in place.

## Research Notes: JCL Approach Does Not Work

An earlier implementation attempted to trigger CA Disk archival by submitting JCL directly using the `CADAMSUP` utility:

```
//CADARC$  JOB IZUACCT
//STEP1    EXEC PGM=CADAMSUP
//SYSPRINT DD SYSOUT=*
//SYSIN    DD *
  ARCHIVE DATASET(<dsn>)
/*
```

Followed by a second job to force the scheduler to process the request immediately:

```
//CADSCH$  JOB IZUACCT
//STEP1    EXEC PGM=CADAMSUP
//SYSPRINT DD SYSOUT=*
//SYSIN    DD *
  START IMMEDIATE
/*
```

Both jobs were confirmed to produce bad JCL output via the CA Disk ISPF panel, and the archival did not complete. The TSO `DARCHIVE` command (used in the current script) is the correct mechanism. The deferred request is processed on CA Disk's own schedule; there is no supported way to force immediate processing from a script.

## Future Automation

The full migrate/recall implementation covers two archival mechanisms:

- **CA Disk** — Broadcom archival product (this document)
- **DFSMShsm** — IBM's Hierarchical Storage Manager, using `HMIGRATE` to migrate and `HRECALL` to recall data sets. See IBM's *DFSMShsm User's Guide* for command reference.

When the implementation story for CA Disk and DFSMShsm migrate/recall support lands in the SDK and CLI, these tests should be promoted to the automated suite. At that point:

- Re-add `ZNP_CADISK: "1"` to `config.example.yaml` under `testEnv` to forward the flag to the remote test runner
- Promote the `xit` placeholders in `zo.ds.test.cpp` to active `it()` tests with appropriate timeouts
- The `ZNP_CADISK` env var check already existed in the codebase and can be restored to gate these tests on systems where CA Disk is available
- A separate `doc/ref/ds/dfsmshsm.md` and corresponding manual test script (`test.dfsmshsm.sh`) should be created following the same pattern as this document
