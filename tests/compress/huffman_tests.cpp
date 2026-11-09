#include "../test_harness.hpp"

#include "vcs/compress/huffman.hpp"

namespace {

// Kraft sum scaled by 2^15: a complete prefix code sums to exactly 1 << 15
uint64_t kraft_sum(const std::vector<uint8_t> &lengths) {
  uint64_t sum = 0;
  for (uint8_t len : lengths)
    if (len > 0 && len <= 15)
      sum += uint64_t(1) << (15 - len);
  return sum;
}

size_t used_symbols(const std::vector<uint8_t> &lengths) {
  size_t n = 0;
  for (uint8_t len : lengths)
    if (len > 0)
      n++;
  return n;
}

// Every symbol with length > 0 has a code that is not a prefix of another
bool prefix_free(const std::vector<uint8_t> &lengths,
                 const std::vector<uint32_t> &codes) {
  for (size_t i = 0; i < lengths.size(); i++) {
    for (size_t j = 0; j < lengths.size(); j++) {
      if (i == j || lengths[i] == 0 || lengths[j] == 0 ||
          lengths[i] > lengths[j])
        continue;
      if ((codes[j] >> (lengths[j] - lengths[i])) == codes[i])
        return false;
    }
  }
  return true;
}

} // namespace

// -------- code lengths --------

TEST(huffman, two_symbols_get_one_bit) {
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths({10, 3});
  CHECK_EQ(lengths, (std::vector<uint8_t>{1, 1}));
}

TEST(huffman, single_symbol_gets_one_bit) {
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths({0, 0, 42, 0});
  CHECK_EQ(lengths, (std::vector<uint8_t>{0, 0, 1, 0}));
}

TEST(huffman, all_zero_freqs_give_all_zero_lengths) {
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths({0, 0, 0});
  CHECK_EQ(lengths, (std::vector<uint8_t>{0, 0, 0}));
}

TEST(huffman, empty_input_gives_empty_output) {
  CHECK(vcs::huffman_code_lengths({}).empty());
  CHECK(vcs::canonical_codes({}).empty());
}

TEST(huffman, output_size_matches_input) {
  std::vector<uint64_t> freqs(257, 0);
  freqs['a'] = 5;
  freqs['b'] = 7;
  freqs[256] = 1;
  CHECK_EQ(vcs::huffman_code_lengths(freqs).size(), size_t(257));
}

TEST(huffman, unused_symbols_get_zero) {
  std::vector<uint64_t> freqs = {5, 0, 3, 0, 9, 0};
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths(freqs);
  CHECK_EQ(lengths.at(1), uint8_t(0));
  CHECK_EQ(lengths.at(3), uint8_t(0));
  CHECK_EQ(lengths.at(5), uint8_t(0));
  CHECK_EQ(used_symbols(lengths), size_t(3));
}

TEST(huffman, known_lengths) {
  // classic example: freqs 45,13,12,16,9,5 -> lengths 1,3,3,3,4,4
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths({45, 13, 12, 16, 9, 5});
  CHECK_EQ(lengths, (std::vector<uint8_t>{1, 3, 3, 3, 4, 4}));
}

TEST(huffman, kraft_equality) {
  std::vector<std::vector<uint64_t>> cases = {
      {1, 1},
      {1, 1, 1},
      {45, 13, 12, 16, 9, 5},
      {1, 2, 4, 8, 16, 32, 64, 128},
  };

  std::vector<uint64_t> bytes_freq(256);
  for (size_t i = 0; i < 256; i++)
    bytes_freq[i] = (i * 7919) % 101 + 1;
  cases.push_back(bytes_freq);

  for (const auto &freqs : cases)
    CHECK_EQ(kraft_sum(vcs::huffman_code_lengths(freqs)), uint64_t(1) << 15);
}

TEST(huffman, more_frequent_never_longer) {
  std::vector<uint64_t> freqs = {100, 1, 50, 3, 25, 7, 12, 2, 6, 1};
  std::vector<uint8_t> lengths = vcs::huffman_code_lengths(freqs);

  for (size_t i = 0; i < freqs.size(); i++)
    for (size_t j = 0; j < freqs.size(); j++)
      if (freqs[i] > freqs[j] && lengths[i] > lengths[j])
        test::fail(__FILE__, __LINE__,
                   "symbol " + std::to_string(i) + " (freq " +
                       std::to_string(freqs[i]) + ") is longer than symbol " +
                       std::to_string(j) + " (freq " +
                       std::to_string(freqs[j]) + ")");
}

TEST(huffman, lengths_capped_at_15) {
  // Fibonacci frequencies build a maximally skewed tree (depth n-1 unlimited)
  std::vector<uint64_t> freqs = {1, 1};
  while (freqs.size() < 30)
    freqs.push_back(freqs[freqs.size() - 1] + freqs[freqs.size() - 2]);

  std::vector<uint8_t> lengths = vcs::huffman_code_lengths(freqs);
  for (uint8_t len : lengths) {
    CHECK(len >= 1);
    CHECK(len <= 15);
  }
  // still a valid (decodable) prefix code
  CHECK(kraft_sum(lengths) <= (uint64_t(1) << 15));
}

// -------- canonical codes --------

TEST(huffman, canonical_rfc1951_example) {
  // RFC 1951 section 3.2.2: symbols A..H
  std::vector<uint8_t> lengths = {3, 3, 3, 3, 3, 2, 4, 4};
  std::vector<uint32_t> codes = vcs::canonical_codes(lengths);
  CHECK_EQ(codes, (std::vector<uint32_t>{0b010, 0b011, 0b100, 0b101, 0b110,
                                         0b00, 0b1110, 0b1111}));
}

TEST(huffman, canonical_skips_unused_symbols) {
  std::vector<uint8_t> lengths = {2, 0, 1, 0, 2};
  std::vector<uint32_t> codes = vcs::canonical_codes(lengths);
  CHECK_EQ(codes.size(), size_t(5));
  CHECK_EQ(codes[2], uint32_t(0b0));
  CHECK_EQ(codes[0], uint32_t(0b10));
  CHECK_EQ(codes[4], uint32_t(0b11));
}

TEST(huffman, canonical_codes_prefix_free) {
  std::vector<uint64_t> freqs(256);
  for (size_t i = 0; i < 256; i++)
    freqs[i] = (i * 31) % 97 + (i % 5 == 0 ? 0 : 1);

  std::vector<uint8_t> lengths = vcs::huffman_code_lengths(freqs);
  std::vector<uint32_t> codes = vcs::canonical_codes(lengths);
  CHECK(prefix_free(lengths, codes));
}
