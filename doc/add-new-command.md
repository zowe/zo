# Adding a New Command to Zowe Remote SSH

This guide walks you through creating a new command for the Zowe Remote SSH stack, from the low-level C++ implementation through the middleware layer to the SDK. We'll use a simple `ping` command as an example that takes an optional message and returns a pong response with a timestamp.

> **💡 Example Source Code**: For a complete working example, see the [`examples/add-new-command`](../examples/add-new-command) directory, which demonstrates adding commands across all layers of the stack (C++ backend, middleware, SDK, CLI, and VS Code extension).

## Table of Contents

1. [Adding a Command to the Native CLI (zo)](#1-adding-a-command-to-the-native-cli-zo)
2. [Testing Your CLI Command](#2-testing-your-cli-command)
3. [Adding the Command to the Server (zo server)](#3-adding-the-command-to-the-server-zo-server)
4. [Testing Your Server Command](#4-testing-your-server-command)

---

## 1. Adding a Command to the Native CLI (zo)

The `zo` CLI is the lowest level of the stack, providing direct access to mainframe capabilities. Commands are defined in the `native/c/commands/` directory.

### Step 1.1: Create Command Files

Create two files in `native/c/commands/`:

- `sample.hpp` - Header file with function declarations
- `sample.cpp` - Implementation file with command logic

**`native/c/commands/sample.hpp`:**

```cpp
#include "../parser.hpp"

namespace sample
{
using namespace plugin;
int handle_ping(InvocationContext &context);
void register_commands(parser::Command &root_command);
} // namespace sample
```

**`native/c/commands/sample.cpp`:**

```cpp
#include "sample.hpp"
#include <string>
#include <ctime>

using namespace ast;
using namespace parser;

namespace sample
{

// Command handler for ping command
int handle_ping(InvocationContext &context)
{
  // Parse the optional message argument
  std::string message = context.get<std::string>("message", "hello");

  // Get current timestamp
  time_t now = time(nullptr);
  char *dt = ctime(&now);

  // Remove newline from ctime output
  std::string timestamp(dt);
  timestamp.erase(timestamp.find_last_not_of("\n\r") + 1);

  // Print to stdout (for CLI output)
  context.output_stream() << "PONG: " << message << " at " << timestamp << std::endl;

  // Set return object (for programmatic access)
  const auto result = obj();
  result->set("data", str("PONG: " + message));
  result->set("timestamp", str(timestamp));
  context.set_object(result);

  return RTNCD_SUCCESS;
}

// Register the command with the parser
void register_commands(parser::Command &root_command)
{
  // Create the ping command
  auto ping_cmd = command_ptr(new Command("ping", "send a ping message"));

  // Add optional message argument
  ping_cmd->add_option(
      "message",
      "message to include in ping",
      ArgType_Single,
      false  // not required
  );

  // Set the handler function
  ping_cmd->set_handler(handle_ping);

  // Add examples
  ping_cmd->add_example("Simple ping", "zo ping");
  ping_cmd->add_example("Ping with message", "zo ping --message \"Hello, world!\"");

  // Register with the root command
  root_command.add_command(ping_cmd);
}

} // namespace sample
```

### Step 1.2: Register Your Command

Edit `native/c/zo.cpp` to include and register your new command:

```cpp
// Add include at the top with other command includes
#include "commands/sample.hpp"

// In the main() function, add registration call with other commands
int main(int argc, char *argv[])
{
  // ... existing code ...

  // Register commands
  ds::register_commands(arg_parser.get_root_command());
  job::register_commands(arg_parser.get_root_command());
  uss::register_commands(arg_parser.get_root_command());
  sample::register_commands(arg_parser.get_root_command());  // Add this line

  // ... rest of main ...
}
```

### Step 1.3: Update the Makefile

Edit `native/c/makefile` to include your new source file in the build:

```makefile
# Find the line with command object files and add sample.o
COMMAND_OBJS = commands/ds.o commands/job.o commands/uss.o \
               commands/tso.o commands/console.o commands/tool.o \
               commands/core.o commands/sample.o
```

---

## 2. Testing Your CLI Command

### Step 2.1: Build zo

Upload your new source code:

```bash
npm run z:upload
```

Build the zo C++ binary:

```bash
npm run z:build
```

**Note:** This command is equivalent to running `cd native/c && make` on z/OS.

### Step 2.2: Test the Command

Once compiled, you can test the command directly on z/OS:

```bash
# Test simple ping
./zo ping

# Expected output:
# PONG: hello at Mon Oct  6 14:23:45 2025

# Test with custom message
./zo ping --message "Hello, world!"

# Expected output:
# PONG: Hello, world! at Mon Oct  6 14:23:45 2025

# The object passed to context.set_object() is also what --json prints
./zo ping --message "Hello, world!" --json

# Expected output:
# {"data":{"data":"PONG: Hello, world!","timestamp":"Mon Oct  6 14:23:45 2025"},"exitCode":0,"stderr":"","success":true}
```

> [!NOTE]
> `--json` is injected into every command by the parser, so there is nothing to register for
> it. See [json-output.md](./json-output.md) for the envelope contract, and call
> `mark_stdout_as_payload()` on the command if its payload is its stdout rather than a set of
> fields.

---

## 3. Adding the Command to the Server (zo server)

The server (`zo server`) is an embedded subcommand that provides remote access to `zo` commands via JSON-RPC over SSH. To expose your command through the server, you need to:

1. Plan your request and response structure
2. Register the command with the C++ dispatcher using `CommandBuilder`
3. Define TypeScript types for the SDK

### Step 3.1: Plan Your JSON-RPC Types

Before implementing the middleware integration, design the request and response structure for your command. This defines how clients will interact with your command over JSON-RPC. Later in [Step 4.1](#step-41-define-sdk-types), we'll formalize these as TypeScript types.

For our `zo ping` command, we'll create a JSON-RPC method called `ping` with:

**Request structure:**

```json
{
  "command": "ping",
  "message": "Hello, world!"
}
```

**Response structure:**

```json
{
  "data": "PONG: Hello, world!",
  "timestamp": "Mon Oct  6 14:23:45 2025"
}
```

Key considerations when planning your API:

- **Method name**: Use camelCase for the RPC method name (in this case `ping` works for both)
- **CLI mapping**: The `message` parameter will map to the CLI's optional `--message` flag
- **Response shape**: Include all relevant data from the CLI's return object
- **Consistency**: Follow patterns from existing commands (e.g., `listDatasets`, `submitJob`)

The `CommandBuilder` will help us map these RPC parameters to the CLI command arguments that `zo ping` expects.

### Step 3.2: Define RPC Types and Generate Schemas

Define TypeScript interfaces for your command's request and response. Create a new file in `packages/sdk/src/doc/rpc/`:

**`packages/sdk/src/doc/rpc/sample.ts`:**

```typescript
import type * as common from "./common";

export interface PingRequest extends common.CommandRequest<"ping"> {
  message?: string;
}

export interface PingResponse extends common.CommandResponse {
  data: string;
  timestamp: string;
}
```

Export your new types from `packages/sdk/src/doc/rpc/index.ts`:

```typescript
export * as sample from "./sample";
```

Then run the type generation script to convert your TypeScript types to C++ schemas:

```bash
npm run build:types
```

This generates `native/c/server/schemas/requests.hpp` and `responses.hpp` with schema definitions for all RPC types.

### Step 3.3: Register the Command with the Server

Edit `native/c/server/rpc_commands.cpp` to register your command with the dispatcher:

```cpp
#include "rpc_commands.hpp"
#include "dispatcher.hpp"
#include "schemas/requests.hpp"
#include "schemas/responses.hpp"
#include "../c/commands/ds.hpp"
#include "../c/commands/job.hpp"
#include "../c/commands/uss.hpp"
#include "../c/commands/sample.hpp"  // Add this include

// ... existing registration functions ...

void register_sample_commands(CommandDispatcher &dispatcher)
{
  dispatcher.register_command("ping",
                              CommandBuilder(sample::handle_ping)
                                  .validate<PingRequest, PingResponse>()
                                  .set_default("message", "hello from zo server"));
}

// In the main registration function, call register_sample_commands:
void register_all_commands(CommandDispatcher &dispatcher)
{
  register_ds_commands(dispatcher);
  register_job_commands(dispatcher);
  register_uss_commands(dispatcher);
  register_cmd_commands(dispatcher);
  register_sample_commands(dispatcher);  // Add this line
}
```

The `CommandBuilder` provides a fluent API for mapping RPC parameters to command arguments:

- `.validate<RequestT, ResponseT>()` - Enable automatic type validation for requests and responses
- `.rename_arg(from, to)` - Rename an RPC parameter to match the command's expected argument
- `.set_default(name, value)` - Set a default value for an argument
- `.flatten_obj(name)` - Flatten a nested JSON object into top-level arguments
- `.write_stdin(name, base64)` - Write an RPC parameter to the command's stdin
- `.read_stdout(name, base64)` - Read the command's stdout into the RPC response
- `.handle_fifo(rpcId, argName, mode)` - Create a FIFO pipe for streaming data

**Note:** The middleware automatically converts camelCase parameter names (used in JSON-RPC requests to match JSON standards) to kebab-case argument names (used in CLI flags to match CLI standards). For example, `maxReturnedRows` in the RPC request becomes `--max-returned-rows` when passed to the CLI command.

---

## 4. Testing Your Server Command

### Step 4.1: Build the Server

Upload your new source code:

```bash
npm run z:upload
```

Build the native binaries (including the server):

```bash
npm run z:build
```

**Note:** This command is equivalent to running `cd native/c && make` on z/OS. The server is now built as part of the `zo` binary.

### Step 4.2: Add SDK Method

Edit `packages/sdk/src/RpcClientApi.ts` to add a method for your new command:

```typescript
import type {
  CommandRequest,
  CommandResponse,
  cmds,
  ds,
  jobs,
  uss,
  sample,
} from "./doc/rpc";

export abstract class RpcClientApi {
  // ... existing apis (cmds, ds, jobs, uss) ...

  public sample = {
    ping: this.rpc<sample.PingRequest, sample.PingResponse>("ping"),
  };
}
```

### Step 4.3: Create a Test Script

Create a test script to verify your command works end-to-end:

**`test-ping.ts`:**

```typescript
import { ZSshClient } from "@zowe/zowex-for-zowe-sdk/src/ZSshClient";
import { ProfileInfo } from "@zowe/imperative";

async function main() {
  // Load Zowe configuration
  const profileInfo = new ProfileInfo("zowe");
  await profileInfo.readProfilesFromDisk();

  // Get SSH profile
  const sshProfile = profileInfo.getDefaultProfile("ssh");
  if (!sshProfile) {
    throw new Error("No default SSH profile found");
  }

  // Create SSH client
  const client = new ZSshClient(sshProfile.profile);

  try {
    // Connect to the server
    await client.connect();
    console.log("Connected to server");

    // Test 1: Ping with default message
    console.log("\nTest 1: Default Message");
    const defaultResponse = await client.sample.ping({});
    console.log("  Data:", defaultResponse.data);
    console.log("  Timestamp:", defaultResponse.timestamp);

    // Test 2: Ping with custom message
    console.log("\nTest 2: Custom Message");
    const customResponse = await client.sample.ping({
      message: "Hello, world!",
    });
    console.log("  Data:", customResponse.data);
    console.log("  Timestamp:", customResponse.timestamp);

    // Test 3: Another custom message
    console.log("\nTest 3: Another Custom Message");
    const anotherResponse = await client.sample.ping({
      message: "Zowe Remote SSH rocks!",
    });
    console.log("  Data:", anotherResponse.data);
    console.log("  Timestamp:", anotherResponse.timestamp);

    console.log("\n✅ All tests passed!");
  } catch (error) {
    console.error("❌ Test failed:", error);
    process.exit(1);
  } finally {
    // Disconnect
    await client.disconnect();
    console.log("Disconnected from server");
  }
}

main().catch(console.error);
```

### Step 4.4: Run the Test

Make sure you have a Zowe SSH profile configured, then run the test:

```bash
# Build the SDK first
cd packages/sdk
npm run build

# Run the test script
npx tsx test-ping.ts
```

Expected output:

```
Connected to server

Test 1: Default Message
  Data: PONG: hello from zo server
  Timestamp: Mon Oct  6 14:23:45 2025

Test 2: Custom Message
  Data: PONG: Hello, world!
  Timestamp: Mon Oct  6 14:23:46 2025

Test 3: Another Custom Message
  Data: PONG: Zowe Remote SSH rocks!
  Timestamp: Mon Oct  6 14:23:47 2025

✅ All tests passed!
Disconnected from server
```

---

## Summary

You've successfully added a new command to the Zowe Remote SSH stack! Here's what you did:

1. **Created a low-level C++ command** in `native/c/commands/` that can be invoked directly via `zo`
2. **Tested it locally** on z/OS using the zo CLI
3. **Defined RPC types** in `packages/sdk/src/doc/rpc/` as TypeScript interfaces
4. **Generated C++ schemas** using `npm run build:types`
5. **Registered it with the server** using the `CommandBuilder` API in `native/c/server/rpc_commands.cpp`
6. **Added SDK methods** in TypeScript for easy client-side access
7. **Tested end-to-end** with a sample script

## Key Patterns

- **Separation of concerns**: Library logic is separate from command handling
- **Return objects**: Always set a return object using `context.set_object()` for programmatic access. It feeds both the JSON-RPC `result` and the CLI's `--json` payload, so a command that sets one is machine-readable from either entry point. See [json-output.md](./json-output.md).
- **Tabular output**: A command that prints a row per result should build it with `commands::format::ResultTable` (`native/c/commands/result_table.hpp`) rather than writing its own `std::setw` chain. Declare each column with a name via `add_column("name", width)`, then feed each row through `table.row()`; the table renders the padded listing or the `--response-format-csv` record from that one declaration, and collects the `ast::Node` you hand `emit()` into the structured result. The column name is shown as a heading, in either text form, when the command also registers `RESPONSE_FORMAT_HEADER` (`--response-format-header`/`--rfh`) and the caller passes it. `handle_job_list` in `native/c/commands/job.cpp` is the shortest example.
- **Error handling**: Use proper return codes (`RTNCD_SUCCESS`, `RTNCD_FAILURE`) and error messages
- **CommandBuilder**: Use the fluent API to map RPC parameters to command arguments
- **Type safety**: Define TypeScript types to ensure consistency between middleware and SDK

## Reference Implementation

For a complete working example that includes all layers (C++ native, SDK, CLI, and VS Code extension), check out the [`examples/add-new-command`](../examples/add-new-command) directory. This includes:

- **C++ command** (`native/c/`) - Example command implementation for `zo`
- **C++ server** (`native/c/server/`) - Command registration with the dispatcher
- **SDK** (`packages/sdk/`) - TypeScript types and client methods
- **CLI** (`packages/cli/`) - CLI command definition and handler

## Next Steps

- Add the command to the CLI package (`packages/cli/`)
- Add the command to the VS Code extension
- Write unit tests for your command
- Update documentation and examples
