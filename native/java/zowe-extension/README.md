# zo REST API: Zowe extension deployment guide

`zo-rest` is a Zowe API Mediation Layer (APIML) extension. It runs as a Zowe component (a Java
25 Spring Boot service started by the Zowe launcher), registers with the APIML Discovery
service as `zo`, and serves a z/OSMF-compatible REST API for data sets, USS files and jobs, plus
zo extensions. Requests run under the calling user's own SAF identity.

This guide is a runbook: follow the sections in order on a z/OS system with Zowe 3.x.

Related documents:

- [`../APIML-REGISTRATION.md`](../APIML-REGISTRATION.md): how APIML identifies the service and
  tells whether it is running (registration metadata, liveness model).
- [`../README.md`](../README.md): developer-oriented notes on the Java app and native libraries.
- [`samples/`](samples/): ESM definitions (RACF, ACF2, Top Secret).

Conventions: `<ZOWE_DIR>` is the Zowe runtime directory, `<ZOWE_YAML>` the path to your
`zowe.yaml`, `<EXT_DIR>` the value of `zowe.extensionDirectory`, `<HOST>` the z/OS host name,
`<GW_PORT>` the Gateway port (`zowe.externalPort`, default 7554), `<HLQ>` a high-level
qualifier you can read.

## 1. Get the package

The package is `zo-rest.pax`, built by the **zo-rest Package** GitHub Actions workflow
(`.github/workflows/zo-rest.yml`). Download it from the workflow run's artifacts (artifact name
`zo-rest`; GitHub delivers it inside a zip, so unzip it first):

```sh
gh run download <run-id> --repo zowe/zowex --name zo-rest --dir ./zo-rest-artifact
```

To build it yourself from a checkout, set up `config.yaml` as described in the top-level
README, then run:

```sh
npm run z:rebuild                 # upload sources and build native/c on z/OS
npm run z:build:java_secur        # libzowe-commons-secur.so (needs Java 8 at /usr/lpp/java/J8.0_64)
npm run z:build:java_bindings     # libzo_java.so
npm run z:package:java_extension  # builds the jar locally (JDK 25), assembles and downloads the pax
```

Result: `native/java/dist/zo-rest.pax` (expected: a file of tens of MB containing `manifest.yaml`,
`bin/`, `config/`, `schemas/` and `lib/{zo-java-app.jar,libzo_java.so,libzowe-commons-secur.so}`).

## 2. Prerequisites

| Requirement | Detail | How to check |
| --- | --- | --- |
| z/OS | 2.5 or later | `D IPLINFO` |
| Zowe | 3.x, with the `apiml` component (Gateway, Discovery, API Catalog) running, and the SAF authentication provider (`components.gateway.apiml.security.auth.provider: saf`) | `curl -k https://<HOST>:<GW_PORT>/gateway/api/v1/auth/query` and `zowe.yaml` |
| Java | **Java 25 or later** (IBM Semeru Runtime for z/OS 25), a separate install from Zowe's own `java.home` (Zowe 3.x uses Java 17) | `<JAVA25>/bin/java -version` shows `version "25..."` |
| zFS space | about 900 MB for the JDK, plus the extension (~tens of MB) and logs | `df -k /usr/lpp/java` |
| Shell | USS shell with `pax`, `iconv`, `chtag`, `extattr` | `which pax extattr` |
| Installer authority | Write access to `<EXT_DIR>`; `READ` on `FACILITY BPX.FILEATTR.PROGCTL` or a security admin to run `extattr +p` (see section 4) | |

Install the JDK (example path `/usr/lpp/java/J25.0_64`) by extracting IBM's pax/archive as
documented by IBM for the Semeru Runtime. Then:

```sh
/usr/lpp/java/J25.0_64/bin/java -version
# expect: openjdk version "25..." / IBM Semeru Runtime Open Edition ...
```

### The JDK mount pitfall (nosetuid)

The JDK must **not** live on a file system mounted `nosetuid` (`NOSETUID`). Such a mount drops
the program-controlled bit on every executable and library in it. Thread-level SAF identity
switching then fails (requests fail with security environment errors even though the service
starts and the health check is UP).

Check:

