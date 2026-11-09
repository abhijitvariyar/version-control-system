#include "sample_inputs.hpp"

#include <algorithm>

#include "vcs/compress/lz77.hpp"

namespace {

vcs::LZ77Token lit(char c) {
  return {false, static_cast<uint8_t>(c), 0, 0};
}

vcs::LZ77Token match(uint16_t length, uint16_t distance) {
  return {true, 0, length, distance};
}

// Returns an empty string if every token is within limits, otherwise a
// description of the first bad token
std::string first_invalid_token(const vcs::TokenStream &tokens) {
  size_t produced = 0;
  for (size_t i = 0; i < tokens.size(); i++) {
    const vcs::LZ77Token &t = tokens[i];
    if (!t.is_match) {
      produced++;
      continue;
    }

    size_t max_distance = std::min<size_t>(produced, 32768);
    if (t.length < 3 || t.length > 258 || t.distance < 1 ||
        t.distance > max_distance)
      return "token " + std::to_string(i) + ": length=" +
             std::to_string(t.length) + " distance=" +
             std::to_string(t.distance) + " produced=" +
             std::to_string(produced);

    produced += t.length;
  }
  return "";
}

} // namespace

TEST(lz77, roundtrip_sample_inputs) {
  for (const auto &[name, input] : test::sample_inputs()) {
    vcs::TokenStream tokens = vcs::lz77_encode(input);
    vcs::ByteStream output = vcs::lz77_decode(tokens);
    if (output != input)
      test::fail(__FILE__, __LINE__, "round-trip mismatch for input: " + name);
  }
}

TEST(lz77, tokens_within_limits) {
  for (const auto &[name, input] : test::sample_inputs()) {
    std::string bad = first_invalid_token(vcs::lz77_encode(input));
    if (!bad.empty())
      test::fail(__FILE__, __LINE__, name + ": " + bad);
  }
}

TEST(lz77, empty_input_no_tokens) {
  CHECK(vcs::lz77_encode({}).empty());
  CHECK(vcs::lz77_decode({}).empty());
}

TEST(lz77, short_input_is_all_literals) {
  // nothing earlier to match against, and matches need length >= 3
  vcs::TokenStream tokens = vcs::lz77_encode(test::bytes("abc"));
  CHECK_EQ(tokens.size(), size_t(3));
  for (const vcs::LZ77Token &t : tokens)
    CHECK(!t.is_match);
}

TEST(lz77, repetitive_input_compresses) {
  vcs::TokenStream tokens = vcs::lz77_encode(vcs::ByteStream(10000, 'a'));
  CHECK(tokens.size() < 100);
}

TEST(lz77, repeated_phrase_uses_matches) {
  vcs::TokenStream tokens = vcs::lz77_encode(test::repetitive_text(4096));
  size_t matches = std::count_if(tokens.begin(), tokens.end(),
                                 [](const vcs::LZ77Token &t) { return t.is_match; });
  CHECK(matches > 0);
  CHECK(tokens.size() < 4096 / 4);
}

TEST(lz77, never_matches_beyond_window) {
  // the only repeat is 40000 bytes back, outside the 32 KB window
  vcs::ByteStream block = test::random_bytes(40000, 0xC0FFEE);
  vcs::ByteStream input = block;
  input.insert(input.end(), block.begin(), block.end());

  vcs::TokenStream tokens = vcs::lz77_encode(input);
  CHECK_EQ(first_invalid_token(tokens), std::string(""));
  CHECK(vcs::lz77_decode(tokens) == input);
}

// -------- decode on hand-built tokens --------

TEST(lz77, decode_literals) {
  CHECK_EQ(vcs::lz77_decode({lit('h'), lit('i')}), test::bytes("hi"));
}

TEST(lz77, decode_simple_match) {
  // "abcd" then copy 4 bytes from 4 back
  CHECK_EQ(vcs::lz77_decode({lit('a'), lit('b'), lit('c'), lit('d'), match(4, 4)}),
           test::bytes("abcdabcd"));
}

TEST(lz77, decode_overlapping_match) {
  // distance < length: the copy reads bytes it is itself producing
  CHECK_EQ(vcs::lz77_decode({lit('a'), lit('b'), lit('c'), match(9, 3)}),
           test::bytes("abcabcabcabc"));
}

TEST(lz77, decode_run_length_match) {
  CHECK_EQ(vcs::lz77_decode({lit('z'), match(5, 1)}), test::bytes("zzzzzz"));
}

TEST(lz77, decode_distance_too_far_throws) {
  CHECK_THROWS(vcs::lz77_decode({lit('a'), match(3, 2)}));
}

TEST(lz77, decode_match_at_start_throws) {
  CHECK_THROWS(vcs::lz77_decode({match(3, 1)}));
}

TEST(lz77, decode_zero_distance_throws) {
  CHECK_THROWS(vcs::lz77_decode({lit('a'), match(3, 0)}));
}
