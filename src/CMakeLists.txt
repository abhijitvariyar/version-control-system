add_library(vcs_core STATIC
  hash/sha256.cpp
  objects/object.cpp
  objects/blob.cpp
  objects/tree.cpp
  objects/commit.cpp
  objects/object_store.cpp
  compress/bitstream.cpp
  compress/lz77.cpp
  compress/huffman.cpp
  compress/deflate.cpp
  diff/myers.cpp
  diff/unified.cpp
  diff/tree_diff.cpp
  repo/ignore.cpp
  repo/index.cpp
  repo/refs.cpp
  repo/repository.cpp
)

target_include_directories(vcs_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(vcs cli/main.cpp)
target_link_libraries(vcs PRIVATE vcs_core)
