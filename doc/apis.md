# API Matrix

### Legend

- ✅ Supported
- 🚧 Partially supported
- ❌ Not supported
- ➖ Not applicable
- _italic_ Target for MVP

## Data Sets

| Operation                | z/OSMF         | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| ------------------------ | -------------- | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| _List data sets_         | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _List data set members_  | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Read data set_          | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Read data set member_   | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Write data set_         | ✅              | ✅   | 🚧 <sup>1</sup>  | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Write data set member_  | ✅              | ✅   | 🚧 <sup>1</sup>  | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Create data set_        | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Create data set member_ | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Delete data set_        | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Delete data set member_ | ✅              | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| Recall data set          | 🚧 <sup>2</sup> | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Migrate data set         | ✅              | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Delete migrated data set | ✅              | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Rename data set          | ✅              | ✅   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Rename data set member   | ✅              | ✅   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Copy data set            | ✅              | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Compress data set        | ➖              | ❌   | ✅               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Search data sets         | 🚧 <sup>3</sup> | ❌   | 🚧 <sup>3</sup>  | ✅              | ❌                  | ✅              | ❌              | ✅       |
| Invoke AMS (VSAM)        | ✅              | ❌   | 🚧 <sup>4</sup>  | ❌              | ❌                  | ❌              | ❌              | ➖       |

1. RECFM=U (undefined record format) data sets are read-only; write attempts return an error.
2. Does not support some migration utilities like CA Disk.
3. Limited options compared to ISPF `srchfor`.
4. Some `IDCAMS` operations are supported like resolving data set aliases.

## USS Files

| Operation                | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| ------------------------ | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| _List files/directories_ | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Read USS file_          | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Write USS file_         | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Create file/directory_  | ✅      | ✅   | 🚧 <sup>1</sup>  | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _Delete file/directory_  | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| Copy file/directory      | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Move file/directory      | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ❌              | ✅       |
| _`chmod` file/directory_ | ✅      | ❌   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _`chown` file/directory_ | ✅      | ❌   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| _`chtag` USS file_       | ✅      | ❌   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅       |
| Invoke `extattr`         | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| Get ACL entries          | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| Set ACL entries          | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| Link file/directory      | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| Unlink file/directory    | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| List z/OS file systems   | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Create z/OS file system  | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Delete z/OS file system  | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Mount file system        | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Unmount file system      | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |

1. Recursive option is not supported.

## Jobs

| Operation          | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code        |
| ------------------ | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | -------------- |
| _Get job status_   | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _List jobs_        | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _List spool files_ | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _Read spool file_  | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _Get job JCL_      | ✅      | ❌   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _Submit job_       | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| _Delete job_       | ✅      | ✅   | ✅               | ✅              | ✅                  | ✅              | ✅              | ✅              |
| Cancel job         | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅ <sup>1</sup> |
| Hold job           | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ➖ <sup>2</sup> |
| Release job        | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ➖ <sup>2</sup> |
| Change job class   | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖              |

1. Does not support force, restart, dump, or purge.
2. Zowe Explorer does not support the Job Hold/Release operation.

### Job execution metadata

`listJobs` items and `getJobStatus` responses include the following optional z/OSMF-compatible fields:

| Field | Meaning |
| --- | --- |
| `execSystem` | z/OS system on which the job ran |
| `execMember` | JES2 member on which the job ran |
| `execSubmitted` | Submission timestamp (JES input end time); submitted-date sort key |
| `execStarted` | Execution start timestamp |
| `execEnded` | Execution end timestamp; completed-date sort key |

Timestamps use UTC ISO 8601 format (`YYYY-MM-DDTHH:mm:ss.SSSZ`) and preserve JES centisecond precision.
They can be compared lexicographically for chronological sorting. Unavailable fields are omitted;
clients should place jobs without the selected timestamp last rather than treating them as epoch dates.
Execution data requires a verbose JES status request, with additional I/O for each returned job.
The existing table and CSV columns are unchanged; the execution fields are available in JSON output.

## Console

| Operation                | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| ------------------------ | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| Issue MVS command        | ✅      | ❌   | ✅ <sup>1</sup>  | ✅ <sup>1</sup> | ❌                  | ✅              | ✅ <sup>1</sup> | ❌       |
| Get MVS command response | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Get MVS detect result    | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |

1. Requires the APF-authorized `zoa` binary installed on the host and ESM
   OPERCMDS authorization — see [zoa-security.md](./zoa-security.md).

## TSO

| Operation                | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| ------------------------ | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| Start TSO address space  | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Start TSO app            | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Issue TSO command        | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ✅              | ✅       |
| Get TSO command response | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Send TSO message         | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Send TSO app message     | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Ping TSO address space   | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Receive TSO message      | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |
| Receive TSO app message  | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| Stop TSO address space   | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ❌              | ❌       |

## System

| Operation       | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| --------------- | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| Display symbol  | ❌      | ❌   | ✅               | ❌              | ❌                  | ❌              | ❌              | ➖       |
| List APF libs   | ❌      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ➖       |
| List linklist   | ❌      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ➖       |
| List parmlib    | ❌      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ➖       |
| List proclib    | ❌      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ➖       |
| List subsystems | ❌      | ❌   | ✅               | ❌              | ❌                  | ❌              | ❌              | ➖       |

## Other

| Operation            | z/OSMF | FTP | `zo`<br>Backend | `zo`<br>Server | Python<br>Bindings | Node.js<br>SDK | CLI<br>Plug-in | VS Code |
| -------------------- | ------ | --- | --------------- | -------------- | ------------------ | -------------- | -------------- | ------- |
| Read system log      | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ➖       |
| Get server status    | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ❌       |
| Change user password | ✅      | ❌   | ❌               | ❌              | ❌                  | ❌              | ➖              | ➖       |
| Issue SSH command    | ✅      | ❌   | ✅               | ✅              | ❌                  | ✅              | ❌              | ❌       |
