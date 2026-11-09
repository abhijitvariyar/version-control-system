# Plan: `vcs` — a from-scratch C++ version control system

## Context

The repo at `/Users/abhijitvariyar/Projects/cpp/vcs` is currently an empty CMake skeleton (`main.cpp` prints "Hello World"). The goal is to build a real, working version control system as a **learning project**: understand how git works internally by implementing its core mechanisms yourself rather than leaning on libraries.

Scope and approach were narrowed down with the user:
- **Scope**: a "core local VCS" — init, staging, commit, log, status, diff, branch, checkout, merge — operating on a single local repository. No networking/remotes/push/pull/packfiles.
- **Learning-focused**: hashing and compression are hand-implemented, not pulled from OpenSSL/zlib.
- **Object model**: git-style content-addressable objects (blob/tree/commit), stored by hash as loose objects in `.vcs/objects`.
- **Hashing**: SHA-256, implemented from scratch.
- **Compression**: a hand-rolled LZ77 + Huffman compressor, implemented from scratch, applied to object contents before writing to disk.
- **Merge**: three-way merge — common-ancestor lookup, tree-level auto-merge of non-conflicting changes, `<<<<<<<`/`=======`/`>>>>>>>` conflict markers on collisions, like git.

The intended outcome is working software built incrementally in testable milestones, each one runnable and manually verifiable (comparable to real `git` output) before moving to the next.

## Source layout

Split into a static library + thin CLI, so every algorithm is unit-testable without shelling out to the binary:

```
vcs/
  CMakeLists.txt
  include/vcs/
    hash/sha256.hpp
    compress/lz77.hpp  compress/huffman.hpp  compress/deflate.hpp
    objects/{object,blob,tree,commit,object_store}.hpp
    repo/{repository,index,refs}.hpp
    diff/{myers,tree_diff}.hpp
    merge/{lca,three_way}.hpp
    util/bytes.hpp
  src/
    CMakeLists.txt
    hash/sha256.cpp
    compress/{lz77,huffman,deflate}.cpp
    objects/{object,blob,tree,commit,object_store}.cpp
    repo/{repository,index,refs}.cpp
    diff/{myers,tree_diff}.cpp
    merge/{lca,three_way}.cpp
    cli/main.cpp  cli/arg_parser.{hpp,cpp}  cli/command_registry.{hpp,cpp}
    cli/commands/{init,add,commit,log,status,diff,branch,checkout,merge,hash_object,cat_file}.cpp
  tests/
    CMakeLists.txt
    test_harness.hpp  test_main.cpp
    hash/sha256_tests.cpp
    compress/{lz77,huffman,deflate_roundtrip}_tests.cpp
    objects/object_store_tests.cpp
    diff/{myers,tree_diff}_tests.cpp
    merge/{lca,three_way}_tests.cpp
    repo/repository_integration_tests.cpp
```

`vcs_core` (STATIC lib) holds everything except `cli/`; `vcs` (executable) links `vcs_core` + CLI sources; `vcs_tests` links `vcs_core` + test sources. CLI command files are thin wrappers around a `Repository` façade class, so the exact logic under test is the exact logic the binary runs.

CMake: explicit source-file lists per target (no `GLOB`, which silently misses new files on re-configure). Bump `cmake_minimum_required` from 3.10 to 3.16 for clean multi-target/subdirectory usage.

## On-disk repository format

```
.vcs/
  HEAD              # "ref: refs/heads/main\n"  (or a raw hash for detached HEAD)
  config            # minimal key=value (user.name/user.email)
  index             # staging area, text format: "<mode> <blob-hash-hex> <path>" per line, sorted
  MERGE_HEAD         # present only mid-conflict-resolution; other parent's commit hash
  objects/<2-hex>/<62-hex>   # loose objects, sharded by first byte of the SHA-256 hash
  refs/heads/<branch-name>   # file containing the 64-hex-char commit hash
```

