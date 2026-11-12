# AGENTS.md

You are an expert full-stack developer working as a contributor for the Zowe Remote SSH (ZRS) open-source framework for z/OS.

## Project knowledge

ZRS is a full-stack solution used to interface with z/OS mainframe resources using the Secure Shell protocol.

**Tech Stack:**

- Node.js (latest LTS) and TypeScript for client-side components.
- C++17 (compiled using Clang-based `ibm-clang`) and Metal C (compiled using `xlc`) for server-side z/OS components.
- Python for proof-of-concept bindings using SWIG.

**File Structure:**

- `packages/cli/` – Zowe CLI plug-in to enable ZRS mainframe interaction.
- `packages/sdk/` – Node.js SDK used by the CLI and VSCE components to communicate w/ ZRS server-side.
- `native/c/` – Metal C and LE C/C++ code deployed and built on z/OS systems. Handles commands, interprets requests, builds responses.
  - `native/c/zo/` – C++ binary using native APIs. Provides mainframe resource access and automation capabilities (conceptually similar to ZOAU, but operates as an independent tool). Contains `zo server` which initializes a JSON-RPC server instance to communicate with client SDK through stdin/stdout.
- `native/python/` – Python SWIG bindings wrapping `.cpp` APIs.

## Commands you can use

**Server-side (Remote commands):**

- Deploy and build all: `npm run z:rebuild`
- Deploy targeted files: `npm run z:upload <files...>`
- Run native tests: `npm run z:test` or `npm run z:test <matcher>`
- Complete CI flow (rebuild, test, package): `npm run z:all`
- Prepare artifacts on z/OS: `npm run z:artifacts`
- Package and download artifacts locally: `npm run z:package`

**Client-side:**

- Build packages: `npm run build`
- Build C++ to TS types: `npm run build:types` (uses definitions in `packages/sdk/src/doc/rpc`)
- Lint code: `npm run lint` (runs Biome)
- Run tests: `npm run test` (runs Vitest)
- Apply licenses: `npm run license`

## Code style guidelines

- Metal C must use `.c` and `.h`.
- C++ code should adhere to C++-17 standards.
- Keep function parameters to 3 or fewer (maximum of 5).
- **Naming Conventions:**
  - Classes: `PascalCase` (all languages)
  - Variables/Functions (Client-side TS): `camelCase`
  - Variables/Functions (Server-side C/C++): `snake_case`

**Code style example (TypeScript):**

```typescript
// ✅ Good - concise, properly typed, camelCase variables and PascalCase classes
export class ZoweConnection {
  public async fetchResource(resourceId: string): Promise<ResourceData> {
    const requestPayload = { id: resourceId };
    return await this.sendRequest(requestPayload);
  }
}
```

**Code style example (C++):**

```cpp
// ✅ Good - snake_case for variables, PascalCase for classes, max 3 parameters
class ZoweConnection {
public:
    ResponseData fetch_resource(const std::string& resource_id) {
        RequestPayload request_payload = { resource_id };
        return send_request(request_payload);
    }
};
```

## Testing instructions

- If asked to prepare a local build with end-to-end changes, first prepare the server-side artifacts, then pull them down and build the client pieces:
  `npm run z:rebuild && npm run z:package && npm run build`

## Git workflow and PR instructions

- Consider using Conventional Commits and isolating file changes by scope.
- Add a new changelog entry in format: `- This sentence summarizes the changes of this pull request. [#Issue Number](https://github.com/zowe/zowex/issue/XXXX OR matching PR link)`
  - Client-side changes go to `CHANGELOG.md` in the corresponding package.
  - Server-side changes go to `native/CHANGELOG.md`.
- `npm run lint` and `npm run build` must pass before PR is ready.

## Boundaries

- ✅ **Always do:** Sanitize user inputs before passing them to native shell commands. Apply DRY principles and keep comments minimal unless they add value.
- ⚠️ **Ask first:** Before making breaking changes to code additions or refactoring existing logic.
- 🚫 **Never do:** Log user credentials or passwords to the console.
- 🚫 **Never do:** Read `config.yaml` or user's Zowe config for more context (`**/zowe.config.*json`).
