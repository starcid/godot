---
name: debug-ci
description: Guide for debugging failing GitHub Actions CI workflows in this Godot Engine fork. Use this when asked to investigate, diagnose, or fix failing CI builds, test failures, or workflow errors.
---

# Debugging CI Failures in Godot Engine

When asked to debug failing GitHub Actions workflows, follow this process using the GitHub MCP Server tools:

## Step 1 — Locate the failure

1. Use `list_workflow_runs` to list recent workflow runs for the branch or pull request and identify which run(s) are failing.
2. Use `list_workflow_jobs` with the failing run ID to see which jobs failed.

## Step 2 — Read the logs

3. Use `get_job_logs` with `failed_only: true` and the run ID to retrieve logs for all failed jobs at once.
   - Start with `tail_lines: 200` for a quick overview.
   - Increase to `tail_lines: 500` or omit the limit if you need more context.

## Step 3 — Classify the failure type

| Failure type | Typical log signals | Where to look |
|---|---|---|
| **clang-format** | `error: code not formatted` | `.clang-format`, changed `.cpp`/`.h` files |
| **clang-tidy** | `warning:` or `error:` from clang-tidy | `.clang-tidy`, changed source files |
| **SCons build error** | `SyntaxError`, `undefined reference`, `error:` from compiler | Changed `.cpp`/`.h`, `SCsub`, `config.py` |
| **Unit tests** | `FAILED` or `TEST CASE` in doctest output | `tests/` directory |
| **Static checks / docs** | `Missing documentation` or XML schema errors | `doc/classes/*.xml` |
| **Python script** | Traceback in workflow step | `.github/` scripts |

## Step 4 — Fix the issue

### clang-format failures
Run locally:
```sh
clang-format -i <changed_file.cpp> <changed_file.h>
```
Or check `.clang-format` for project style (LLVM-based, tabs for indentation).

### clang-tidy failures
Run locally:
```sh
clang-tidy <changed_file.cpp> -- -I. [compiler flags]
```
Fix the warning in the source. Do **not** suppress with `NOLINT` unless genuinely a false positive.

### Build failures
- Check the exact compiler error line and file.
- Common causes: missing `#include`, wrong type, undefined symbol after refactor.
- The project uses **C++17** and SCons (not CMake).

### Test failures
- Build with `tests=yes`: `scons platform=linuxbsd target=editor tests=yes dev_mode=yes`
- Run: `./bin/godot.linuxbsd.editor.x86_64 --test`
- Tests live in `tests/` and use the [doctest](https://github.com/doctest/doctest) framework.

### Missing doc XML
- New public classes need an entry in `doc/classes/<ClassName>.xml`.
- Regenerate stubs: `./bin/godot.linuxbsd.editor.x86_64 --doctool doc/classes --no-docbase`

## Step 5 — Verify

After applying a fix:
1. Check that the same workflow step would now pass by re-reading the relevant source.
2. If possible, reproduce the failure locally before committing.
3. Commit and push — CI will re-run automatically.
