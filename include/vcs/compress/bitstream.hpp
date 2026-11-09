#pragma once

#include "../util/bytes.hpp"

namespace vcs {

class BitWriter {
private:
    ByteStream buffer;
    size_t bit_count = 0;

public:
    void write_bits(uint32_t , int);
    ByteStream finish();
};

class BitReader {
private:
    const ByteStream &buffer;
    size_t bit_pos = 0;

public:
    explicit BitReader(const ByteStream &);
    uint32_t read_bits(int);
};

} // namespace vcs