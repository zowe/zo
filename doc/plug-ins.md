# Creating a `zo` Plug-in

The native plug-in infrastructure lets extenders add new commands and behaviors to `zo`. The entry points exposed in
`native/c/extend/plugin.hpp` describe the full contract between a plug-in and `zo`.

## Enabling plug-in loading: `ZO_PLUGINS_DIR`

`ZO_PLUGINS_DIR` is a **security control**, not a path override. There is no implicit fallback directory: `zo`
never loads plug-ins unless this environment variable is explicitly set for the process. Setting it opts that
`zo` invocation into `dlopen`-ing and executing every shared library it finds in the named directory, with the
same privileges as the `zo` process itself.

- If `ZO_PLUGINS_DIR` is unset or empty, `zo` does not touch a plugins directory at all: no `opendir`, no
  `dlopen`, no plug-in code runs.
- If it is set, `zo` treats every regular file in that directory as native code to execute. On a shared install,
  this directory **must** be writable only by trusted installers/administrators. Anyone who can write a file into it
  can run arbitrary code as every user who subsequently invokes `zo` with that variable set.
- Environment variables are set per session, so overriding `ZO_PLUGINS_DIR` for a session grants that session's
  code the privileges of whoever authored the plug-ins in the directory it names. Only set this variable for a
  session when you trust that directory's contents.

## Directory and file requirements (provenance checks)

Even once `ZO_PLUGINS_DIR` is set, `zo` will only load plug-ins from a directory whose ownership and
permissions make it a safe place to load native code from. These checks run *before* any shared library is opened,
so a rejected directory or file never gets to execute constructor code.

**The plugins directory must:**

