# zo Java REST API

A Spring Boot REST API that exposes z/OSMF-compatible endpoints (data sets, USS files, and
jobs) backed directly by the native `zo` C++/Metal C libraries in `native/c`. It calls into
those libraries via the Java 25 Foreign Function & Memory (FFM/Panama) API instead of shelling
out to the `zo` CLI, and authenticates/authorizes requests against SAF (RACF) through a small
JNI bridge or against Zowe API Mediation Layer over HTTPS. The service registers with the Zowe API Mediation Layer 
(Discovery/Eureka) so it can be reached through the API Gateway alongside other Zowe services.

## Layout

| Path          | Description |
| ------------- | ----------- |
| `app/`        | The Spring Boot application (Gradle project) — controllers, FFM bindings, SAF security integration. |
| `bindings/`   | C++ glue library (`libzo_java.so`) that adapts `native/c`'s `libzds`, `libzjb`, `libzusf`, `libzut` static libraries into a shared object the JVM can call via FFM. |
| `secur/`      | JNI library (`libzowe-commons-secur.so`) wrapping SAF calls (`createSecurityEnvironment`, thread-level security, etc.) used for HTTP Basic authentication. |
| `jaas-poc/`   | Standalone proof-of-concept (`OS390Tester.java`) exercising IBM's `OS390ThreadSubject`/JAAS APIs; not part of the app build. |

## Prerequisites

- **Java 25** — the app's Gradle toolchain requires it (uses the FFM API, which is finalized in
  JDK 25). On z/OS this means IBM Semeru/OpenJDK 25 for z/OS.
- **Gradle** — invoked via the included wrapper (`app/gradlew`), no separate install needed.
- On z/OS, to build the native pieces:
  - IBM Open XL C/C++ (`ibm-clang++64`) and IBM XL C/C++ (`xlc`), same toolchain used by
    `native/c` (see `native/c/toolchain.mk`).
  - The `native/c` static libraries already built (`libzds.a`, `libzjb.a`, `libzusf.a`,
    `libzut.a` under `native/c/build-out`) — `bindings/` links against them.
  - **Java 8** — `secur/makefile` currently hardcodes `JAVA_HOME=/usr/lpp/java/J8.0_64` and
    compiles/binds against its JNI headers and sidedeck (`bin/j9vm/libjvm.x`), so building
    `java_secur` requires a Java 8 install at that path on the target system, independent of the
    Java 25 the app itself runs on.

## Building

The `bindings/` and `secur/` native libraries only build on z/OS — they depend on `xlc`/
`ibm-clang++64` and z/OS-only headers, so they can't be compiled locally. Building them is
driven by the same SSH-based build tooling used for the rest of `native/`: set up `config.yaml`
at the repo root as described in the [top-level README](../../README.md#setup) (`sshProfile`,
`deployDir`), then build from the repo root using these npm scripts:

| Script | What it does |
| ------ | ------------ |
| `npm run z:build:java_secur` | Uploads `native/java/secur` to the remote deploy dir and runs `make` there, producing `libzowe-commons-secur.so`. |
| `npm run z:build:java_bindings` | Uploads `native/java/bindings` and runs `make` there, producing `libzo_java.so`. Requires `native/c`'s static libraries (`libzds.a`, `libzjb.a`, `libzusf.a`, `libzut.a`) already built in the same remote deploy dir (`npm run z:build`). |
| `npm run z:build:java_app` | Builds the Spring Boot app **locally** with `./gradlew clean build -x test`, then uploads the resulting jar to `<deployDir>/java/app/build/libs/` over SFTP. |
| `npm run z:build:java_full` | Runs all three of the above in order (secur → bindings → app) — use this for a full rebuild. |

