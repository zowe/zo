<details>
<summary><strong>Data Set Operations</strong></summary>

| Action                     | Server Command                                                                                                                    |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| Read data set              | `{"jsonrpc":"2.0","method":"readDataset","params":{"dsname":"USER.PS"},"id":1}`                                                   |
| Read data set w/ encoding  | `{"jsonrpc":"2.0","method":"readDataset","params":{"dsname":"USER.PS","encoding":"ISO8859-1"},"id":1}`                            |
| Write data set             | `{"jsonrpc":"2.0","method":"writeDataset","params":{"dsname":"USER.PS","data":"aGVsbG8gd29ybGQ="},"id":1}`                        |
| Write data set w/ encoding | `{"jsonrpc":"2.0","method":"writeDataset","params":{"dsname":"USER.PS","data":"aGVsbG8gd29ybGQ=","encoding":"ISO8859-1"},"id":1}` |
| List data sets             | `{"jsonrpc":"2.0","method":"listDatasets","params":{"pattern":"USER.*"},"id":1}`                                                  |
| List data set members      | `{"jsonrpc":"2.0","method":"listDsMembers","params":{"dsname":"USER.PDS"},"id":1}`                                                |
| Create data set            | `{"jsonrpc":"2.0","method":"createDataset","params":{"dsname":"USER.NEW","attributes":{"primary":10,"lrecl":80}},"id":1}`         |
| Delete data set            | `{"jsonrpc":"2.0","method":"deleteDataset","params":{"dsname":"USER.OLD"},"id":1}`                                                |
| Rename data set            | `{"jsonrpc":"2.0","method":"renameDataset","params":{"dsnameBefore":"USER.OLD","dsnameAfter":"USER.NEW"},"id":1}`                 |
| Copy data set              | `{"jsonrpc":"2.0","method":"copyDataset","params":{"source":"USER.SOURCE","target":"USER.TARGET"},"id":1}`                        |

</details>

<details>
<summary><strong>USS File Operations</strong></summary>

| Action                     | Server Command                                                                                                                                |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Read USS file              | `{"jsonrpc":"2.0","method":"readFile","params":{"fspath":"/u/users/user/file.txt"},"id":1}`                                                   |
| Read USS file w/ encoding  | `{"jsonrpc":"2.0","method":"readFile","params":{"fspath":"/u/users/user/file.txt","encoding":"ISO8859-1"},"id":1}`                            |
| Write USS file             | `{"jsonrpc":"2.0","method":"writeFile","params":{"fspath":"/u/users/user/file.txt","data":"aGVsbG8gd29ybGQ="},"id":1}`                        |
| Write USS file w/ encoding | `{"jsonrpc":"2.0","method":"writeFile","params":{"fspath":"/u/users/user/file.txt","data":"aGVsbG8gd29ybGQ=","encoding":"ISO8859-1"},"id":1}` |
| List USS files             | `{"jsonrpc":"2.0","method":"listFiles","params":{"fspath":"/u/users/self"},"id":1}`                                                           |
| Create USS file            | `{"jsonrpc":"2.0","method":"createFile","params":{"fspath":"/u/users/user/newfile.txt"},"id":1}`                                              |
| Create USS directory       | `{"jsonrpc":"2.0","method":"createFile","params":{"fspath":"/u/users/user/newdir","isDir":true},"id":1}`                                      |
| Delete USS file/directory  | `{"jsonrpc":"2.0","method":"deleteFile","params":{"fspath":"/u/users/user/file.txt"},"id":1}`                                                 |
| Move USS file              | `{"jsonrpc":"2.0","method":"moveFile","params":{"source":"/path/old.txt","target":"/path/new.txt"},"id":1}`                                   |
| Copy USS file              | `{"jsonrpc":"2.0","method":"copyUss","params":{"srcFsPath":"/path/source.txt","dstFsPath":"/path/dest.txt"},"id":1}`                          |
| Change file permissions    | `{"jsonrpc":"2.0","method":"chmodFile","params":{"fspath":"/path/file.txt","mode":"755"},"id":1}`                                             |
| Change file ownership      | `{"jsonrpc":"2.0","method":"chownFile","params":{"fspath":"/path/file.txt","owner":"newowner"},"id":1}`                                       |
| Change file tag            | `{"jsonrpc":"2.0","method":"chtagFile","params":{"fspath":"/path/file.txt","tag":"UTF-8"},"id":1}`                                            |

</details>

<details>
<summary><strong>USS Command Execution</strong></summary>

| Action              | Server Command                                                                      |
| ------------------- | ----------------------------------------------------------------------------------- |
| Execute USS command | `{"jsonrpc":"2.0","method":"unixCommand","params":{"commandText":"whoami"},"id":1}` |

</details>

<details>
<summary><strong>Job Operations</strong></summary>

| Action                 | Server Command                                                                                                                                              |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| List jobs              | `{"jsonrpc":"2.0","method":"listJobs","params":{},"id":1}`                                                                                                  |
| List jobs by owner     | `{"jsonrpc":"2.0","method":"listJobs","params":{"owner":"USER"},"id":1}`                                                                                    |
| List jobs by prefix    | `{"jsonrpc":"2.0","method":"listJobs","params":{"prefix":"JOB*"},"id":1}`                                                                                   |
| List spool files       | `{"jsonrpc":"2.0","method":"listSpools","params":{"jobId":"JOB01234"},"id":1}`                                                                              |
| Read spool file        | `{"jsonrpc":"2.0","method":"readSpool","params":{"jobId":"JOB01234","spoolId":2},"id":1}`                                                                   |
| Get job status         | `{"jsonrpc":"2.0","method":"getJobStatus","params":{"jobId":"JOB01234"},"id":1}`                                                                            |
| Get job JCL            | `{"jsonrpc":"2.0","method":"getJcl","params":{"jobId":"JOB01234"},"id":1}`                                                                                  |
| Submit JCL             | `{"jsonrpc":"2.0","method":"submitJcl","params":{"jcl":"Ly9JRUZCUjE0IEpPQiAoSVpVQUNDVCksVEVTVCxSRUdJT049ME0KLy9SVU4gICAgIEVYRUMgUEdNPUlFRkJSMTQ="},"id":1}` |
| Submit data set as job | `{"jsonrpc":"2.0","method":"submitJob","params":{"dsname":"USER.JCL(MEMBER)"},"id":1}`                                                                      |
| Submit USS file as job | `{"jsonrpc":"2.0","method":"submitUss","params":{"fspath":"/u/users/user/job.jcl"},"id":1}`                                                                 |
| Cancel job             | `{"jsonrpc":"2.0","method":"cancelJob","params":{"jobId":"JOB01234"},"id":1}`                                                                               |
| Delete job             | `{"jsonrpc":"2.0","method":"deleteJob","params":{"jobId":"JOB01234"},"id":1}`                                                                               |
| Hold job               | `{"jsonrpc":"2.0","method":"holdJob","params":{"jobId":"JOB01234"},"id":1}`                                                                                 |
| Release job            | `{"jsonrpc":"2.0","method":"releaseJob","params":{"jobId":"JOB01234"},"id":1}`                                                                              |

</details>

**Notes:**

- All commands use JSON-RPC 2.0 format with `zo server`
- The `id` field should be unique for each request to match responses
- Data content is Base64 encoded in the `data` field
- Use `zo --help` for CLI command reference and examples