- be a directory (not a file, symlink target that isn't a directory, etc.);
- **not** be group- or world-writable. If any identity other than the owner can add files to it, the whole
  directory is rejected and no plug-ins load;
- be owned by **root or by the user running `zo`**. A directory owned by some other identity is rejected.

**Each plug-in file must:**

- be a regular file (symlinks, FIFOs, devices, and subdirectories are skipped);
- be owned by the **same owner as the directory**, or by root, or by the user running `zo`. A file dropped into
  an otherwise-trusted directory by a different, unprivileged identity is rejected even though the directory passed;
- **not** be group- or world-writable, since a file others can overwrite could be swapped out after it was placed.

Any rejected directory or file is logged (at `ZLOG_ERROR`) with the specific reason, and skipped; a single bad entry
does not stop the loader from considering the rest of the directory. If the directory itself is rejected, nothing in
it is loaded.

> These are fixed rules, not a configurable trust list: root and the invoking user are trusted, and no one else is.
> Loading code owned by root or by yourself is not a cross-user privilege escalation; loading code owned by another
> unprivileged identity is. Extending trust to a dedicated non-root installer identity via the platform ESM
> (RACF/ACF2/TSS) is tracked separately in [zowe/zowex#1072](https://github.com/zowe/zowex/issues/1072), not
> handled by a `zo`-managed config file.

## Command name reservations (built-in verb protection)

Even a plug-in that loads successfully cannot override a built-in command. When `zo` wires plug-in commands into
the command tree, it refuses a top-level command if its name or any of its aliases collides with a built-in verb
(`console`, `ds`, `job`, `server`, `sys`, `tool`, `tso`, `uss`, and their aliases) or with a top-level command
another plug-in has already registered.

- A refused command is logged at `ZLOG_ERROR` explaining which token collided; the built-in (or first-registered)
  command stays in place and continues to work.
- Only the colliding command is dropped. The plug-in's other, non-colliding commands still register, and `zo`
  continues to run.
- The collision check looks at the command's *final* name and alias set, so adding an alias after attaching a command
  does not sidestep it. Pick command names and aliases that do not overlap the built-in verbs above.

## Runtime entry point

Each shared library is expected to export a single function that `zo` discovers via dynamic loading:

```c
extern "C" void register_plugin(PluginManager &manager);
```

Inside `register_plugin`, call `manager.register_plugin_metadata(...)` once to describe your plug-in and then `manager.register_command_provider(...)` for every command set the plug-in contributes. The manager automatically records the underlying shared library filename for debugging, so you only supply a display name and version:

```cpp
#include "native/c/extend/plugin.hpp"

class MyCommandProviderFactory : public CommandProvider {
public:
  std::unique_ptr<CommandProviderImpl> create() override {
    return std::make_unique<MyCommandProviderImpl>();
  }
};

ZO_PLUGIN_DECLARE_ABI()

extern "C" void register_plugin(PluginManager &manager)
{
  manager.register_plugin_metadata("Sample Plug-in", "1.0.0");
  manager.register_command_provider(std::make_unique<MyCommandProviderFactory>());
}
```

## Binary compatibility

`ZO_PLUGIN_DECLARE_ABI()` is required, and must be expanded exactly once, in the same translation unit as
`register_plugin`. It exports the version of the plug-in ABI your library was compiled against.

Types like `InvocationContext`, `ArgumentMap` and `ast::ObjMap` cross the `dlopen` boundary, and accessors such as
`context.get<T>(...)` are inlined *into your plug-in*. A plug-in built against a different revision of `plugin.hpp`
therefore reads the wrong member offsets. `zo` compares the reported version against its own
`ZO_PLUGIN_ABI_VERSION` before calling `register_plugin`, and rejects a mismatch with a `ZLOG_ERROR` message rather
than loading the plug-in and corrupting memory. A library that does not export the symbol at all is treated as version
0 and rejected.

Rebuild your plug-in against the current `native/c/extend/plugin.hpp` whenever the constant is bumped.

Plug-ins are also subject to the same runtime floor as `zo` itself: build them with the Open XL C/C++ level the
project targets, or they will fail to load on older Language Environment maintenance levels even when `zo` loads
fine. See [`native/c/compat/README.md`](../native/c/compat/README.md).

## Implementing a command provider

`CommandProvider` is a `Factory<CommandProviderImpl>`. When `zo` is ready to populate the command tree it calls
`create()`, expects a `CommandProviderImpl`, and immediately invokes `register_commands(...)` on the instance.

```cpp
class MyCommandProviderImpl : public CommandProviderImpl {
public:
  void register_commands(CommandRegistrationContext &context) override;
};
```

The `CommandRegistrationContext` supplied to `register_commands` is your builder for commands, arguments, and handlers. It offers the following capabilities:

- `create_command(name, help)` – create a child command and hold on to the returned handle.
- `get_root_command()` – fetch a handle to the root so you can attach top-level commands.
- `add_alias(command, alias)` – register alternative names.
- `add_keyword_arg(...)` and `add_positional_arg(...)` – add options/positional parameters with the specified `ArgumentType` (`Flag`, `Single`, `Multiple`, or `Positional`), whether they are required, and an optional default.
- `set_handler(command, handler)` – wire in an `int handler(const parser::ParseResult &)` that executes when the command runs.
- `add_subcommand(parent, child)` – stitch commands into the hierarchy. Pass `get_root_command()` as the parent to create top-level commands.

### Providing default argument values

`CommandDefaultValue` supplies constructors for every supported kind (bool, integer, double, and string). Construct one on the stack and pass its pointer when you want a default:

```cpp
CommandDefaultValue default_timeout(30LL);
context.add_keyword_arg(cmd,
                      "timeout",
                      nullptr,
                      0,
                      "Number of seconds to wait",
                      CommandRegistrationContext::ArgumentType_Single,
                      /* required */ 0,
                      &default_timeout);
```

If you do not supply a default or pass `nullptr`, the argument inherits the parser's standard empty value.

### Complete example

```cpp
int hello_cmd_handler(const InvocationContext& context) {
  const auto name = context.get<std::string>("name", "");
  if (!name.empty()) {
    context.println("hello, " + name + "!");
  } else {
    context.println("hello");
  }

  return 0;
}

void MyCommandProviderImpl::register_commands(CommandRegistrationContext &context)
{
  auto root = context.get_root_command();
  auto hello = context.create_command("hello", "Print a greeting");

  CommandDefaultValue default_name("World");
  const char *aliases[] = {"-n", "--name"};
  context.add_keyword_arg(hello,
                        "name",
                        aliases,
                        2,
                        "Person to greet",
                        CommandRegistrationContext::ArgumentType_Single,
                        /* required */ 0,
                        &defaultName);

  context.set_handler(hello, &hello_cmd_handler);
  context.add_subcommand(root, hello);
}
```
