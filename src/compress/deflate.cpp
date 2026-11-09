#include "vcs/compress/deflate.hpp"

#include <array>
#include <stdexcept>

#include "vcs/compress/bitstream.hpp"
#include "vcs/compress/huffman.hpp"
#include "vcs/compress/lz77.hpp"

/*
 * Layout, written through one BitWriter so that a truncated file makes
 * BitReader throw:
 *
 *   magic              4 bytes
 *   mode               1 byte
 *   uncompressed size  8 bytes, low 32 bits first
 *
 * Stored mode follows that with the raw bytes. Compressed mode follows it with
 * one 4-bit code length per symbol, then the Huffman coded tokens.
 *
 * Symbols 0-255 are literal bytes. MATCH_SYMBOL means a match follows, as an
 * 8-bit (length - 3) and a 15-bit (distance - 1), so those two fields can't
 * hold a value outside the ranges LZ77 allows.
 * */

namespace {

constexpr std::array<uint8_t, 4> MAGIC = {'V', 'C', 'Z', '1'};
constexpr uint8_t MODE_STORED = 0;
constexpr uint8_t MODE_COMPRESSED = 1;
constexpr size_t HEADER_SIZE = MAGIC.size() + 1 + 8;

constexpr size_t MATCH_SYMBOL = 256;
constexpr size_t SYMBOL_COUNT = 257;

constexpr int CODE_LENGTH_BITS = 4;
constexpr int MAX_CODE_LENGTH = 15;

constexpr int MATCH_LENGTH_BITS = 8;
constexpr int MATCH_DISTANCE_BITS = 15;
constexpr uint16_t MIN_MATCH = 3;

// A match costs its symbol plus 23 bits of fields, however short it is. That is
// more than a few literals cost, so shorter matches are written as literals.
// 5 was the best value measured on source code and text.
constexpr uint16_t MIN_WORTHWHILE_MATCH = 5;

void write_header(vcs::BitWriter &writer, uint8_t mode, uint64_t size) {
  for (uint8_t byte : MAGIC)
    writer.write_bits(byte, 8);

  writer.write_bits(mode, 8);
  writer.write_bits(static_cast<uint32_t>(size), 32);
  writer.write_bits(static_cast<uint32_t>(size >> 32), 32);
}

vcs::ByteStream store(const vcs::ByteStream &input) {
  vcs::BitWriter writer;
  write_header(writer, MODE_STORED, input.size());

  for (uint8_t byte : input)
    writer.write_bits(byte, 8);

  return writer.finish();
}

// A Huffman code's first bit is its top bit, but BitWriter starts from the
// lowest bit of the value it is given
uint32_t reverse_bits(uint32_t code, int length) {
  uint32_t reversed = 0;

  for (int i = 0; i < length; i++)
    reversed |= ((code >> i) & 1u) << (length - 1 - i);

  return reversed;
}

class SymbolWriter {
private:
  vcs::BitWriter &writer;
  const std::vector<uint8_t> &lengths;
  const std::vector<uint32_t> &codes;

public:
  SymbolWriter(vcs::BitWriter &writer_in, const std::vector<uint8_t> &lengths_in,
               const std::vector<uint32_t> &codes_in)
      : writer(writer_in), lengths(lengths_in), codes(codes_in) {}

