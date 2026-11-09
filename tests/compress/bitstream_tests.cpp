#include "../test_harness.hpp"

#include "vcs/compress/bitstream.hpp"

// Bits are packed LSB-first: the first bit written lands in bit 0 of byte 0.

TEST(bitstream, empty_writer_finishes_empty) {
  vcs::BitWriter writer;
  CHECK(writer.finish().empty());
}

TEST(bitstream, single_bit_padded_with_zeros) {
  vcs::BitWriter writer;
  writer.write_bits(1, 1);
  CHECK_EQ(writer.finish(), vcs::ByteStream{0x01});
}

TEST(bitstream, lsb_first_packing) {
  vcs::BitWriter writer;
  writer.write_bits(0b101, 3); // bits 0..2
  writer.write_bits(0b11, 2);  // bits 3..4
  CHECK_EQ(writer.finish(), vcs::ByteStream{0b00011101});
}

TEST(bitstream, value_spanning_bytes) {
  vcs::BitWriter writer;
  writer.write_bits(0x1, 4);
  writer.write_bits(0xABC, 12); // straddles the byte boundary
  CHECK_EQ(writer.finish(), (vcs::ByteStream{0xC1, 0xAB}));
}

TEST(bitstream, only_low_count_bits_written) {
  // high bits of `value` above `count` must be ignored
  vcs::BitWriter writer;
  writer.write_bits(0xFF, 3);
  writer.write_bits(0, 5);
  CHECK_EQ(writer.finish(), vcs::ByteStream{0x07});
}

TEST(bitstream, output_length_is_ceil_bits_over_8) {
  for (int bits = 1; bits <= 40; bits++) {
    vcs::BitWriter writer;
    for (int i = 0; i < bits; i++)
      writer.write_bits(1, 1);
    CHECK_EQ(writer.finish().size(), static_cast<size_t>((bits + 7) / 8));
  }
}

TEST(bitstream, mixed_width_roundtrip) {
  const std::vector<std::pair<uint32_t, int>> values = {
      {1, 1},       {0, 1},     {5, 3},          {0xA5, 8},   {0x1ABC, 13},
      {0xDEADBEEF, 32}, {0, 7}, {0x7FFF, 15},    {1, 1},      {0xFFFFFFFF, 32},
  };

  vcs::BitWriter writer;
  for (auto [value, count] : values)
    writer.write_bits(value, count);
  vcs::ByteStream data = writer.finish();

  vcs::BitReader reader(data);
  for (auto [value, count] : values)
    CHECK_EQ(reader.read_bits(count), value);
}

TEST(bitstream, read_past_end_throws) {
  vcs::BitWriter writer;
  writer.write_bits(0b101, 3);
  vcs::ByteStream data = writer.finish(); // one byte

  vcs::BitReader reader(data);
  CHECK_EQ(reader.read_bits(8), uint32_t(0b101)); // padding bits are readable
  CHECK_THROWS(reader.read_bits(1));
}

TEST(bitstream, read_from_empty_throws) {
  vcs::ByteStream empty;
  vcs::BitReader reader(empty);
  CHECK_THROWS(reader.read_bits(1));
}
