#include "vcs/compress/lz77.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr size_t MIN_MATCH = 3;
constexpr size_t MAX_MATCH = 258;
constexpr size_t WINDOW_SIZE = 32768;

// How many candidates to try per position; bounds the work on inputs like a
// long run of one byte, where every position lands on the same chain
constexpr int MAX_CHAIN = 128;

constexpr size_t HASH_BITS = 15;
constexpr size_t HASH_SIZE = size_t(1) << HASH_BITS;

constexpr int64_t NO_POSITION = -1;

size_t hash_at(const vcs::ByteStream &input, size_t pos) {
  uint32_t key = (uint32_t(input[pos]) << 16) | (uint32_t(input[pos + 1]) << 8) |
                 uint32_t(input[pos + 2]);

  // multiplicative hash, keeping the top HASH_BITS bits
  return (key * 2654435761u) >> (32 - HASH_BITS);
}

// Length of the common run at `candidate` and `pos`. The run may continue past
// `pos` on the candidate side: that is an overlapping match, and it is valid.
size_t match_length(const vcs::ByteStream &input, size_t candidate, size_t pos) {
  size_t limit = std::min(MAX_MATCH, input.size() - pos);

  size_t length = 0;
  while (length < limit && input[candidate + length] == input[pos + length])
    length++;

  return length;
}

} // namespace

vcs::TokenStream vcs::lz77_encode(const ByteStream &input) {
  /*
   * Greedy LZ77 with a hash-chain match finder
   *
   * head[h] is the most recent position whose next 3 bytes hash to h, and
   * prev[p] is the position before p with the same hash. Together they form a
   * newest-to-oldest list of match candidates for each hash.
   * */
  TokenStream tokens;

  std::vector<int64_t> head(HASH_SIZE, NO_POSITION);
  std::vector<int64_t> prev(input.size(), NO_POSITION);

  // the last 2 positions have fewer than 3 bytes left to hash
  auto can_hash = [&](size_t pos) { return pos + MIN_MATCH <= input.size(); };

  auto insert = [&](size_t pos) {
    if (!can_hash(pos))
      return;

    size_t hash = hash_at(input, pos);
    prev[pos] = head[hash];
    head[hash] = static_cast<int64_t>(pos);
  };

  size_t pos = 0;
  while (pos < input.size()) {
    size_t best_length = 0;
    size_t best_distance = 0;

    // searching before inserting pos, so it can't match itself
    if (can_hash(pos)) {
      int64_t candidate = head[hash_at(input, pos)];

      for (int i = 0; i < MAX_CHAIN && candidate != NO_POSITION; i++) {
        size_t candidate_pos = static_cast<size_t>(candidate);
        size_t distance = pos - candidate_pos;

        // chains run newest to oldest, so everything after this is older still
        if (distance > WINDOW_SIZE)
          break;

        size_t length = match_length(input, candidate_pos, pos);
        if (length > best_length) {
          best_length = length;
          best_distance = distance;

          if (length == MAX_MATCH)
            break;
        }

        candidate = prev[candidate_pos];
      }
    }

    if (best_length >= MIN_MATCH) {
      tokens.push_back({true, 0, static_cast<uint16_t>(best_length),
                        static_cast<uint16_t>(best_distance)});

      // every position needs inserting, or later matches into it are missed
      for (size_t i = 0; i < best_length; i++)
        insert(pos + i);

      pos += best_length;
    } else {
      tokens.push_back({false, input[pos], 0, 0});
      insert(pos);
      pos++;
    }
  }

  return tokens;
}

vcs::ByteStream vcs::lz77_decode(const TokenStream &tokens) {
  ByteStream output;

  for (const LZ77Token &token : tokens) {
    if (!token.is_match) {
      output.push_back(token.literal);
      continue;
    }

    if (token.distance == 0 || token.distance > output.size())
      throw std::runtime_error("LZ77 match points before the start of output");

    // one byte at a time: when distance < length, the copy reads bytes it has
    // only just written
    for (size_t i = 0; i < token.length; i++) {
      uint8_t byte = output[output.size() - token.distance];
      output.push_back(byte);
    }
  }

  return output;
}
