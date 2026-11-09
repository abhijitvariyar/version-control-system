# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

`vcs` is a from-scratch C++20 version control system built as a learning project: SHA-256, LZ77 +
Huffman compression, Myers diff and three-way merge are all hand-written, with no external libraries.
The full design and milestones (M0–M7) are in `PLAN.md`.

The code is split into a `vcs_core` static library (`src/`, headers in `include/vcs/`), a thin
`vcs` CLI (`src/cli/main.cpp`), and a `vcs_tests` executable (`tests/`).

## Who writes what

- **The user writes the implementation** in `src/` and `include/`. Don't change these unless asked.
- **Claude writes and maintains the tests** in `tests/`. When a test exposes a bug, leave the test
  failing and report the bug with its `file:line` in `src/`. Don't fix the bug.

## README

Keep `README.md` up to date. Whenever a feature is finished or verified, whether Claude wrote it
or the user did, update it in the same turn:

- the milestone table and "What works right now"
- "Known limitations", removing anything no longer true
- usage, on-disk format and project layout, if they changed

Only describe what exists and passes its tests. Don't list planned work as working.

## Build

Standard CMake out-of-tree build, configured into `bin/`. Use Debug so the address and
undefined-behavior sanitizers are on:

```
cmake -S . -B bin -DCMAKE_BUILD_TYPE=Debug
cmake --build bin
```

Warnings are errors (`-Wall -Wextra -Wpedantic -Werror`). The executables are `bin/src/vcs` and
`bin/tests/vcs_tests`. `compile_commands.json` at the repo root is a symlink into `bin/`.

## Tests

```
ctest --test-dir bin --output-on-failure      # every suite
bin/tests/vcs_tests <suite>                   # one suite, e.g. tree
bin/tests/vcs_tests <suite>.<name>            # one test, e.g. object_malformed.missing_nul
```

- **Harness:** `tests/test_harness.hpp` is a small hand-rolled framework with no dependencies.
  - `TEST(suite, name)`, `CHECK`, `CHECK_EQ`, `CHECK_THROWS` and `CHECK_NOTHROW`. A failed check
    is recorded and the test keeps running.
  - `TempDir` is a temporary directory that deletes itself.
  - `bytes("...")` builds a `ByteStream`. Use `bytes_n("a\0b")` when the literal contains `\0`.
  - `read_text` and `write_bytes` read and write files.
- **One ctest entry per suite.** A sanitizer crash in one suite then can't hide the results of
  the others. Inputs that crash on purpose go in a suite of their own, as `object_malformed` does.
- **Adding a test file:** list it explicitly in `tests/CMakeLists.txt` (no `GLOB`), and add its
  suite name to `TEST_SUITES`.
- **Compression suites:** `tests/compress/` holds `bitstream`, `lz77`, `huffman`, `deflate` and
  `deflate_corrupt`. Shared inputs are in `tests/compress/sample_inputs.hpp`.
- **CLI tests** (`tests/cli/`) run the real `bin/src/vcs` binary inside a `TempDir`. CMake passes in
  the binary's path as `VCS_BIN` and the repo root as `VCS_SOURCE_DIR`.
- **Expected values** for hashes come from `shasum -a 256`, for example
  `printf 'blob 5\0hello' | shasum -a 256`. Hard-code them in the test with a comment saying how
  they were generated.
- **Checking tests written ahead of the code:** compile them against a throwaway implementation
  in a scratch copy of the project, never in the repo. Delete that copy afterwards so it doesn't
  give away the user's implementation.