```sh
df -v /usr/lpp/java/J25.0_64
# Look at the mount line; the "Mounted" attributes must not include NOSETUID.
# Example of a good line:  ...  Read/Write  File System Name: OMVS.JAVA25.ZFS ... Setuid allowed
```

If it says `nosetuid`, remount as a superuser (or `BPX.SUPERUSER` with `su`):

```sh
/usr/sbin/unmount -f /usr/lpp/java/J25.0_64        # stop anything using the JDK first
/usr/sbin/mount -f OMVS.JAVA25.ZFS -t ZFS -o SETUID /usr/lpp/java/J25.0_64
# or without unmounting, from TSO:  UNMOUNT FILESYSTEM('OMVS.JAVA25.ZFS') REMOUNT(SETUID)
```

Persist it in BPXPRMxx (replace the data set and mount point):

```text
MOUNT FILESYSTEM('OMVS.JAVA25.ZFS') TYPE(ZFS)
      MOUNTPOINT('/usr/lpp/java/J25.0_64') MODE(READ) SETUID
```

If you use a bind to a Zowe directory, check the mount that actually contains the `java`
binary, not just its parent.

## 3. Install

1. Upload the pax in **binary** (it carries extended attributes and tags). With SFTP:

   ```sh
   sftp <user>@<HOST>
   sftp> binary
   sftp> put zo-rest.pax /tmp/zo-rest.pax
   ```

   Do not use `scp` text mode or FTP ASCII; both corrupt the archive.

2. Make sure the extension directory is writable by your user:

   ```sh
   ls -ld <EXT_DIR>        # expect your user (or a group you belong to) to have write
   ```

3. Install:

   ```sh
   <ZOWE_DIR>/bin/zwe components install -o /tmp/zo-rest.pax -c <ZOWE_YAML>
   ```

   Expected: messages ending with the component installed under `<EXT_DIR>/zo-rest`, and
   `bin/install.sh` runs `extattr +p` on each `lib/*.so`. If `ZOREST002W` appears, the installer
   lacks `BPX.FILEATTR.PROGCTL`; see section 4.

4. Verify the layout and attributes:

   ```sh
   ls <EXT_DIR>/zo-rest
   # bin  config  lib  manifest.yaml  schemas
   ls -E <EXT_DIR>/zo-rest/lib/*.so      # extended attributes: the 'p' (program controlled) flag
   ls -T <EXT_DIR>/zo-rest/config/application.yml   # expect: UTF-8 text tag (t ISO8859-1/UTF-8)
   ```

   `zwe` also registers the extension in `zowe.yaml` (`zowe.extensionDirectory` must contain it).

5. Configure `zowe.yaml`:

   ```yaml
   zowe:
     launchScript:
       startupChecks:
         zosmf: warn          # z/OSMF is not required for zo-rest; see Troubleshooting
   components:
     zo-rest:
       enabled: true
       javaHome: /usr/lpp/java/J25.0_64   # Java 25+, NOT zowe.java.home
       port: 27610                        # optional; default 27610
   ```

   Only `javaHome` is required (when unset the launcher falls back to `JAVA_HOME`, which on a
   Zowe system is normally Java 17 and is rejected). All other keys are in section 8.

## 4. Security definitions (ESM)

The service runs under Zowe's STC user and acts on behalf of each end user. Ready-to-adapt
definitions are in [`samples/`](samples/):

| ESM | File | Status |
| --- | --- | --- |
| RACF | [`samples/zo-rest-racf.jcl`](samples/zo-rest-racf.jcl) | primary |
| CA ACF2 | [`samples/zo-rest-acf2.txt`](samples/zo-rest-acf2.txt) | unverified translation |
| CA Top Secret | [`samples/zo-rest-tss.txt`](samples/zo-rest-tss.txt) | unverified translation |

