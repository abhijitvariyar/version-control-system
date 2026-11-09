#include "vcs/compress/bitstream.hpp"

#include <stdexcept>

/*
 * Both classes walk the same numbered row of bits, LSB-first:
 * bit position p lives in byte p / 8, at bit p % 8 within that byte
 * */

void vcs::BitWriter::write_bits(uint32_t value, int count) {
  if (count < 0 || count > 32)
    throw std::invalid_argument("Bit count must be between 0 and 32");

  for (int i = 0; i < count; i++) {
    // starting a new byte; it begins as all 0s, so padding comes for free
    if (bit_count % 8 == 0)
      buffer.push_back(0);

    if ((value >> i) & 1u)
      buffer.back() |= static_cast<uint8_t>(1u << (bit_count % 8));

    bit_count++;
  }
}

vcs::ByteStream vcs::BitWriter::finish() { return buffer; }

vcs::BitReader::BitReader(const ByteStream &bytes) : buffer(bytes) {}

uint32_t vcs::BitReader::read_bits(int count) {
  if (count < 0 || count > 32)
    throw std::invalid_argument("Bit count must be between 0 and 32");

  // checking up front so that a failed read leaves the position unchanged
  if (bit_pos + static_cast<size_t>(count) > buffer.size() * 8)
    throw std::runtime_error("Read past the end of the bit stream");

  uint32_t result = 0;

  for (int i = 0; i < count; i++) {
    uint32_t bit = (buffer[bit_pos / 8] >> (bit_pos % 8)) & 1u;
    result |= bit << i;
    bit_pos++;
  }

  return result;
}