  void write(size_t symbol) {
    writer.write_bits(reverse_bits(codes[symbol], lengths[symbol]),
                      lengths[symbol]);
  }
};

vcs::TokenStream drop_short_matches(const vcs::TokenStream &tokens,
                                    const vcs::ByteStream &input) {
  vcs::TokenStream kept;
  kept.reserve(tokens.size());

  size_t pos = 0;

  for (const vcs::LZ77Token &token : tokens) {
    if (token.is_match && token.length < MIN_WORTHWHILE_MATCH) {
      for (size_t i = 0; i < token.length; i++)
        kept.push_back({false, input[pos + i], 0, 0});
    } else {
      kept.push_back(token);
    }

    pos += token.is_match ? token.length : 1;
  }

  return kept;
}

vcs::ByteStream encode(const vcs::ByteStream &input) {
  vcs::TokenStream tokens = drop_short_matches(vcs::lz77_encode(input), input);

  std::vector<uint64_t> freqs(SYMBOL_COUNT, 0);
  for (const vcs::LZ77Token &token : tokens)
    freqs[token.is_match ? MATCH_SYMBOL : token.literal]++;

  std::vector<uint8_t> lengths = vcs::huffman_code_lengths(freqs);
  std::vector<uint32_t> codes = vcs::canonical_codes(lengths);

  vcs::BitWriter writer;
  write_header(writer, MODE_COMPRESSED, input.size());

  for (uint8_t length : lengths)
    writer.write_bits(length, CODE_LENGTH_BITS);

  SymbolWriter symbols(writer, lengths, codes);

  for (const vcs::LZ77Token &token : tokens) {
    if (!token.is_match) {
      symbols.write(token.literal);
      continue;
    }

    symbols.write(MATCH_SYMBOL);
    writer.write_bits(token.length - MIN_MATCH, MATCH_LENGTH_BITS);
    writer.write_bits(token.distance - 1u, MATCH_DISTANCE_BITS);
  }

  return writer.finish();
}

// Canonical codes of one length are consecutive, so a code can be recognised
// from where its length's run starts and how many codes are in the run
class SymbolReader {
private:
  vcs::BitReader &reader;
  std::array<uint32_t, MAX_CODE_LENGTH + 1> counts{};
  std::array<uint32_t, MAX_CODE_LENGTH + 1> first_codes{};
  std::array<uint32_t, MAX_CODE_LENGTH + 1> first_indexes{};
  std::vector<uint16_t> symbols; // sorted by length, then by symbol

public:
  SymbolReader(vcs::BitReader &reader_in, const std::vector<uint8_t> &lengths)
      : reader(reader_in) {
    for (uint8_t length : lengths)
      if (length > 0)
        counts[length]++;

    // More codes than the bits can hold: two symbols would share a code
    uint64_t kraft_sum = 0;
    for (int length = 1; length <= MAX_CODE_LENGTH; length++)
      kraft_sum += uint64_t(counts[length]) << (MAX_CODE_LENGTH - length);

    if (kraft_sum > (uint64_t(1) << MAX_CODE_LENGTH))
      throw std::runtime_error("Invalid Huffman code lengths");

    std::vector<uint32_t> codes = vcs::canonical_codes(lengths);

    for (int length = 1; length <= MAX_CODE_LENGTH; length++) {
      first_indexes[length] = static_cast<uint32_t>(symbols.size());

      for (size_t symbol = 0; symbol < lengths.size(); symbol++) {
        if (lengths[symbol] != length)
          continue;

        if (symbols.size() == first_indexes[length])
          first_codes[length] = codes[symbol];

        symbols.push_back(static_cast<uint16_t>(symbol));
      }
    }
  }

  size_t read() {
    uint32_t code = 0;

    for (int length = 1; length <= MAX_CODE_LENGTH; length++) {
      code = (code << 1) | reader.read_bits(1);

      if (counts[length] > 0 && code >= first_codes[length] &&
          code - first_codes[length] < counts[length])
        return symbols[first_indexes[length] + code - first_codes[length]];
    }

    throw std::runtime_error("Invalid Huffman code in compressed data");
  }
};

vcs::ByteStream decode(vcs::BitReader &reader, uint64_t size) {
  std::vector<uint8_t> lengths(SYMBOL_COUNT);
  for (uint8_t &length : lengths)
    length = static_cast<uint8_t>(reader.read_bits(CODE_LENGTH_BITS));

  SymbolReader symbols(reader, lengths);

  // `size` comes from the file and can't be trusted, so nothing is allocated
  // from it. The tokens grow only as far as the data really goes.
  vcs::TokenStream tokens;
  uint64_t produced = 0;

  while (produced < size) {
    size_t symbol = symbols.read();

    if (symbol == MATCH_SYMBOL) {
      uint16_t length = static_cast<uint16_t>(
          reader.read_bits(MATCH_LENGTH_BITS) + MIN_MATCH);
      uint16_t distance =
          static_cast<uint16_t>(reader.read_bits(MATCH_DISTANCE_BITS) + 1);

      tokens.push_back({true, 0, length, distance});
      produced += length;
    } else {
      tokens.push_back({false, static_cast<uint8_t>(symbol), 0, 0});
      produced++;
    }
  }

  if (produced != size)
    throw std::runtime_error("Compressed data is longer than its stated size");

  return vcs::lz77_decode(tokens);
}

} // namespace

vcs::ByteStream vcs::compress(const ByteStream &input) {
  ByteStream encoded = encode(input);

  if (encoded.size() < HEADER_SIZE + input.size())
    return encoded;

  return store(input);
}

vcs::ByteStream vcs::decompress(const ByteStream &compressed) {
  BitReader reader(compressed);

  for (uint8_t byte : MAGIC)
    if (reader.read_bits(8) != byte)
      throw std::runtime_error("Not a compressed object");

  uint32_t mode = reader.read_bits(8);

  uint64_t size = reader.read_bits(32);
  size |= uint64_t(reader.read_bits(32)) << 32;

  if (mode == MODE_COMPRESSED)
    return decode(reader, size);

  if (mode != MODE_STORED)
    throw std::runtime_error("Unknown compression mode");

  if (compressed.size() - HEADER_SIZE != size)
    throw std::runtime_error("Stored data doesn't match its stated size");

  return ByteStream(compressed.begin() + HEADER_SIZE, compressed.end());
}
