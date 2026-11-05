# Zowe Remote SSH plug-in for Zowe CLI

The Zowe Remote SSH plug-in for Zowe CLI enables developers to work with mainframe resources over SSH from their command line.

## Features

- **Dataset Operations**: Create, read, write, and delete datasets
- **USS File Access**: Create, read, write, and delete files and directories in Unix System Services
- **Job Management**: Submit, view, cancel, hold, and release jobs on z/OS

## Minimum Requirements

- Zowe CLI v8.0.0 (or newer)

## Installation

Access the latest version of the CLI plug-in from the GitHub Releases page.

Install the CLI plug-in from the tarball:

```
zowe plugins install zowex-for-zowe-cli-*.tgz
```

Once complete, the Zowe CLI plug-in is installed and ready to use.

## Usage

Run the `zowe plugins show-first-steps zowex-for-zowe-cli` command to see the first steps for using the plug-in.

## SSH connection reuse in daemon mode

When a command runs through the Zowe CLI daemon, the plug-in reuses the SDK's authenticated SSH
connection for later commands to the same user, host, and port. Each command still starts and stops
its own `zo server` channel. Idle connections expire after ten minutes; connection failures and
expired passwords invalidate them.

Daemon execution is detected through `ImperativeConfig.instance.daemonContext?.stream`, the same
public context used by Imperative's stream handling. Outside daemon execution, the cache is scoped
to one command and closed when it finishes, including on errors. This also covers setup, install,
and uninstall, while allowing authentication fallback to reuse a connection within one command.

Reuse applies only within the same daemon process. Restarting the daemon, expiry, or invalidation
requires fresh authentication.

## Building from source

1. From the root of this repository, run `npm install` to install all the dependencies
2. Change to the `packages/cli` folder and run `npm run build` to build the plug-in
3. `npm run package` packages the plug-in

The plug-in is created and saved in the `dist` folder.
Install the plug-in by running the following Zowe CLI command:

```
zowe plugins install dist/zowex-for-zowe-cli-*.tgz
```

Zowe Remote SSH plug-in commands are accessible through the `zowe zssh` command group.

```
zowe zssh --help
```
