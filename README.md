# Zowe Remote SSH

An open-source, native protocol for z/OS mainframe operations via SSH with minimal server-side configuration.

## Prerequisites

### Local

- Node.js & npm - required to build client packages
- VS Code extensions - recommended ones are listed in [extensions.json](./.vscode/extensions.json)

### z/OS (build system)

- [IBM Open XL C/C++ for z/OS](https://www.ibm.com/docs/en/open-xl-c-cpp-zos/latest) - `ibm-clang++` (C++ compilation).
  Use **Open XL C/C++ 2.1**, not 2.2: 2.2 requires z/OS 3.1 or later and references a newer C++ runtime than a z/OS 2.5
  system can provide. See [native/c/compat/README.md](./native/c/compat/README.md).
- [IBM C/C++ for z/OS](https://www.ibm.com/products/xl-cpp-compiler-zos) - `xlc` (required for Metal C compilation)
- [SWIG](https://github.com/t1m0thyj/swig-zos) (optional) - used to build experimental Python bindings

### z/OS (target system)

The server binary is built once and deployed to every target system, so the target's runtime level matters:

- **z/OS 2.5 or later.**
- **Current Language Environment maintenance.** `zo` links dynamically against the C++ runtime (libc++) that ships
  inside Language Environment. A system at a supported z/OS release can still be missing the required service, in which
  case the server fails to load with `CEE3561S ... was not found in DLL CRTEQCXE`. Required APARs:
  - z/OS 2.5 - the Language Environment C++ runtime service for Open XL C/C++ (the `PH45516` APAR family)
  - z/OS 3.1 - `PH53938`, `PH60056`, `PH62468`, `PH68179`, per the IBM Z Distribution for Zowe program directory
- An SSH login and a writable z/OS UNIX directory.
- **PKCS#12 certificate export:** System SSL with APAR OA64899 support and active ICSF. This support is available
  on z/OS 2.5 and 3.1 with the applicable PTFs. See [certificate export security](./native/README.md#pkcs12-certificate-export-security)
  for authorization and passphrase requirements.

See [Troubleshooting](./doc/troubleshooting.md) if the server fails to start with a `CEE`-prefixed message.

## Setup

1. Clone this repository and run `npm install` to install project dependencies.
2. Create a config file by copying `config.example.yaml` to `config.yaml`. Update the properties in the file for your environment.
3. For a quick start, run `npm run all` which performs the following tasks:
   1. `z:upload` - Upload source files<br/>
   2. `z:build` - Build native binaries on z/OS<br/>
   3. `z:artifacts` - Download build artifacts<br/>
   4. `build` - Build client packages locally

> [!TIP]
> If you don't have a z/OS build environment, you can download prebuilt binaries with `npm run download` (this requires the GitHub CLI). Then skip to Step 3.4 and run `npm run build` to build client packages.

## Usage

### Zowe CLI plug-in

To install the CLI plug-in from source, run `zowe plugins install ./packages/cli`. Then run `zowe zssh --help` to see available commands.

See the [CLI readme](./packages/cli/README.md) for more details about using the CLI plug-in.

### Native binaries

To run the `zo` CLI on z/OS, connect to USS shell (OMVS) and `cd` to `c/build-out` inside the deploy directory. Then run `zo`. MVS console commands live in the separate APF-authorized `zoa` binary (`zoa console issue "D T"`) — see [doc/zoa-security.md](./doc/zoa-security.md) for how to install and secure it.

Pass `--json` to any command to get its result as a single line of JSON instead of formatted text — handy for scripting without starting a server. See [doc/json-output.md](./doc/json-output.md).

See the [native readme](./native/README.md) for more details about building the native components.

## Development

### Quickstart

Run `npm run z:rebuild` to rebuild server code after editing files in the `native` folder. See [Troubleshooting](./doc/troubleshooting.md) to debug common build issues.

Run `npm run build` to rebuild client code after editing files in the `packages` folder.

To test server changes without having to download artifacts and re-deploy them each time, you can define `serverPath` property in your SSH profile in `zowe.config.json`. It should point to the `<deployDir>/c/build-out` folder that contains the `zo` binary (which includes the embedded server via `zo server`). See example below.

```json
  "profiles": {
    "ssh_dev": {
      "type": "ssh",
      "properties": {
        ...
        "serverPath": "~/zowex/c/build-out"
      }
    }
  }
```

To package client components with native binaries bundled, run `npm run z:artifacts && npm run package` which generates packages in the `dist` directory.

### Rebuilding the SDK

The SDK package contains both TypeScript client code and the native z/OS server binary (`bin/server.pax.Z`). How to rebuild depends on what changed:

- **TypeScript only**: `npm run build --workspace=packages/sdk`
- **Native only**: Run `npm run z:rebuild` and set `serverPath` in your SSH profile to the remote `<deployDir>/c/build-out` — no SDK repackage needed
- **Full SDK package** (`.tgz` with bundled binaries): `npm run z:rebuild && npm run z:artifacts && npm run build --workspace=packages/sdk && mkdir -p dist && npm run package --workspace=packages/sdk`

> [!TIP]
> See the list below for more useful scripts like `watch` for incremental build.

### NPM Scripts

We use a custom build tool for interacting with z/OS that defines the following NPM scripts:

| Command       | Description                                                                 |
| ------------- | --------------------------------------------------------------------------- |
| `z:artifacts` | Download native binaries and package them with clients                      |
| `z:build`     | Run `make` on z/OS to build native binaries                                 |
| `z:clean`     | Run `make clean` on z/OS to clean build targets                             |
| `z:delete`    | Delete all files from deploy directory                                      |
| `z:make`      | Execute the specified Make targets on z/OS                                  |
| `z:package`   | Create server PAX artifact in `dist` directory                              |
| `z:rebuild`   | Upload and Build combined in one command                                    |
| `z:test`      | Run automated tests for native components on z/OS                           |
| `z:upload`    | Upload source files from `native` directory to z/OS <sup>1</sup>            |
| `z:watch`     | Detect changes to files in `native` directory and upload/build <sup>2</sup> |

1. To deploy a single file or directory: `npm run z:upload -- c/zo.cpp`
2. To watch server and client code at the same time: `npm run watch:all`

> [!TIP]
> To deploy and build on a different LPAR, you can define additional profiles in `config.yaml` and use them by changing the `activeProfile` property. For example:
>
> ```yaml
> activeProfile: another
>
> profiles:
>   default:
>     # Profile properties...
>
>   another:
>     sshProfile: ssh2
>     deployDir: /tmp/zowex
> ```

## Architecture

Click on a component in the diagram below to learn more about it:

```mermaid
graph LR
  subgraph Client
  sdk[Node.js SDK Package]
  cli[Zowe CLI Plug-in]-->sdk
  vsce[Zowe Explorer for VS Code]-->sdk
  other[&quot;My Custom App&quot;]-->sdk
  end
  subgraph z/OS Server
  zo["C++ CLI Binary (zo)"]-->cpp
  ioserver["C++ I/O Server (zo server)"]-->cpp["C++ Libraries (Backend)"]
  python["Python REST API (experimental)"]-->cpp
  sdk<-->|SSH|ioserver
  subgraph Mainframe Resources
  ds[Data Sets]
  uss[USS Files]
  jobs[Jobs]
  end
  cpp-->ds
  cpp-->uss
  cpp-->jobs
  end
  click sdk "https://github.com/zowe/zowex/blob/main/doc/client/architecture.md#sdk-package"
  click cli "https://github.com/zowe/zowex/blob/main/doc/client/architecture.md#cli-plug-in"
  click ioserver "https://github.com/zowe/zowex/blob/main/doc/backend/server/architecture.md"
  click zo "https://github.com/zowe/zowex/blob/main/doc/backend/zo_architecture.md"
```
