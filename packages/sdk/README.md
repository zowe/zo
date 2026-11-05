# Zowe Remote SSH SDK

The Zowe Remote SSH SDK is a TypeScript library that enables developers to interact with mainframe resources over SSH, directly from their custom Node.js application.

It communicates with the `zo server` daemon on z/OS via JSON-RPC over SSH, offering fast execution with minimal server-side dependencies.

## Installation

Install the package from NPM in your project:

```bash
npm install @zowe/zowex-for-zowe-sdk
```

Or install from a tarball available on [GitHub releases](https://github.com/zowe/zowex/releases):

```bash
npm install zowe-zowex-for-zowe-sdk-*.tgz
```

## Quick Start

The following script loads an SSH profile from your Zowe team configuration (`zowe.config.json`), connects to the z/OS host, and queries data sets, USS files, and jobs.

> [!NOTE]
> Requires the `zo` binary deployed on the host (defaults to `~/.zowe-server/zo`, or specified via `serverPath`).

```typescript
import { ProfileInfo } from "@zowe/imperative";
import { SshSession, ZSshClient } from "@zowe/zowex-for-zowe-sdk";

async function loadSshProfile(profName?: string): Promise<IProfArgAttrs[]> {
  const profInfo = new ProfileInfo("zowe");
  await profInfo.readProfilesFromDisk();
  const sshProfAttrs = profName
    ? profInfo.getAllProfiles("ssh").find((p) => p.profName === profName)
    : profInfo.getDefaultProfile("ssh");
  const sshMergedArgs = profInfo.mergeArgsForProfile(sshProfAttrs, { getSecureVals: true });
  return sshMergedArgs.knownArgs;
}

async function runForEachUser(action: string, users: string[], cb: (user: string) => Promise<any>) {
  for (const user of users) {
    console.time(`${action}-${user}`);
    console.dir(await cb(user));
    console.timeEnd(`${action}-${user}`);
  }
}

(async () => {
  const sshProfArgs = await loadSshProfile();
  const session = new SshSession(ProfileInfo.initSessCfg(sshProfArgs));
  const serverPathArg = sshProfArgs.find((arg) => arg.argName === "serverPath");
  using client = await ZSshClient.create(session, { serverPath: serverPathArg?.argValue as string });
  console.log("ready:", await client.core.getInfo());
  const testUsers = process.argv.length > 2 ? process.argv.slice(2) : [session.ISshSession.user];
  await runForEachUser("listDatasets", testUsers, async (user: string) => {
    const dsResponse = await client.ds.listDatasets({ pattern: `${user}.*` });
    return dsResponse.items.map((item) => item.name);
  });
  await runForEachUser("listFiles", testUsers, async (user: string) => {
    const ussResponse = await client.uss.listFiles({ fspath: `/u/users/${user}` });
    return ussResponse.items.map((item) => item.name);
  });
  await runForEachUser("listJobs", testUsers, async (user: string) => {
    const jobsResponse = await client.jobs.listJobs({ owner: user });
    return jobsResponse.items.map((item) => item.id);
  });
})().catch((err) => {
  console.error(err.stack);
  process.exit(1);
});
```

Run the example with `tsx` or `ts-node` and optionally pass mainframe user IDs on the command line:

```bash
npx tsx sample.ts IBMUSER
```

## API Overview

`ZSshClient` extends `RpcClientApi`, grouping commands into dedicated namespaces such as `ds`, `jobs`, and `uss`. For detailed method interfaces and type definitions, see the [doc/rpc directory](./src/doc/rpc/).

Pass `ClientOptions` like `serverPath` as the second argument to `ZSshClient.create(session, options)`. For available options, see the [doc/client.ts file](./src/doc/client.ts).

## SSH connection reuse

`SshClientCache.inst` shares authenticated connections between profile validation, SSH utilities,
SFTP deployment, and `ZSshClient.create`. Existing session-based method calls continue to work;
equivalent sessions resolve to the same connection. Transport identity uses the user, host, and port;
server-client keys additionally include the server path, e.g. `ssh://user@host:22/opt/zo`.
Credentials are excluded from cache keys. Profile validation publishes a connection only after
both authentication and a validation command succeed.

Idle connections close after ten minutes. An active server client or workflow lease prevents expiry
from interrupting its operations. `SessionContext` is deprecated; SDK methods accept plain `SshSession`
objects and look up the cached transport automatically. For a longer workflow, use
`using lease = await SshClientCache.inst.acquire(session)` to hold its connection until the scope ends.
Disposing a client closes its server channel and releases the transport for reuse. Use `skipCache: true` to create a dedicated connection that closes on disposal:

```typescript
using client = await ZSshClient.create(session, { skipCache: true });
```

For explicit invalidation, import `SshClientCache` and use the client's key:

```typescript
SshClientCache.inst.invalidate(client.cacheKey);
```

Invalidation immediately closes the shared transport and affects every client using that connection.
`cacheKey` is undefined for dedicated or native SSH clients. Experimental native SSH cannot adopt the
`node-ssh` connection used for validation and SFTP deployment, so server startup authenticates separately.
Disable experimental native SSH when the entire workflow must reuse one MFA authentication.
`SshClientCache.inst.dispose()` closes all cached clients and transports, including idle connections.

Extenders can subclass `SshClientCache` to override settings, deployment confirmation, progress, and
error presentation while inheriting server caching and deployment sequencing. Install that subclass
as the SDK singleton with the protected static `setInstance` method, as the VS Code adapter does.

## Building from Source

1. From the root of this repository, run `npm install` to install all the dependencies
2. Make changes in the `packages/sdk` folder and run `npm run build` to build the SDK

The SDK is compiled and saved in the `packages/sdk/lib` folder.
