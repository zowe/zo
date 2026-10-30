# SSH agents and user certificates

An SSH agent holds your signing identities so Zowe Remote SSH can authenticate without a `privateKey` or `password` in its SSH profile. This guide adapts the setup in [PR #1111](https://github.com/zowe/zo/pull/1111) to Zowe team configuration.

The examples use `mainframe.example.com`, SSH port `22`, and user `IBMUSER`. Replace these with your host, port, and SSH login name. Run workstation commands from the same terminal so Zowe inherits the agent environment. You need Zowe CLI and a version of the Remote SSH plug-in that supports `identityAgent`.

## Authenticate with a key in an agent

### 1. Start or select an agent

On macOS or Linux, check an existing agent with `ssh-add -l`. If no agent is available, start one:

```sh
eval "$(ssh-agent -s)"
```

This sets `SSH_AUTH_SOCK` to the agent socket. An agent reporting that it has no identities is running but needs a key loaded.

On Windows, use the OpenSSH Authentication Agent service or Pageant. The corresponding `identityAgent` values are shown below; load your key into the agent you select.

### 2. Load a key

Use an existing key, or create a dedicated one on your workstation:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/zos-agent-key -C "Zowe SSH agent"
ssh-add ~/.ssh/zos-agent-key
ssh-add -l
```

Choose a passphrase when prompted. `ssh-add` prompts to unlock the key, then the agent handles signing for subsequent connections. Use a key algorithm allowed by your site's SSH policy.

### 3. Authorize the public key on z/OS

Using an existing login method, append the contents of `~/.ssh/zos-agent-key.pub` to the target user's `~/.ssh/authorized_keys`. Where `ssh-copy-id` is available:

```sh
ssh-copy-id -i ~/.ssh/zos-agent-key.pub -p 22 IBMUSER@mainframe.example.com
```

For manual installation, preserve existing entries and keep each public key on one line. On z/OS, the target user should own the `.ssh` directory and `authorized_keys`, with permissions `700` and `600`, respectively. Only the public key belongs on the server.

### 4. Set `identityAgent` in team configuration

Add an SSH profile to `zowe.config.json`, or update the properties of an existing one. This is a minimal example; merge it into your team's existing profiles and defaults:

```json
{
  "profiles": {
    "zos_agent": {
      "type": "ssh",
      "properties": {
        "host": "mainframe.example.com",
        "port": 22,
        "user": "IBMUSER",
        "identityAgent": "SSH_AUTH_SOCK"
      }
    }
  },
  "defaults": {
    "ssh": "zos_agent"
  }
}
```

`"SSH_AUTH_SOCK"` is a special literal value: the SDK resolves the current environment variable each time it connects. Do not write `"$SSH_AUTH_SOCK"` in JSON. This makes a shared team configuration usable across machines and agent restarts.

| Agent | `identityAgent` value in JSON |
| --- | --- |
| Current macOS/Linux agent, including an agent forwarded into the current shell | `"SSH_AUTH_SOCK"` |
| A specific Unix socket | `"/absolute/path/to/agent.sock"` |
| Windows OpenSSH agent | `"\\\\.\\pipe\\openssh-ssh-agent"` |
| Windows Pageant | `"pageant"` |

For an agent-only profile, omit `privateKey`, `keyPassphrase`, and `password`, including credentials inherited from parent/base profiles or stored securely for that profile. A resolved agent takes precedence over these credentials. If `SSH_AUTH_SOCK` is unset, other configured credentials can be used, so leaving them configured can hide an agent setup problem.

### 5. Connect with Zowe CLI

Run a command using the profile, from the directory containing your team configuration:

```sh
zowe zo list uss-files /u/ibmuser --ssh-profile zos_agent
```

If the Remote SSH server has not been installed yet, first run `zowe zo server install --ssh-profile zos_agent`. For an existing installation outside the default location, add its directory as `serverPath` in the profile.

### Agent authentication and forwarding

`identityAgent` selects the agent used to log in to the target. Agent forwarding makes that agent available to programs running on the target for further SSH connections.

In this version, `agentForward` is an underlying `ssh2` connection option, not a supported Remote SSH team-profile property. Adding `"agentForward": true` to `zowe.config.json` does not enable forwarding in this plug-in. Use `identityAgent` for the setup above; ordinary Zowe operations do not require forwarding.

If a separate OpenSSH connection forwards an agent into the shell where you run Zowe, the `"SSH_AUTH_SOCK"` profile value can use that socket. Enable forwarding only to hosts you trust: processes with access to the forwarded socket can request signatures from your agent.

## Use an OpenSSH user certificate with an agent

A user certificate lets the server trust a certificate authority (CA) that signs users' public keys. The matching private key is still needed in the agent to sign authentication requests. These are OpenSSH certificates, distinct from the X.509 certificates used by z/OSMF and RACF certificate commands. See [OpenSSH certificate creation](https://man.openbsd.org/ssh-keygen#CERTIFICATES).

**Current CLI limitation:** the plug-in uses `ssh2`, which skips certificate identities returned by the agent. The following steps configure and verify certificate authentication with the OpenSSH client; they do not enable certificate authentication in `zowe zo` by setting `identityAgent`. A successful CLI connection using the accompanying plain key does not prove that the certificate was used. The SDK's `useNativeSsh` option is not exposed as a CLI team-profile setting.

### 1. Create a separate user key and have it signed

On the user's workstation, create a key specifically for this example. Do not add its public key to `authorized_keys`, so a successful connection demonstrates certificate authentication:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/zos-cert-key -C "Zowe SSH certificate"
```

On a trusted CA administration machine, create a CA key if your organization does not already have one:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/zos-user-ca -C "z/OS SSH user CA"
```

Protect the CA private key with a passphrase and keep it on the CA administration machine. Transfer only the user's `zos-cert-key.pub` there, then sign it:

```sh
ssh-keygen -s ~/.ssh/zos-user-ca -I IBMUSER-zowe -n IBMUSER \
  -V -5m:+1d ./zos-cert-key.pub
```

This produces `zos-cert-key-cert.pub`, valid from five minutes ago until one day from now. `-I` supplies an audit identifier; `-n` supplies the allowed login principal and must match the target SSH username, including case. Return the certificate to the user's workstation as `~/.ssh/zos-cert-key-cert.pub`, alongside the private key.

### 2. Register the CA on the z/OS SSH server

Have the SSH server administrator install the CA **public** key from `zos-user-ca.pub` as a line in `/etc/ssh/trusted_user_ca_keys`, preserving any existing trusted CA keys. Make the file root-owned and writable only by root (`600`, or `644` if other users need read access).

In the active server-side `sshd_config` (normally `/etc/ssh/sshd_config`), configure:

```text
PubkeyAuthentication yes
TrustedUserCAKeys /etc/ssh/trusted_user_ca_keys
```

The server trusts the signing CA; it does not need each user's certificate installed. This follows IBM's [OpenSSH user certificate setup](https://www.ibm.com/docs/en/zos/3.1.0?topic=keys-example-using-openssh-certificates-user-authentication).

With no `AuthorizedPrincipalsFile` or `AuthorizedPrincipalsCommand` configured, the login username must be in the certificate's principals. If your site uses either option, the administrator must authorize the certificate's principal through that mechanism. See [the OpenSSH server configuration reference](https://man.openbsd.org/sshd_config#AuthorizedPrincipalsFile).

Have the administrator validate the active configuration before reloading or restarting the SSH started task using your site's procedure:

```sh
sshd -t -f /etc/ssh/sshd_config
```

Keep an existing administrative session open until a new login succeeds. On z/OS, ensure transferred key files have the text encoding/tagging expected by the installed OpenSSH, and that the site's OpenSSH mode supports these options; IBM documents restrictions for FIPSMODE in the [z/OS `sshd_config` reference](https://www.ibm.com/docs/en/zos/3.2.0?topic=files-sshd-config-openssh-daemon-configuration-file).

### 3. Load the certificate identity into the agent

Back on the user's workstation, with the agent running:

```sh
ssh-keygen -L -f ~/.ssh/zos-cert-key-cert.pub
ssh-add ~/.ssh/zos-cert-key
ssh-add -L
```

`ssh-add` automatically loads the matching `-cert.pub` file along with the private key. Look for an identity beginning with `ssh-ed25519-cert-v01@openssh.com`. If you renew the certificate, run `ssh-add` again to load it. See [the `ssh-add` reference](https://man.openbsd.org/ssh-add).

### 4. Verify certificate authentication

Use the certificate as the selected public identity while the agent supplies the signature:

```sh
ssh -v -p 22 -o IdentityAgent="$SSH_AUTH_SOCK" \
  -o IdentitiesOnly=yes -o PreferredAuthentications=publickey \
  -i ~/.ssh/zos-cert-key-cert.pub IBMUSER@mainframe.example.com
```

In verbose output, confirm that the server accepts the certificate identity (`ED25519-CERT`) and authentication succeeds with `publickey`. When diagnosing rejection, check the certificate's validity interval and principal with `ssh-keygen -L`, then have the administrator check the active CA trust configuration and server logs.

## Troubleshooting

- **Cannot connect to the agent:** run `ssh-add -l` in the same terminal as Zowe. A new terminal or desktop-launched client may not inherit `SSH_AUTH_SOCK`.
- **Agent has no identities:** load the intended key with `ssh-add`, or unlock your agent.
- **All configured authentication methods failed:** confirm the profile's user and host, the authorized public key, and server-side file ownership and permissions. For certificates, check the CLI limitation above before changing server policy.
- **Testing removal:** `ssh-add -d ~/.ssh/zos-agent-key.pub` removes just the example key. Avoid `ssh-add -D` unless you intend to remove every identity from the current agent.
