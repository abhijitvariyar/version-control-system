#pragma once

#include "../util/bytes.hpp"

namespace vcs {

// Output is never more than 13 bytes larger than the input: anything that
// doesn't shrink is stored as it is, after the header.
ByteStream compress(const ByteStream &input);

// Throws if the data is truncated, corrupt, or wasn't made by compress
ByteStream decompress(const ByteStream &compressed);

} // namespace vcs
