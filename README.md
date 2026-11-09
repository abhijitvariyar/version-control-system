# vcs

A version control system built from scratch in C++20 — as a learning project.

## Goals for this project

1. Learn the basics of how a version control system actually works internally.
2. Have my own system, built with my own commands and policies.

In service of that, nothing core is pulled from a library: hashing and compression are
hand-implemented rather than linked from OpenSSL/zlib. The parts that aren't the point of
the exercise (CMake, the test harness) stay simple.

## Status

This project is built incrementally in milestones, each one a runnable, manually-verifiable
step before moving to the next.

| Milestone | Description | Status |
|---|---|---|
| M0 | Project restructure, CMake skeleton, warnings-as-errors + sanitizers | ✅ Done |
| M1 | SHA-256 from scratch | ✅ Done |
| M2 | Object model (blob/tree/commit), loose object storage, `init`/`hash-object`/`cat-file` | ✅ Done |
| M3 | Compression (LZ77 + Huffman), wired into object storage | ✅ Done |
| M4 | Staging index, `add`/`commit`/`log` | ✅ Done |
| M5 | `status`, Myers diff, `diff` | ✅ Done |
| M6 | Branches/refs, `checkout` | ⬜ Not started |
| M7 | Three-way merge | ⬜ Not started |

### What works right now

- **SHA-256**, hand-implemented per FIPS 180-4, hashing arbitrary in-memory byte buffers.
  Verified against NIST test vectors (empty string, `"abc"`, a two-block boundary case, and
  a 1,000,000×`'a'` multi-block case).
- **Git-style content-addressable objects** — blobs, trees, and commits — serialized with a
  `"<type> <size>\0<content>"` header, hashed as a whole buffer, and stored as loose objects
  sharded by hash (`.vcs/objects/<2-hex>/<62-hex>`), the same layout real git uses.
- **Tree objects** support building a real nested directory structure (`TreeNode::insert`),
  recursive bottom-up hashing of the whole subtree, git's sort order (directories sorted as
  if their name ended in `/`), and duplicate-entry rejection. Verified to produce byte-identical
  hashes to a real SHA-256 git repository (`git init --object-format=sha256`) on the same
  file content, including the directory/file name-collision edge case.
- **Commit objects** capture a tree hash, 0–2 parent hashes, author/committer, and a message.
- **A working CLI**: `init`, `config`, `add`, `commit`, `log`, plus `hash-object` and `cat-file`
  for working with objects directly.
- **Staging and commits**: `add` stores files as blobs and records them in the index, including
  deletions. `commit` writes a tree object for every directory, then the commit, then moves the
  branch. Tree and commit hashes match real git (`--object-format=sha256`) for the same content,
  author and date.
- **History**: `log` walks the first-parent chain from HEAD, newest first.
- **`status`**: staged changes (HEAD against the index), unstaged changes (the index against the
  working files) and untracked files, with a directory that has nothing tracked in it shown once.
  It compares the index's tree in memory, so it never writes objects.
- **`diff` and `diff --staged`**: unified diffs in git's format, which `git apply` accepts. Myers'
  algorithm in its linear-memory form always finds a shortest edit script, and lines that appear
  in only one of the two files are set aside before the search, so a rewritten file is quick.
  Binary files (a NUL in the first 8000 bytes) are reported, not diffed.
- **Tree diff**: compares two trees and skips any directory with the same hash on both sides.
- **Ignore rules**: a `.vcsignore` file at the repository root keeps files out of `vcs add <dir>`.
  Files that are already tracked stay tracked, and naming an ignored file directly still adds it.
- **Corruption detection**: `ObjectStore::read` checks every object against its hash, so a damaged
  object is reported instead of being returned.
- **Malformed input is rejected**: `unwrap` throws on truncated or invalid object headers, and
  the object store only accepts 64-character lowercase hex hashes, so a crafted hash can't
  point outside `.vcs/objects`.
- **Bit stream** (`BitWriter`/`BitReader`), the first piece of M3: reads and writes 1–32 bits at
  a time, packed LSB-first.
- **LZ77** (`lz77_encode`/`lz77_decode`): turns bytes into literal and match tokens using a 32 KB
  window, matches of 3–258 bytes, and a hash-chain match finder.
- **Huffman coding** (`huffman_code_lengths`/`canonical_codes`): turns symbol frequencies into code
  lengths capped at 15 bits, then builds canonical codes from those lengths.
- **Compressed object storage** (`compress`/`decompress`): LZ77 tokens are Huffman coded using one
  257-symbol alphabet, and `ObjectStore` compresses on write and decompresses on read. The hash is
  always of the uncompressed buffer, so hashes still match real git. Anything that wouldn't shrink,
  such as a tiny object or random data, is stored as it is behind a 13-byte header. Matches shorter
  than 5 bytes are written as literals, because the fixed-width match fields cost more than they
  save.
- **An automated test suite** covering all of the above, including end-to-end runs of the CLI.

### Known limitations (by design, for now)

- Objects written before compression was added can't be read. Re-create those repositories.
- `.vcsignore` is a subset of `.gitignore`: one file at the repository root, no `!` negation, and
  `**` behaves like `*`. Nothing is ignored by default except `.vcs`, so a project that also uses
  git needs `.git/` in its `.vcsignore`.
- Every file is recorded as mode `100644`, so the executable bit isn't kept. Symlinks aren't
  supported: `add` refuses one by name and skips them inside a directory.
- Each repository needs its own `user.name` and `user.email`; there's no global config.
- `status` and `diff` hash every tracked file to find changes, since the index doesn't cache file
  sizes and times the way git's does. Their paths are relative to the repository root, not the
  current directory, and neither takes paths to narrow it down.
