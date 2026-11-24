---
name: review-prs
description: Review pull requests for the zowex codebase. Analyze C++ backend and middleware (ibm-clang), TypeScript clients (CLI, SDK, VS Code extension), and z/OS-specific concerns. Use when asked to review a PR, branch, or code changes.
---

# Review PRs

Review pull requests for code quality, security, and adherence to project standards.

## Review Workflow

1. **Get changed files**: `git diff --name-only main...HEAD 2>nul` (or target branch)
2. **Get full diff**: `git diff main...HEAD 2>nul`
3. **Read key changed files** to understand context
4. **Categorize findings** using the format below
5. **Verify PR checklist** items from the workspace's `.github/PULL_REQUEST_TEMPLATE.md`

## Output Format

Categorize all findings:

### 🔴 Critical

Issues that must be fixed before merge:

- Security vulnerabilities
- Breaking changes to public APIs
- Data corruption risks
- Memory leaks or resource handling bugs

### 🟡 Warning

Issues that should be addressed:

- Missing error handling
- Performance concerns
- Incomplete implementations
- Missing tests for new functionality

### 🟢 Suggestion

Optional improvements:

- Code style or readability
- Refactoring opportunities
- Documentation improvements

---

## Platform-Specific Checks

### C++ & Metal C Backend (`native/c/`, `native/zowed/`)

**Compiler constraints:**

- Backend and JSON-RPC server uses **ibm-clang**, supporting C++17

**Check for:**

- Manual memory management: proper `new`/`delete` pairing, no leaks
- EBCDIC/ASCII encoding issues for z/OS
- Proper error handling with return codes
- Thread safety in `zo server` code

**Style (from .clang-format):**

- Allman-style braces
- 2-space indentation
- Pointer alignment: right (`char *ptr`)

Use the [cpp-code-quality](../cpp-code-quality) skill to evaluate quality of new or changed C++ code.
Use the [metalc-code-quality](../metalc-code-quality/) skill to evaluate quality of new or changed Metal C code.

### TypeScript Clients (`packages/cli/`, `packages/sdk/`)

**Check for:**

- Node.js LTS compatibility
- Proper async/await error handling
- Type safety (avoid `any` outside tests)
- Consistent API patterns across handlers

**Style (from biome.json):**

- 4-space indentation
- Double quotes
- 120 character line width
- No unused imports

Use the [frontend-code-quality](../frontend-code-quality/) skill to evaluate quality of new or changed TypeScript code.

---

## Breaking Changes Detection

Flag as 🔴 Critical if changes affect:

**C++ API:**

- Function signatures in public headers (`native/c/*.h`, `native/c/*.hpp`)
- RPC message structures
- Return code meanings

**TypeScript API:**

- Exported functions/types in `packages/sdk/src/`
- CLI command options or output format
- VS Code extension API surfaces

---

## Security Checklist

- [ ] No hardcoded credentials or secrets
- [ ] Input validation for user-provided data
- [ ] Proper bounds checking in C++ code
- [ ] No path traversal vulnerabilities
- [ ] Secure handling of authentication tokens

---

## PR Template Verification

Ensure the PR addresses:

- [ ] **What It Does**: Clear description of changes
- [ ] **How to Test**: Verification steps provided
- [ ] **Tests**: Added/updated for new functionality
- [ ] **Changelog**: Updated if user-facing change
- [ ] **Contribution guidelines**: Followed

---

## Review Questions

Consider these during review:

- Does this change make sense in the overall architecture?
- Is there duplicated logic that could be refactored (DRY)?
- Are variable/function names clear without excessive comments?
- Is error handling comprehensive?
- Could this be more efficient with different data structures/algorithms?
