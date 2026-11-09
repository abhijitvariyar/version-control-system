#include "sample_inputs.hpp"

#include <memory>

#include "vcs/compress/deflate.hpp"
#include "vcs/objects/commit.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/tree.hpp"

namespace {

vcs::ByteStream roundtrip(const vcs::ByteStream &input) {
  return vcs::decompress(vcs::compress(input));
}

// The fixed header: 4 magic bytes, 1 mode byte, 8 size bytes (low bytes first)
constexpr size_t HEADER_SIZE = 13;
constexpr size_t MODE_OFFSET = 4;
constexpr size_t SIZE_OFFSET = 5;
constexpr uint8_t MODE_STORED = 0;
constexpr uint8_t MODE_COMPRESSED = 1;

void set_size(vcs::ByteStream &data, uint64_t size) {
  for (size_t i = 0; i < 8; i++)
    data[SIZE_OFFSET + i] = static_cast<uint8_t>(size >> (8 * i));
}

} // namespace

TEST(deflate, roundtrip_sample_inputs) {
  for (const auto &[name, input] : test::sample_inputs()) {
    vcs::ByteStream output = roundtrip(input);
    if (output != input)
      test::fail(__FILE__, __LINE__, "round-trip mismatch for input: " + name);
  }
}

TEST(deflate, roundtrip_blob_object) {
  vcs::ByteStream blob = vcs::wrap(vcs::ObjectType::Blob, test::source_file());
  CHECK(roundtrip(blob) == blob);
}

TEST(deflate, roundtrip_tree_object) {
  auto root = std::make_unique<vcs::TreeNode>(vcs::TreeNodeType::Dir, "");
  for (std::string path : {"README.md", "src/a.cpp", "src/b.cpp", "include/a.hpp"}) {
    vcs::FilePath p(path);
    root->insert(p, vcs::ByteStream(32, static_cast<uint8_t>(path.size())));
  }
  vcs::Tree tree(std::move(root));

  CHECK_EQ(roundtrip(tree.get_payload()), tree.get_payload());
}

TEST(deflate, roundtrip_commit_object) {
  vcs::Commit commit(vcs::ByteStream(32, 0x11), {vcs::ByteStream(32, 0x22)},
                     "Ada <ada@example.com> 1700000000 +0000",
                     "Ada <ada@example.com> 1700000000 +0000", "message\n");
  CHECK_EQ(roundtrip(commit.get_payload()), commit.get_payload());
}

TEST(deflate, repetitive_text_compresses_well) {
  vcs::ByteStream input = test::repetitive_text(100 * 1024);
  size_t compressed = vcs::compress(input).size();
  CHECK(compressed < input.size() / 10);
}

TEST(deflate, source_code_shrinks) {
  vcs::ByteStream input = test::source_file();
  CHECK(vcs::compress(input).size() < input.size());
}

TEST(deflate, short_matches_are_not_worth_their_cost) {
  // 16 letters in random order: each costs 4 bits as a literal, and repeats
  // are short and common. Writing them as matches costs far more than that.
  vcs::ByteStream input = test::random_bytes(64 * 1024);
  for (uint8_t &byte : input)
    byte = static_cast<uint8_t>('a' + (byte & 0x0F));

  size_t compressed = vcs::compress(input).size();
  CHECK(compressed < input.size() * 55 / 100);
}

TEST(deflate, random_data_bounded_growth) {
  vcs::ByteStream input = test::random_bytes(100 * 1024);
  size_t limit = input.size() + input.size() / 100 + 300;
  size_t compressed = vcs::compress(input).size();
  if (compressed > limit)
    test::fail(__FILE__, __LINE__,
               "compressed " + std::to_string(input.size()) + " random bytes to " +
                   std::to_string(compressed) + " (limit " +
                   std::to_string(limit) + ")");
}

TEST(deflate, deterministic) {
  vcs::ByteStream input = test::source_file();
  CHECK(vcs::compress(input) == vcs::compress(input));
}

// -------- corrupt input --------

TEST(deflate, decompress_empty_throws) {
  CHECK_THROWS(vcs::decompress({}));
}

TEST(deflate, decompress_bad_magic_throws) {
  vcs::ByteStream data = vcs::compress(test::bytes("hello hello hello"));
  CHECK(!data.empty());
  if (data.empty())
    return;
  data[0] ^= 0xFF;
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate, decompress_garbage_throws) {
  CHECK_THROWS(vcs::decompress(test::bytes("definitely not a compressed stream")));
}

