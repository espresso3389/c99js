# c99js - Claude Code Guidelines

## Project Overview

c99js is a C99-to-JavaScript transpiler. It compiles C99 source code into JavaScript that runs under Node.js or Bun.

## Build

```bash
clang -Wall -Wextra -std=c99 -g -O2 -D_CRT_SECURE_NO_WARNINGS -o c99js.exe src/*.c
```

Or compile individual files:
```bash
clang -Wall -Wextra -std=c99 -g -O2 -D_CRT_SECURE_NO_WARNINGS -c -o obj/<name>.o src/<name>.c
```

## Test

Unit tests (core compiler):
```bash
bash test/run_tests.sh ./c99js.exe bun
```

Challenge tests (real-world libraries):
```bash
bash challenges/run_challenge_tests.sh ./c99js.exe bun
```

## Directory Structure

- `src/` - Compiler source (lexer, parser, preprocessor, sema, codegen)
- `test/` - Core compiler test suite
- `runtime/` - JavaScript runtime (`runtime.js`)
- `challenges/` - Completed challenge libraries (committed, git submodules)
- `challenges/tests/` - Test drivers for completed challenges (committed)
- `challenges_wip/` - Work-in-progress challenge workspace (NOT committed)

## Challenge Workflow

Challenges are real-world C libraries compiled and tested with c99js (tracked in `CHALLENGES.md`).

### Working on a new challenge

1. Clone/copy the library source into `challenges_wip/<name>/`
2. Write a test driver `challenges_wip/<name>/c99js_test.c`
3. Iterate: compile with c99js, fix issues, re-test
4. Temporary/scratch files (`.c`, `.h`, `.js`, `.txt`) stay in `challenges_wip/`

### Promoting a completed challenge

Once all tests pass:
1. Add the upstream repo as a git submodule under `challenges/<name>/`
2. Move the test driver to `challenges/tests/<name>/c99js_test.c`
3. Add `expected.txt` if output verification is needed
4. Verify it passes via `challenges/run_challenge_tests.sh`
5. Update `CHALLENGES.md` status from "pending" to "PASS"
6. Clean up `challenges_wip/<name>/`

### Files that should NOT be committed

- `challenges_wip/` - scratch workspace for in-progress challenges
- `*.ilk` - MSVC incremental linker files
- `*.ini` - test data files used during challenge development
- `*.exe`, `*.pdb`, `*.obj`, `obj/` - build artifacts (already in .gitignore)
