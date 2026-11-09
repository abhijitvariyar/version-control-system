/* Input the raw file text
 * Padding into multiple of 512 bits
 * Block construction
 * 64 round mixing
 * Final output of 256 bits
 * */
#pragma once

#include "vcs/util/bytes.hpp"

#define BLOCK_SIZE_BITS 512
#define BLOCK_SIZE_BYTES 64

#define TWO_POWER_32 4294967296.0

namespace vcs {

class SHA256 {
private:
  ByteStream pad_byte_stream(const ByteStream &);

public:
  SHA256();
  std::string scramble(const ByteStream &);
};

} // namespace vcs
