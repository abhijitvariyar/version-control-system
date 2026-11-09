#pragma once

#include <cstdint>
#include <vector>

namespace vcs {

// One code length per symbol, 0 for a symbol with frequency 0. Lengths are
// capped at 15 bits.
std::vector<uint8_t> huffman_code_lengths(const std::vector<uint64_t> &freqs);

// Canonical codes built from code lengths, one per symbol. A code is a plain
// value whose top bit is the first bit of the code; unused symbols get 0.
std::vector<uint32_t> canonical_codes(const std::vector<uint8_t> &lengths);

} // namespace vcs
