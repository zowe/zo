---
name: build-zo-command
description: Guides users through creating new zo commands across the Zowe Remote SSH stack (native C++, server, SDK). Use when the user wants to add a new command, implement a zo command, or add functionality to the native backend.
---

# Building a New ZO Command

This skill guides the creation of a new command for the Zowe Remote SSH stack, covering the C++ implementation, middleware layer, and SDK.

## 1. Native CLI Implementation (zo)

Commands are loosely organized by command group in `native/c/commands/`.

### Locate or Create Command Files
- **For an existing command group (e.g., `ds`, `job`, `uss`):** Update the existing `native/c/commands/<group>.hpp` and `<group>.cpp`.
- **For a new command group:** Create new files `native/c/commands/<group>.hpp` and `<group>.cpp`.

For the command implementation:
- Use `int handle_<name>(InvocationContext &context)` for logic.
- Use `context.get<T>("argName", defaultValue)` to parse arguments.
- **Crucial:** Always set a return object using `context.set_object(result)` for programmatic access.
- Return `RTNCD_SUCCESS`, `RTNCD_WARNING`, or `RTNCD_FAILURE`.
- Register the command inside `void register_commands(parser::Command &root_command)` using `Command` and `add_option()`.

### Register Command Group
*(Only required if creating a new command group)*
Update `native/c/zo.cpp`:
1. `#include "commands/<group>.hpp"`
2. Call `<group>::register_commands(arg_parser.get_root_command());` in `main()`.

### Update Makefile
*(Only required if creating a new command group)*
Add `commands/<group>.o` to `COMMAND_OBJS` in `native/c/makefile`.

## 2. Server Implementation (zo server) [Optional]

*(Optional: Not all commands need to be exposed through the server)*
Expose the command via JSON-RPC over SSH.

### Define RPC Types
Update or create `packages/sdk/src/doc/rpc/<group>.ts`:
- Define `<Name>Request` extending `common.CommandRequest`.
- Define `<Name>Response` extending `common.CommandResponse`.
- If new, export it in `packages/sdk/src/doc/rpc/index.ts`.
- Run `npm run build:types` to generate C++ schemas.

### Register with Server
Update `native/c/server/rpc_commands.cpp`:
1. Ensure `#include "../c/commands/<group>.hpp"` is present.
2. In the `void register_<group>_commands(CommandDispatcher &dispatcher)` function (create it if missing), register your command:
   ```cpp
   dispatcher.register_command("methodName", 
       CommandBuilder(<group>::handle_<name>)
           .validate<<Name>Request, <Name>Response>()
   );
   ```
   *Note: Middleware automatically converts camelCase RPC parameters to kebab-case CLI arguments.*
3. If this is a new group, call your registration function in `register_all_commands()`.

## 3. SDK Integration [Optional]

Update `packages/sdk/src/RpcClientApi.ts`:
- Ensure your group's types are imported from `./doc/rpc`.
- Add a method to the corresponding group property in `RpcClientApi` (create the property if it's a new group):
  ```typescript
  public <group> = {
    // ... existing methods
    methodName: this.rpc<<group>.<Name>Request, <group>.<Name>Response>("methodName"),
  };
  ```

## 4. Testing Workflow

1. Upload and build native code: `npm run z:upload && npm run z:build` (or `npm run z:rebuild`)
2. Build SDK: `cd packages/sdk && npm run build`
3. Test native locally on z/OS (`./zo <command>`) or via a TypeScript SDK script.

## Reference Implementation

For a complete working example, check out the `examples/add-new-command` directory.