- `diff` doesn't detect renames (a rename is a deletion plus an addition), and its `@@` lines
  leave out the nearby line git adds after them. Both are minimal, but where several equally
  short diffs exist, git's clean-up heuristics can pick a different one.
- `TreeNode::insert` validates against absolute paths but doesn't yet reject `.`/`..`
  components. The index does reject them, so paths from `add` are safe.

## Building

Standard out-of-tree CMake build:

```sh
cmake -S . -B bin -DCMAKE_BUILD_TYPE=Debug
cmake --build bin
```

Debug builds compile with `-Wall -Wextra -Wpedantic -Werror` and AddressSanitizer +
UndefinedBehaviorSanitizer enabled. The resulting binary is `bin/src/vcs`.

To generate a compile database for editor tooling (clangd, VS Code IntelliSense):

```sh
cmake -S . -B bin -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Testing

```sh
ctest --test-dir bin --output-on-failure   # every suite
bin/tests/vcs_tests tree                   # one suite
bin/tests/vcs_tests tree.parse_roundtrip   # one test
```

The tests use a small hand-rolled harness (`tests/test_harness.hpp`) with no dependencies.

## Usage

```sh
$ vcs init
Initialized empty vcs repository in /path/to/project/.vcs

$ echo "hello" > README.md
$ vcs hash-object README.md
2cf8d83d9ee29543b34a87727421fdecb7e3f3a183d337639025de576db9ebb4

$ vcs cat-file 2cf8d83d9ee29543b34a87727421fdecb7e3f3a183d337639025de576db9ebb4
hello

$ vcs config user.name "Ada Lovelace"
$ vcs config user.email ada@example.com

$ vcs add README.md
$ vcs commit -m "first commit"
[1a2b3c4] first commit

$ vcs log
commit 1a2b3c4...
Author: Ada Lovelace <ada@example.com>
Date:   Tue Nov 14 22:13:20 2023 +0000

    first commit
```

`vcs add` takes files and directories, and a tracked file that no longer exists is staged as a
deletion. To keep files out of `vcs add .`, list them in `.vcsignore`:

```
# Comments go on their own line
# A trailing slash matches directories only
bin/
# * and ? match within one path component
*.o
# A plain name matches at any depth
build.log
# A leading or middle slash anchors to the repository root
/notes.txt
```

`vcs status` shows what's staged, what isn't, and what's untracked. `vcs diff` shows unstaged
changes line by line, and `vcs diff --staged` (or `--cached`) shows what the next commit would
change:

```sh
$ vcs status
On branch main

Changes to be committed:
	modified:   README.md

Untracked files:
	notes/

$ vcs diff --staged
diff --git a/README.md b/README.md
--- a/README.md
+++ b/README.md
@@ -1 +1,2 @@
 hello
+world
```

`cat-file` also decodes trees (printed `ls-tree`-style) and commits (printed as their
raw text).

## On-disk repository format

```
.vcs/
  HEAD                       # "ref: refs/heads/main\n"
  config                     # key=value lines, such as user.name=Ada
  index                      # staging area: "<mode> <blob-hash> <path>" per line, sorted by path
  objects/<2-hex>/<62-hex>    # compressed loose objects, sharded by the first byte of the SHA-256 hash
  refs/heads/<branch-name>    # the branch's latest commit hash
```

### Compressed file format

Each object file starts with a 13-byte header: the magic bytes `VCZ1`, a mode byte, and the
uncompressed size as 8 bytes, low byte first. Mode 0 is followed by the raw object. Mode 1 is
followed by 257 four-bit code lengths and then the Huffman coded tokens. Symbols 0–255 are literal
bytes; symbol 256 is a match, followed by an 8-bit (length − 3) and a 15-bit (distance − 1).

### Object formats

Every object is a header followed by its content, and the SHA-256 hash is computed over the
**entire** buffer — header included:

- **Blob**: `"blob " + <decimal size> + '\0' + <raw file bytes>`
- **Tree**: `"tree " + <decimal size> + '\0' + <entries>`, where entries are sorted by name
  (directories compared as if their name ended in `/`) and each entry is
  `<mode> ' ' <name> '\0' <32-byte raw hash>`. Modes: `100644` (file), `40000` (directory).
- **Commit**: `"commit " + <decimal size> + '\0' + "tree <hex>\n" + ("parent <hex>\n")×0-2 + "author ...\ncommitter ...\n\n<message>"`

## Project layout

```
vcs/
  CMakeLists.txt
  include/vcs/
    hash/sha256.hpp
    compress/{bitstream,lz77,huffman,deflate}.hpp
    diff/{myers,unified,tree_diff}.hpp
    objects/{object,blob,tree,commit,object_store}.hpp
    repo/{ignore,index,refs,repository}.hpp
    util/{bytes,file}.hpp
  src/
    CMakeLists.txt
    hash/sha256.cpp
    compress/{bitstream,lz77,huffman,deflate}.cpp
    diff/{myers,unified,tree_diff}.cpp
    objects/{object,blob,tree,commit,object_store}.cpp
    repo/{ignore,index,refs,repository}.cpp
    cli/main.cpp
  tests/
    CMakeLists.txt
    test_harness.hpp  test_main.cpp
    hash/  objects/  compress/  diff/  repo/  cli/
```

`vcs_core` is a static library holding everything except the CLI; `vcs` and `vcs_tests` are the
executables that link it. This split keeps every algorithm testable independently of the binary.

## Design notes

- **C++20**, no third-party dependencies.
- `std::span`/`std::string_view` for byte-buffer APIs, `std::filesystem` for path handling.
- All project code lives in the `vcs::` namespace.
- Hashing and compression are implemented from scratch on purpose — that's the point of the
  project, not an area where a library would be "cheating" but also wouldn't teach anything.