Object formats (git-style header + content; hash is computed on the **uncompressed** buffer, then that buffer is compressed for storage):
- Blob: `"blob " + <decimal size> + '\0' + <raw file bytes>`
- Tree: `"tree " + <decimal size> + '\0' + <entries>`, entries sorted by name: `<mode> ' ' <name> '\0' <32-byte raw hash>` repeated. Modes: `100644` (file) / `040000` (dir) for v1 — executable bit / symlinks deferred as a stretch goal.
- Commit: `"commit " + <size> + '\0' + "tree <hash>\nparent <hash>\n" (0/1/2 parent lines) + "author ...\ncommitter ...\n\n<message>"`.

Deliberate simplifications (flagged, consistent with "learning-focused, minimal deps"):
- Index is a plain sorted text file, not git's binary format with a trailing checksum. (Adding a SHA-256 checksum of the index body is a natural later stretch goal.)
- The compressor implements the core LZ77+Huffman ideas with one Huffman alphabet (256 literal bytes + a match-escape symbol with fixed-width length/distance fields) rather than replicating real DEFLATE's dual literal/length+distance trees and extra-bits tables — those tables aren't essential to the learning goal.
- Merge-base (LCA) walks full ancestor sets; doesn't handle git's multi-merge-base edge cases.

## Core algorithms

- **SHA-256** (`hash/sha256.cpp`): `Sha256` class with `update()`/`finalize()` plus a one-shot `sha256(span<const uint8_t>)` free function, implemented per FIPS 180-4 (padding, message schedule, compression rounds). Tested against NIST vectors (`""`, `"abc"`, the two-block and 1,000,000×`'a'` vectors) plus a streaming-vs-one-shot consistency check.
- **LZ77** (`compress/lz77.cpp`): sliding-window tokenizer (32KB window, ~258-byte max match) with a hash-chain match finder (hash table on 3-byte prefixes) for usable performance; emits literal-byte vs (distance, length) tokens.
- **Huffman** (`compress/huffman.cpp`): canonical Huffman coding — build code lengths via a min-heap tree, serialize an explicit per-symbol length table, bit-pack with canonical codes.
- **Deflate orchestration** (`compress/deflate.cpp`): combines LZ77 tokens + Huffman coding into a compressed container (magic/version + uncompressed length + code-length table + bitstream). Tested via round-trip on empty/single-byte/highly-repetitive/random-incompressible inputs and on real serialized object buffers.
- **Myers diff** (`diff/myers.cpp`): standard O(ND) greedy line-diff, `diff_lines(a, b) -> vector<DiffOp>`. Feeds both `vcs diff` rendering and merge. Tested on identical/total-replacement/pure-insert-delete/textbook cases, verifying edit-script reconstruction.
- **Tree diff** (`diff/tree_diff.cpp`): merge-walk of two trees' sorted entries, recursing into subdirectories and short-circuiting when a subtree hash matches on both sides. Produces `vector<TreeDiffEntry{path, Added|Deleted|Modified, old_hash, new_hash}>`; used by `status`, `diff`, and `merge`.
- **Merge** (`merge/lca.cpp`, `merge/three_way.cpp`): LCA via ancestor-set walk of one side + BFS from the other. Tree-level three-way merge classifies each path from `tree_diff(base, ours)` / `tree_diff(base, theirs)` (one-sided change → take it; identical change on both → no conflict; differing change → conflict). Per-file content conflicts use a mini diff3 built on Myers: align both sides' edit scripts by base-line position into hunks, emit `<<<<<<< HEAD` / `=======` / `>>>>>>> <branch>` markers on real collisions. On conflict, write `.vcs/MERGE_HEAD` so a later `commit` produces a correct two-parent merge commit.

## CLI

No argument-parsing library — `cli/main.cpp` dispatches `argv[1]` through a `{name -> handler}` table (`command_registry.cpp`), with a small shared `ArgParser` helper for common flag/positional patterns.

