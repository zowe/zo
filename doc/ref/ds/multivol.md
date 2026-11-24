# Testing Multivolume Data Sets

A multivolume data set spans more than one volume when the first one runs out of space.

## Option 1: ISPF (Manual)

Use **ISPF 3.2** to allocate a data set and select the "Multiple Volumes" option. Enter multiple volume serial names when prompted (e.g. `VOL001` and `VOL002`). For a multivolume data set, ISPF requires the directory blocks to be 0 and the data set type to be blank.

## Option 2: IEFBR14 (Automation-Friendly)

Use an `IEFBR14` step in JCL to allocate the data set with multiple volumes as indicated by the ",2" in the `UNIT` parameter.

```
//STEP1   EXEC PGM=IEFBR14
//DD1     DD DSN=IBMUSER.TEST.MULTIVOL,DISP=(NEW,CATLG),
//           UNIT=(3390,2),SPACE=(CYL,(1,1)),
//           VOL=SER=(VOL001,VOL002),
//           DCB=(RECFM=FB,LRECL=80,BLKSIZE=0)
```

## Verify

When a multivolume data set is listed with `zo`, the volser column shows a `+` suffix (e.g., `VOL001+`).

```bash
zo ds list IBMUSER.TEST.MULTIVOL --attributes
```

When a multivolume data set is listed by the client, the ZRS server returns data set attributes as a JSON object. A multivolume data set contains the following properties:

| Key         | Value                  |
| ----------- | ---------------------- |
| multivolume | `true`                 |
| volume      | `"VOL001"`             |
| volumes     | `["VOL001", "VOL002"]` |

## Cleanup

No special steps are needed to delete multivolume data sets.

```bash
zo ds delete IBMUSER.TEST.MULTIVOL
```