TEST(deflate, decompress_every_truncation_throws) {
  vcs::ByteStream data = vcs::compress(test::repetitive_text(2000));

  for (size_t cut = 0; cut < data.size(); cut++) {
    vcs::ByteStream truncated(data.begin(), data.begin() + cut);
    bool threw = false;
    try {
      vcs::decompress(truncated);
    } catch (...) {
      threw = true;
    }
    if (!threw) {
      test::fail(__FILE__, __LINE__,
                 "no throw when truncated to " + std::to_string(cut) + " of " +
                     std::to_string(data.size()) + " bytes");
      return; // one report is enough
    }
  }
}

// -------- stored mode --------

TEST(deflate, tiny_input_is_stored) {
  vcs::ByteStream input = test::bytes("hello");
  vcs::ByteStream data = vcs::compress(input);
  CHECK_EQ(data.size(), input.size() + HEADER_SIZE);
  CHECK_EQ(data.at(MODE_OFFSET), MODE_STORED);
  CHECK_EQ(vcs::decompress(data), input);
}

TEST(deflate, empty_input_is_header_only) {
  CHECK_EQ(vcs::compress({}).size(), HEADER_SIZE);
}

TEST(deflate, random_data_grows_by_header_at_most) {
  vcs::ByteStream input = test::random_bytes(100 * 1024);
  CHECK(vcs::compress(input).size() <= input.size() + HEADER_SIZE);
}

TEST(deflate, repetitive_input_uses_compressed_mode) {
  vcs::ByteStream data = vcs::compress(test::repetitive_text(10000));
  CHECK_EQ(data.at(MODE_OFFSET), MODE_COMPRESSED);
}

// -------- hand-corrupted streams --------
// Their own suite: if one crashes under the sanitizers, it can't hide the
// results of the deflate suite.

TEST(deflate_corrupt, unknown_mode_throws) {
  vcs::ByteStream data = vcs::compress(test::bytes("hello"));
  data.at(MODE_OFFSET) = 7;
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate_corrupt, stored_size_too_big_throws) {
  vcs::ByteStream data = vcs::compress(test::bytes("hello"));
  set_size(data, 6);
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate_corrupt, stored_size_too_small_throws) {
  vcs::ByteStream data = vcs::compress(test::bytes("hello"));
  set_size(data, 4);
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate_corrupt, huge_stated_size_throws) {
  vcs::ByteStream stored = vcs::compress(test::bytes("hello"));
  set_size(stored, UINT64_MAX);
  CHECK_THROWS(vcs::decompress(stored));

  vcs::ByteStream compressed = vcs::compress(test::repetitive_text(10000));
  CHECK_EQ(compressed.at(MODE_OFFSET), MODE_COMPRESSED);
  set_size(compressed, UINT64_MAX);
  CHECK_THROWS(vcs::decompress(compressed));
}

TEST(deflate_corrupt, stated_size_inside_a_match_throws) {
  vcs::ByteStream data = vcs::compress(vcs::ByteStream(10000, 'a'));
  CHECK_EQ(data.at(MODE_OFFSET), MODE_COMPRESSED);
  set_size(data, 9999);
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate_corrupt, oversubscribed_code_lengths_throw) {
  // every symbol claims a 1-bit code, which only two symbols can have
  vcs::ByteStream data = vcs::compress(test::repetitive_text(10000));
  CHECK_EQ(data.at(MODE_OFFSET), MODE_COMPRESSED);
  for (size_t i = HEADER_SIZE; i < HEADER_SIZE + 129; i++)
    data.at(i) = 0x11;
  CHECK_THROWS(vcs::decompress(data));
}

TEST(deflate_corrupt, flipped_bits_never_crash) {
  // A flip may still decode to something, or throw. What it must not do is
  // trip the sanitizers or hang.
  vcs::ByteStream data = vcs::compress(test::repetitive_text(2000));

  for (size_t bit = 0; bit < data.size() * 8; bit++) {
    vcs::ByteStream corrupt = data;
    corrupt[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));

    try {
      vcs::decompress(corrupt);
    } catch (const std::exception &) {
    }
  }
}
