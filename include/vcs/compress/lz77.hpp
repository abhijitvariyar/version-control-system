#pragma once

#include <vector>

#include "../util/bytes.hpp"

namespace vcs {

struct LZ77Token {
  bool is_match;
  uint8_t literal;   // valid when !is_match
  uint16_t length;   // 3..258 when is_match
  uint16_t distance; // 1..32768 when is_match
};

typedef std::vector<LZ77Token> TokenStream;

TokenStream lz77_encode(const ByteStream &input);
ByteStream lz77_decode(const TokenStream &tokens);

} // namespace vcs
