# Server Design

The `zo server` subcommand acts as the middleware between the client and backend. It is written in modern C++ and leverages an input channel to support asynchronous request processing. The server is embedded within the `zo` binary and invoked via `zo server`.

## Architectural overview

The server mediates all requests and dispatches them to appropriate command handlers. These command handlers are defined in a map and are accessed by the command name. Those command handlers execute C++ methods in the backend layer (e.g. `handle_data_set_list`) to access data. The response data is composed and serialized as JSON before being returned to the caller through stdout.

## Configuration

The `--num-workers` argument (or `-w`) for `zo server` takes precedence over the `ZO_NUM_WORKERS` environment variable. If the argument is omitted, the server uses `ZO_NUM_WORKERS` when set, otherwise it defaults to 10 workers. This lets system administrators set default server concurrency at the environment level while allowing client configurations to override it.

## Request and response processing

The server process is instantiated by the client through SSH (via `zo server`), which opens a communication channel over stdio. When a request is received from the client over stdin, the server attempts to parse the input as JSON. If the JSON response is valid, the server looks for the `command` property of the JSON object and attempts to identify a matching command handler. If a command handler is found for the given command, the handler is executed and given the JSON object for further processing.

The command handlers can expect a stronger request type than what is expected during initial command processing. Appropriate request and response types can be exposed for use with these handlers. In the event of a JSON deserialization error, the command handler stops execution and returns early, returning an error response with any additional context. Once the JSON is successfully deserialized into the desired type, command processing continues and the handler can perform any actions necessary to create, receive, update or delete data.

Once the command handler has the required data for a response, the handler "marshals" (serializes) the response type as JSON and prints it to stdout. The output is redirected to the SSH client, where it is interpreted as a response and processed according to the corresponding response type.

See the example JSON request below for listing data sets:

```json
{
  "command": "listDatasets",
  "pattern": "DS.PAT.*"
}
```

This request is deserialized by the server and a command handler is invoked to get the list of data sets matching the given pattern. Then the response is composed, serialized as JSON and returned to the caller, for example:

```json
{
  "items": [
    {
      "dsname": "DS.PAT.AB",
      "dsorg": "PO",
      "volser": "MIGRAT"
    },
    {
      "dsname": "DS.PAT.ABC",
      "dsorg": "PO-E",
      "volser": "WRKD01"
    },
    {
      "dsname": "DS.PAT.ABCD",
      "dsorg": "PS",
      "volser": "WRKD03"
    }
  ],
  "returnedRows": 3
}
```

## Handling encoding for resource contents

Modern text editors expect a standardized encoding format such as UTF-8. The server implements processing for reading/writing data sets, USS files and job spools (read-only) with a given encoding.

### Read

When an encoding is not provided, we use the system default codepage as the source encoding for resource contents (`IBM-1047`, in most cases). We convert the contents from the source encoding to UTF-8 so the contents can be rendered and edited within any modern text editor.

```json
{
  "command": "readFile",
  "path": "/u/users/you/file.txt",
  "encoding": "ISO8859-1"
}
```

Response:

```jsonc
{
  "etag": "1234-5",
  "data": "aGVsbG8=", // "hello" in ASCII
}
```

### Write

The contents of write requests are interpreted as UTF-8 data. We convert the UTF-8 bytes to the given encoding so the data can be read by z/OSMF and other mainframe utilities. If no encoding is provided, we convert the contents from `UTF-8` to the system default codepage.

```jsonc
{
  "command": "writeFile",
  "path": "/u/users/you/file.txt",
  "encoding": "IBM-939",
  "data": "pYO/pYTApYOrpYOhpYOvIQ==", // "Hello!" in Japanese, encoded as UTF-8
}
```

Response:

```json
{
  "success": true,
  "etag": "1234-5",
  "created": false
}
```

### Data transmission

To transmit codepage-encoded contents between the server and the backend, we pipe raw bytes to `stdin` for a write request and interpret raw bytes from `stdout` for a read request. For large files, a FIFO pipe is created per request which allows Base64-encoded data to be streamed directly between the client and the backend.
