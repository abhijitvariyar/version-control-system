#pragma once

// Inputs shared by the lz77 and deflate round-trip tests

#include <string>
#include <utility>
#include <vector>

#include "../test_harness.hpp"

namespace test {

// Deterministic pseudo-random bytes (xorshift32), so failures are reproducible
inline vcs::ByteStream random_bytes(size_t n, uint32_t seed = 0x12345678) {
  vcs::ByteStream out(n);
  uint32_t x = seed;
  for (size_t i = 0; i < n; i++) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    out[i] = static_cast<uint8_t>(x);
  }
  return out;
}

inline vcs::ByteStream repetitive_text(size_t n) {
  const std::string line = "the quick brown fox jumps over the lazy dog\n";
  vcs::ByteStream out;
  out.reserve(n);
  while (out.size() < n)
    out.push_back(static_cast<uint8_t>(line[out.size() % line.size()]));
  return out;
}

inline vcs::ByteStream source_file() {
  // VCS_SOURCE_DIR is defined by tests/CMakeLists.txt
  return bytes(read_text(std::filesystem::path(VCS_SOURCE_DIR) / "src" /
                         "hash" / "sha256.cpp"));
}

inline std::vector<std::pair<std::string, vcs::ByteStream>> sample_inputs() {
  vcs::ByteStream all_bytes;
  for (int i = 0; i < 256; i++)
    all_bytes.push_back(static_cast<uint8_t>(i));

  return {
      {"empty", {}},
      {"one_byte", bytes("x")},
      {"abc", bytes("abc")},
      {"ten_thousand_a", vcs::ByteStream(10000, 'a')},
      {"all_byte_values", all_bytes},
      {"random_100k", random_bytes(100 * 1024)},
      {"repetitive_text_100k", repetitive_text(100 * 1024)},
      {"sha256_source", source_file()},
  };
}

} // namespace test