Edit the placeholder names (`ZWESVUSR` is Zowe's default STC user), review, then submit. The
requirements the samples implement:

### 4.1 Service user: `FACILITY BPX.SERVER`

`ZWESVUSR` (the Zowe STC user) needs **UPDATE** on `FACILITY BPX.SERVER`. That lets it create a
per-user security environment without a password, which is how requests carrying an APIML JWT
are executed under the end user's identity. Zowe's `ZWESECUR` normally grants it. Check:

```text
RLIST FACILITY BPX.SERVER AUTHUSER
```

Expected: `ZWESVUSR` listed with `UPDATE` (or a group containing it). If missing, requests fail
with a security environment or `EDC5139I Operation not permitted` style errors in the log.

### 4.2 Program control

If `BPX.DAEMON` or `BPX.SERVER` program control is active (`RLIST FACILITY BPX.DAEMON` exists
and `BPX.SERVER` is defined), **every module loaded into the JVM address space must be program
controlled**. That covers:

- the JDK (IBM ships it with the program-controlled attribute; keep it on a `SETUID` mount),
- `libzo_java.so` and `libzowe-commons-secur.so`, which `bin/install.sh` marks with
  `extattr +p`.

The installer user needs `READ` on `FACILITY BPX.FILEATTR.PROGCTL` for that. Otherwise a
security administrator runs, after install:

```sh
extattr +p <EXT_DIR>/zo-rest/lib/*.so
ls -E <EXT_DIR>/zo-rest/lib/*.so       # confirm
```

### 4.3 End users

End users need only their normal data set, USS, JES and SAF authority; requests run under their
own identity. There is **no** z/OSMF-style `IZUDFLT`/`ZMFAPLA` check. Certificate and keyring
endpoints additionally need `IRR.DIGTCERT.*` (FACILITY) and `IRR.RDATALIB` access, the same as
for `RACDCERT` and `R_datalib`.

### 4.4 Optional APPL gate (not yet wired)

`zowe.commons.security.saf.applid` can restrict logon to an APPL profile. The extension config
(`config/application.yml`, `bin/start.sh`) does **not** pass this setting yet, so defining an
APPL profile has no effect today. The sample includes it for when it is wired.

### 4.5 TLS

The service uses Zowe's keystore or keyring from `zowe.certificate`. For keyrings,
`ZWESVUSR` needs the same ring access Zowe already uses, so nothing extra is required.
`bin/start.sh` selects the `safkeyring` protocol handler from `keystore.type`:

| `zowe.certificate.keystore.type` | `-Djava.protocol.handler.pkgs` |
| --- | --- |
| `JCERACFKS` or other | `com.ibm.crypto.zsecurity.provider` |
| `JCECCARACFKS` | `com.ibm.crypto.hdwrCCA.provider` |
| `JCEHYBRIDRACFKS` | `com.ibm.crypto.ibmjcehybrid.provider` |
| `PKCS12` | `com.ibm.crypto.zsecurity.provider` (set, but unused for file keystores) |

### 4.6 Port

Optionally reserve the port in the TCP/IP profile for the started task (job name is
`<zowe.job.prefix>ZO`, for example `ZWE1ZO`; the wildcard form `ZWE*ZO` is what the request
asked for, but TCP/IP only allows `*` as a trailing wildcard, so use the exact name unless you
verified otherwise):

```text
27610 TCP ZWE1ZO
```

## 5. Zowe deployment structure

| Item | Value |
| --- | --- |
| Component directory | `<EXT_DIR>/zo-rest/{bin,config,lib,schemas,manifest.yaml}` |
| Entry points | `bin/start.sh` (launch), `bin/install.sh` (install-time) |
| Libraries | `lib/zo-java-app.jar`, `lib/libzo_java.so`, `lib/libzowe-commons-secur.so` |
| Logs | `<zowe.logDirectory>/<job-prefix>_<instance>_zo-rest_*.log` |
| Job name | `<zowe.job.prefix>ZO` (set through `_BPX_JOBNAME`), shows in SDSF/`D OMVS` |
| APIML service id | `zo`; Gateway routes `/zo/api/v1/zosmf/...` and `/zo/api/v1/...` |

Find logs:

```sh
ls -lt <zowe.logDirectory> | grep zo-rest
tail -f <zowe.logDirectory>/*zo-rest*.log
```

## 6. Start and verify

JVM startup can take **about 5 to 6 minutes on small LPARs**. The Gateway's service cache lags
**1 to 3 minutes** behind the Discovery `UP` status, so allow for both.

1. Start Zowe (`zwe start`, or `S ZWESLSTC` from the console), or restart only this component:

   ```text
   F ZWE1AS,APPL=STOP(zo-rest)
   F ZWE1AS,APPL=START(zo-rest)
   ```

   `ZWE1AS` is your Zowe STC name. Look for `ZOREST000I Starting zo REST API on ...` in the log.

2. Health directly on the service (no auth):

   ```sh
   curl -k https://<HOST>:27610/application/health
   # {"status":"UP"}
   ```

   `DOWN` means the native libraries failed to load or `zo_c_init` failed; read the log.

3. Through the Gateway (wait for the cache; 503 right after `UP` is expected):

   ```sh
   curl -k -o /dev/null -w "%{http_code}\n" \
     "https://<HOST>:<GW_PORT>/zo/api/v1/zosmf/restfiles/ds?dslevel=<HLQ>"
   # 401  (no credentials)

   curl -k -u <user>:<password> -H "X-CSRF-ZOSMF-HEADER: true" \
     "https://<HOST>:<GW_PORT>/zo/api/v1/zosmf/restfiles/ds?dslevel=<HLQ>"
   # 200 with {"items":[...],"returnedRows":N,...}
   ```

4. Zowe CLI, reusing an existing z/OSMF profile and overriding port and base path:

   ```sh
   zowe files list ds "<HLQ>.*" --zosmf-profile <existing> \
     --port <GW_PORT> --base-path /zo/api/v1
   ```

   Expected: the same data set list z/OSMF would return.

5. Discovery check (optional): the API Catalog tile `zo REST API` appears, and
   `https://<HOST>:<GW_PORT>/eureka/apps/ZO` (Discovery, with a client cert) lists the instance
   with status `UP`. See [`../APIML-REGISTRATION.md`](../APIML-REGISTRATION.md).

## 7. How `components.zo-rest.*` maps to the launcher

Zowe exports `components.zo-rest.<key>` as `ZWE_configs_<key>` (dots become `_`) for `bin/start.sh`.

## 8. Configuration reference

| Key (`components.zo-rest.`) | Default | Effect in `bin/start.sh` / `application.yml` |
| --- | --- | --- |
| `enabled` | `false` unless set | Zowe starts the component |
| `port` | `27610` | `ZO_PORT` to `server.port` and the registered port |
| `javaHome` | empty (falls back to `JAVA_HOME`) | `ZO_JAVA_HOME`; its `bin/java` must be version 25 or later, else `ZOREST001E` |
| `heap.init` | `64` | `-Xms<N>m` |
| `heap.max` | `512` | `-Xmx<N>m` |
| `logging.level` | `INFO` | `ZO_LOG_LEVEL` to `logging.level.root` |
| `logging.nativeLevel` | `WARN` | `ZO_NATIVE_LOG_LEVEL` to `logging.level.org.zowe.zo.native` (`OFF` allowed) |
| `security.requireCsrfHeader` | `true` | `ZO_REQUIRE_CSRF_HEADER` to `zo.security.require-csrf-header` (requires `X-CSRF-ZOSMF-HEADER` on state-changing calls) |
| `debug` | `false` | adds the Spring profile `debug` (`spring.profiles.active=zos,debug`) |
| `experimental.spawningCommands` | `false` | evaluation only: exposes `unixCommand` and `tsoCommand` (child processes); sets `ZO_EXPERIMENTAL_SPAWNING` and `ZO_EXPERIMENTAL_METHODS` |

Derived from other Zowe settings (not `components.zo-rest`):

| Zowe setting | Used for |
| --- | --- |
| `zowe.certificate.keystore.*`, `truststore.*` | `ZO_KEYSTORE*`, `ZO_TRUSTSTORE*`: server TLS and APIML client TLS |
| `zowe.verifyCertificates` (`STRICT`, `NONSTRICT`, `DISABLED`) | `ZO_VERIFY_CERTIFICATES`, `ZO_NONSTRICT_VERIFY_CERTIFICATES` |
| `zowe.externalDomains[0]`, `haInstance.hostname` | `ZO_HOSTNAME`: registered host name |
| `zowe.externalPort` or `components.gateway.port` | `ZO_GATEWAY_PORT` (registration metadata) |
| `ZWE_DISCOVERY_SERVICES_LIST` or `components.discovery.port` | `ZO_DISCOVERY_URLS` |
| `zowe.network.server.listenAddresses[0]` | `ZO_LISTEN_ADDRESS` (default `0.0.0.0`) |
| `zowe.job.prefix` | job name `<prefix>ZO` |

## 9. Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Registration fails with HTTP 400 from Discovery | Metadata URLs (swagger, documentation, status) are on a host outside `apiml.security.allowedDomains` | Add the service host to `components.discovery`/`gateway` `apiml.security.allowedDomains` in `zowe.yaml` (or set the externalDomains to match), restart APIML and zo-rest |
| `MalformedInputException` at startup | `config/application.yml` is not UTF-8 (tagged or converted as EBCDIC) | `iconv -f IBM-1047 -t UTF-8 application.yml > tmp && mv tmp application.yml && chtag -tc UTF-8 application.yml`. `package.sh` already does this; re-uploading text with auto-conversion undoes it |
| Garbled characters in logs | A logback charset was forced (`-Dfile.encoding`/logback `<charset>`) that does not match the log tag | Do not force a logback charset; keep `-Dfile.encoding=UTF-8` and let the file tag stay as-is |
| `eurekaAutoServiceRegistration` NPE at startup | Spring Cloud's Eureka client conflicts with the APIML onboarding enabler | Keep `eureka.client.enabled: false` in `application.yml` (already set); registration is done by `apiml.enabled` |
| `Unable to access jarfile //lib/zo-java-app.jar` | `start.sh` could not locate its home (no `manifest.yaml` plus `lib/zo-java-app.jar` under `zowe.extensionDirectory/zo-rest`, `<runtime>/components/zo-rest`, or cwd) | Confirm `zowe.extensionDirectory` and that the pax installed both files; check `ls <EXT_DIR>/zo-rest/lib` |
| 503 / `ZWEAG510E` through the Gateway right after UP | Gateway cache refresh lags Discovery by 1 to 3 minutes | Wait and retry |
| Zowe fails to start: z/OSMF check | Startup check expects z/OSMF | `zowe.launchScript.startupChecks.zosmf: warn` in `zowe.yaml` |
| Listener dead after a TCP/IP stop/start event; health refused | JVM socket did not recover | Recycle: `F <stc>,APPL=STOP(zo-rest)` then `APPL=START(zo-rest)` (`-Dibm.serversocket.recover=true` helps but is not guaranteed) |
| `ZOREST001E Java ... is too old` | `javaHome` points at Java 17 (Zowe's) | Set `components.zo-rest.javaHome` to a Java 25 install |
| `ZOREST002W Could not set program-controlled attribute` | Installer lacks `BPX.FILEATTR.PROGCTL` | Admin runs `extattr +p <EXT_DIR>/zo-rest/lib/*.so` |
| Health UP but requests fail with security environment errors | JDK on a `nosetuid` mount, libraries not `+p`, or `BPX.SERVER` UPDATE missing | Sections 2 and 4 |

## 10. Uninstall

1. Stop only the component: `F <stc>,APPL=STOP(zo-rest)` (or stop Zowe).
2. If your Zowe level has it, `zwe components uninstall` for `zo-rest`; otherwise remove the
   directory manually: `rm -rf <EXT_DIR>/zo-rest`.
3. Remove the `components.zo-rest` block from `zowe.yaml` (and the `startupChecks.zosmf` override
   if you added it only for this service).
4. Remove ESM grants only if dedicated to zo-rest (the optional APPL and `IRR.DIGTCERT.*`
   grants, an installer `BPX.FILEATTR.PROGCTL` grant). **Never** remove `BPX.SERVER` access for
   `ZWESVUSR`: Zowe itself relies on it.
5. Optionally remove the Java 25 install and its TCP/IP port reservation.

## Appendix: CI package

The `.github/workflows/zo-rest.yml` workflow runs the Java tests on a GitHub runner (JDK 25),
then builds `native/c`, `libzowe-commons-secur.so` and `libzo_java.so` on the shared z/OS build
host and assembles `zo-rest.pax`. The pax contains the jar, both libraries, scripts, config, schema and
manifest; it does not contain the `samples/` directory (those are in the repository).