| Command | Behavior |
|---|---|
| `vcs init [path]` | Create `.vcs/` skeleton, HEAD → `refs/heads/main`, empty index. |
| `vcs add <path>...` | Hash+compress+store blob(s), update index (recurse into dirs). |
| `vcs commit -m "<msg>"` | Build tree(s) bottom-up from index, write commit object (2 parents if `MERGE_HEAD` present, then clear it), update branch ref. |
| `vcs log` | Walk first-parent chain from HEAD, print hash/author/date/message. |
| `vcs status` | index-vs-HEAD (staged) + workdir-vs-index (unstaged/untracked) via tree_diff + filesystem scan. |
| `vcs diff [--staged]` | tree_diff + per-file Myers rendered as unified diff. |
| `vcs branch [name]` | List (marking current) or create a branch ref at current HEAD. |
| `vcs checkout <branch\|commit>` | Update HEAD, apply target tree to working dir + index via tree_diff against current tree. |
| `vcs merge <branch>` | LCA + three-way merge; clean → auto merge-commit; conflicts → write markers, set `MERGE_HEAD`, nonzero exit. |
| `vcs hash-object <file>` / `vcs cat-file <hash>` | Plumbing/debug commands, added early (M2) to manually verify the object store at every milestone. |

## Build & test setup

- **C++20**, `-Wall -Wextra -Wpedantic -Werror`; Debug builds add `-fsanitize=address,undefined`.
- `std::span` for byte-buffer APIs (hash/compress), `<filesystem>` for working-tree walking (`add`/`checkout`/`status`).
- **Testing**: a custom lightweight harness (`tests/test_harness.hpp`: `VCS_TEST(name){...}` + `VCS_ASSERT_EQ/TRUE` macros), not GoogleTest/Catch2 — avoids a `FetchContent` network dependency and stays in the spirit of "implement it yourself," since the harness itself is simple/low-risk unlike SHA-256/DEFLATE where hand-rolling is the actual point. `enable_testing()` + `add_test(...)` wires it into `ctest`.

## Milestones (each runnable and manually verifiable before the next)

1. **M0 — Restructure**: split the current single-file CMakeLists into `vcs_core`/`vcs`/`vcs_tests`, create the directory skeleton, verify an empty build still runs; turn on warnings-as-errors + Debug sanitizers.
2. **M1 — SHA-256**: implement + test against NIST vectors and streaming consistency.
3. **M2 — Object model + loose storage (uncompressed) + `init`/`hash-object`/`cat-file`**: blob/tree/commit serialization, sharded object store (raw bytes; compression deferred). Manually verify by hashing/storing/reading real files from the shell.
4. **M3 — Compression**: LZ77 + Huffman + round-trip tests, then wire into the object store (compress on write, decompress on read).
5. **M4 — Index + `add`/`commit`/`log`**: text index format, tree construction from the index, commit objects + parent chain, branch ref update. First full `init → add → commit → log` loop.
6. **M5 — `status` + Myers diff + `diff`**: tree_diff, Myers, unified-diff rendering.
7. **M6 — Branches/refs + `checkout`**: `branch` list/create, `checkout` switching HEAD + working dir + index via tree_diff.
8. **M7 — Three-way merge**: LCA (with a hand-built commit-graph test fixture), tree-level classification, mini diff3 on Myers, conflict markers, `MERGE_HEAD`, merge-commit creation on `commit`.

Exercise each milestone's new commands manually from the shell against real files (comparing to real `git` output where useful) before moving on — watching `.vcs/` evolve on disk is part of the point, not just passing automated tests.

## Verification

- `ctest` (via `vcs_tests`) after each milestone — SHA-256 vectors, compression round-trips, diff/merge algorithm unit tests must pass.
- Manual end-to-end smoke test per milestone: run the actual `vcs` CLI commands added in that milestone against a scratch directory with real files, inspect `.vcs/` contents directly (`cat-file`/`hash-object`, hex dump of loose objects), and where relevant diff behavior against real `git` on the same inputs.
- After M7, a full walkthrough: init a repo, create a branch, make diverging edits (including one intentional conflicting edit) on both branches, and confirm `merge` produces a correct auto-merged result plus correctly formatted conflict markers on the colliding file.

### Critical files
- `/Users/abhijitvariyar/Projects/cpp/vcs/CMakeLists.txt`
- `/Users/abhijitvariyar/Projects/cpp/vcs/src/hash/sha256.cpp`
- `/Users/abhijitvariyar/Projects/cpp/vcs/src/compress/deflate.cpp`
- `/Users/abhijitvariyar/Projects/cpp/vcs/src/objects/object_store.cpp`
- `/Users/abhijitvariyar/Projects/cpp/vcs/src/merge/three_way.cpp`
