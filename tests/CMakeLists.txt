add_executable(vcs_tests
  test_main.cpp
  hash/sha256_tests.cpp
  objects/object_tests.cpp
  objects/blob_tests.cpp
  objects/tree_tests.cpp
  objects/commit_tests.cpp
  objects/object_store_tests.cpp
  compress/bitstream_tests.cpp
  compress/lz77_tests.cpp
  compress/huffman_tests.cpp
  compress/deflate_tests.cpp
  diff/myers_tests.cpp
  diff/unified_tests.cpp
  diff/tree_diff_tests.cpp
  repo/ignore_tests.cpp
  repo/index_tests.cpp
  repo/refs_tests.cpp
  repo/repository_tests.cpp
  repo/checkout_tests.cpp
  cli/cli_tests.cpp
)
target_link_libraries(vcs_tests PRIVATE vcs_core)

# cli tests shell out to the real binary
add_dependencies(vcs_tests vcs)
target_compile_definitions(vcs_tests PRIVATE
  VCS_BIN="$<TARGET_FILE:vcs>"
  VCS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set(TEST_SUITES
  sha256
  object
  object_malformed
  blob
  tree
  commit
  object_store
  bitstream
  lz77
  huffman
  deflate
  deflate_corrupt
  myers
  unified
  tree_diff
  ignore
  index
  refs
  repository
  checkout
  cli
)

# One ctest entry per suite, so a crash in one suite doesn't hide the others.
# Run a single suite directly with: bin/tests/vcs_tests <suite>
foreach(suite ${TEST_SUITES})
  add_test(NAME ${suite} COMMAND vcs_tests ${suite})
endforeach()