Under the hood, `z:build:java_secur`/`z:build:java_bindings` just `cd` into the deployed
`java/secur`/`java/bindings` directory on z/OS and run `make` (append `-DBuildType=DEBUG` or
`-DBuildType=RELEASE` via the usual build-tool flags for a debug/optimized build) — so if you're
already logged into USS with the sources deployed, running `make` directly in those directories
works the same way. On other platforms you can still work on the Java code, but the app won't be
able to load the native libraries at runtime — see [Native bindings](#native-bindings) below.

To build just the Java app locally without touching the remote host (e.g. to run tests or check
compilation):

```bash
cd app
./gradlew build
```

## Running

```bash
cd app
java --enable-native-access=ALL-UNNAMED -Djava.library.path="../bindings/build-out:../secur" -Djava.protocol.handler.pkgs=com.ibm.crypto.provider -jar build/libs/zo-java-app.jar --spring.config.additional-location=file:./src/main/resources/application.yml --spring.profiles.active=zos 
```

The app expects the following to be reachable/configured (see `app/src/main/resources/application.yml`):

- A TLS keystore at `classpath:config/service-keystore.p12` (PKCS12, referenced by
  `server.ssl.key-store*`). No keystore is checked into the repo — generate one for your
  environment and place it under `app/src/main/resources/config/` before packaging, or override
  `server.ssl.*` externally. You can also use existing keystore/keyring if it's available.
- The native libraries described below, loadable at startup.
- Optionally, a running Zowe API Mediation Layer Discovery service (`eureka.client.serviceUrl.defaultZone`,
  default `https://localhost:10011/eureka/`) for API Gateway registration — set
  `zowe.apiml.discovery.enabled: false` to disable if you're running standalone.

By default the service listens on HTTPS port `10190` and exposes z/OSMF-style routes, e.g.
`/zosmf/restfiles/ds`, `/zosmf/restfiles/fs`, `/zosmf/restjobs/jobs`. Swagger UI is available
(unauthenticated) at `/swagger-ui.html`.

### Authentication

Two authentication paths are wired in ahead of Spring Security's default filter:

- **HTTP Basic** — validated against SAF via the JNI `Secur` bridge (`secur/`), so credentials
  must be valid mainframe user IDs/passwords.
- **Bearer JWT** — validated by calling back into the APIML ZAAS client (`zaas-client`), for
  tokens issued by the API Gateway. This method requires REST API to be registered with API ML.

A `SecurityContextController` exposing SAF/security-context troubleshooting endpoints
(`/api/v1/securityTest/**`) exists for testing only and is disabled by default
(`zowe.apiml.security.test-endpoints-enabled: false`) — do not enable it in production.

## Deploying to z/OS

Running `npm run z:build:java_full` (or the individual scripts, in order) already leaves
everything in place on the remote host under `<deployDir>/java/`:

- `java/secur/libzowe-commons-secur.so`
- `java/bindings/build-out/libzo_java.so`
- `java/app/build/libs/zo-java-app-<version>.jar`

From there, to actually run the service:

1. Both `.so` files already carry the program-controlled bit — `bindings/Makefile` and
   `secur/makefile` each run `extattr +p` as part of the build — but it's worth re-checking with
   `ls -E` if you move/copy them elsewhere, since program control is required for the SAF calls
   they make to succeed.
2. Set `LIBPATH` (or `java.library.path`) to include both `.so` directories, so
   `NativeLoader`'s `System.loadLibrary("zo_java")` fallback and `Secur`'s
   `System.loadLibrary("zowe-commons-secur")` can find them. (`NativeLoader` also tries a
   relative dev path first, `../bindings/build-out/libzo_java.so`, which resolves correctly if
   you run the jar from `<deployDir>/java/app`.)
3. Ensure a TLS keystore is present on the classpath/filesystem per [Running](#running).
4. Start the service — `java -jar zo-java-app-<version>.jar` from `<deployDir>/java/app/build/libs` —
   with a JCL job, started task, or shell wrapper as appropriate for your environment.

## Native bindings

The app talks to native code through two independent mechanisms:

- **FFM (Project Panama)** — `ffm/NativeLoader` loads `libzo_java.so` (first trying a
  relative dev path, `../bindings/build-out/libzo_java.so`, then falling back to
  `System.loadLibrary("zo_java")` via `java.library.path`/`LIBPATH`). `ffm/*Bindings` classes
  (`ZdsBindings`, `ZjbBindings`, `ZusfBindings`) use `java.lang.foreign` to call into it; the
  `ffm/generated/*_C.java` classes are `jextract`-style struct/layout descriptors generated from
  the C headers in `bindings/*.h`, mirroring the C structs used by `native/c`.
- **JNI** — `zos/security/jni/Secur` declares `native` methods implemented in `secur/secur.c`
  (built into `libzowe-commons-secur.so`) and loaded via plain `System.loadLibrary`. This is used
  for SAF security-environment management (`createSecurityEnvironment`, thread-level security
  for Basic Auth) rather than the FFM path, since it predates/complements the FFM bindings.

