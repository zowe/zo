# JSON Output (`--json`)

Every `zo` command accepts `--json`. With it, the command prints one line of JSON to stdout
instead of human-readable text, so a script can parse a result without starting `zo server`
and speaking JSON-RPC.

```console
$ zo ds list IBMUSER --json
{"data":{"items":[{"name":"IBMUSER.CNTL"}],"returnedRows":1},"exitCode":0,"stderr":"","success":true}

$ zo ds list NO.SUCH.THING --json
{"data":{},"exitCode":1,"stderr":"Warning: no matching results found for: 'NO.SUCH.THING.**'\n","success":false}
```

Keys are emitted in sorted order, since the underlying object is an ordered map. Do not depend
on key order; parse it as JSON.

## The envelope

| Field | Type | Meaning |
| --- | --- | --- |
| `success` | boolean | `exitCode == 0` |
| `exitCode` | number | The command's return code, also the process exit status |
| `data` | object | The command's result (see below) |
| `stderr` | string | Whatever the command wrote to stderr, verbatim |
| `encoding` | string | Present and set to `base64` only when `data.data` had to be encoded |
| `truncated` | boolean | Present and `true` only when the stdout payload hit the 8 MB cap |

`success` is derived from the exit code here. The JSON-RPC server instead derives its `success`
from "stderr was empty" (`native/c/server/rpc_server.cpp`). The exit code is the better signal
for a CLI, because commands that emit warnings write to stderr and still exit 0 under
`--no-warn`.

## Where `data` comes from

Command handlers already build a structured result and hand it to the JSON-RPC layer via
`context.set_object()`. `--json` serializes that same object, so **for any given command,
`data` is what that command's RPC method returns in `result`.**

What happens to the text a handler writes to stdout depends on the command:

1. **The command's payload *is* stdout** — `ds view`, `uss view`, `job view-file`,
   `job view-file-by-id`, `job view-jcl`, `uss issue`, `tso issue`, `tool search`,
   `system view-syslog`. The captured text becomes `data.data`, alongside any fields the
   handler set:

   ```console
   $ zo ds view IBMUSER.CNTL(MEMBER) --json --return-etag
   {"data":{"data":"//STEP1 EXEC PGM=IEFBR14\n","etag":"8890283"},"exitCode":0,"stderr":"","success":true}
   ```

2. **The handler set no result object** — most mutations, such as `ds create` and `ds delete`.
   The message it printed becomes `data.data`, so no command yields an empty payload:

   ```console
   $ zo ds create-fb IBMUSER.TEMP --json
   {"data":{"data":"Data set created: 'IBMUSER.TEMP'\n"},"exitCode":0,"stderr":"","success":true}
   ```

3. **The handler reported fields of its own** — the `list` and `status` commands. The text was
   a rendering of those same fields, so it is dropped rather than duplicated. This is why
   `ds list --json` returns `items` and not also a copy of the aligned table.

Handlers that print only human-readable decoration already suppress it when their output is
being captured, via `Io::is_redirecting_output()` — so lines like `etag: ...` and `size: ...`
stay out of the JSON without any per-command work.

### Binary payloads

`data.data` is a plain JSON string when the payload is text. If it contains control bytes that
JSON cannot represent faithfully, it is base64-encoded instead and the envelope carries
`"encoding":"base64"`.

This is not optional prettiness: `zjson`'s string escaper maps any control byte it has no
escape for onto `U+FFFD`, deliberately, because IBM-1047 control bytes do not share Unicode
code points (see the comment on `escape_json_string` in `native/c/zjson.hpp`). Emitting such a
payload as a plain string would silently corrupt it.

Two common cases produce base64 rather than a plain string, so **always check `encoding`**
rather than assuming:

- Binary or `RECFM=U` content, and anything read with `--response-format-bytes`.
- Anything read with `--encoding`. Records are joined with the *target* encoding's newline
  byte (`get_newline_char` in `native/c/zds.cpp`), which is no longer the native `'\n'`, so the
  payload stops being representable as an EBCDIC-escaped string. The JSON-RPC layer
  base64-encodes these same commands unconditionally, so this matches `readDataset`,
  `readFile` and `readSpool`.

Without `--encoding`, native EBCDIC text comes back as a plain string.

```bash
zo ds view "$DSN" --json \
  | jq -r 'if .encoding == "base64" then (.data.data | @base64d) else .data.data end'
```

## What `--json` does not do

- **Help stays human-readable.** `zo ds --json` and `zo ds list --help --json` print help
  text, not an envelope — an envelope wrapped around a help screen serves nobody.
- **It does not wrap the root command's own output.** `zo --json ds list IBMUSER` and
  `zo ds list IBMUSER --json` are equivalent — the flag is accepted at the root and threaded
  down to the subcommand. But `zo --json` on its own just prints help, and `zo --json --it`
  just starts the REPL: that handler owns stdout for its lifetime and frames each result with
  `[rc]` plus an EOT byte that a capture buffer would swallow. Inside the REPL, `--json` works
  per line.
- **`zo server --json` starts the server normally.** It already writes JSON-RPC to stdout for
  the life of the process, so there is no result to wrap.
- **`zoa` does not support it at all.** `zoa` runs APF-authorized and its console commands
  stay privileged, so the authorization-drop hook never fires for them. Serializing an envelope
  would call the z/OS HWTJ services from an authorized job step, which needs a security review
  first, so `--json` is disabled for that binary outright.
- **Usage errors are still reported as JSON.** A bad option or a lexer error never reaches a
  handler, but the envelope is still emitted with `success:false`; the usage text itself goes to
  stderr.

## Notes for other flags

- `--pipe-path` works well with `--json`: the bytes still go to the FIFO and the envelope
  carries `etag` and `contentLen`.
- `--response-format-bytes` puts its hex rendering into `data.data`.
- `--response-format-csv` is redundant with `--json`: you get the structured `items`, and the
  CSV text is discarded under rule 3 above.

## Adding `--json` support to a new command

Nothing to do — the flag is injected into every command by the parser, next to `--help`. To get
a rich `data` object rather than a message string, call `context.set_object()` in the handler,
exactly as you would to expose the command over JSON-RPC. See
[add-new-command.md](./add-new-command.md).

If the command's payload is its stdout rather than a set of fields, also call
`mark_stdout_as_payload()` on it at registration time.
