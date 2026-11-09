/*
 * Input the raw file text
 * Padding into multiple of 512 bits
 * Block construction
 * 64 round mixing
 * Final output of 256 bits
 * */

#include "vcs/hash/sha256.hpp"

#include <cmath>
#include <stdexcept>

vcs::SHA256::SHA256() {}

vcs::ByteStream vcs::SHA256::pad_byte_stream(const ByteStream &buffer) {
  /*
   * This function pads the raw text into multiple of 512 bits
   *
   * target length = multiple of 512 bits = multiple of 64 bytes
   * */

  // creating a copy of the original buffer
  ByteStream payload = buffer;

  // add 1 byte of 1-bit followed by 0-bits
  payload.push_back(0x80);

  // calculating how many 0-bit bytes required to make a total size multiple of
  // 64-bytes
  size_t buffer_size = buffer.size();
  size_t buffer_size_bits = buffer_size * 8;
  size_t payload_rem_bytes = 64 - (payload.size() % 64);

  /*
   * We require 8 bytes o accomodate the file size
   * Therefore, if the remaining bytes in the current block in the payload is
   * less than 8 bytes, we need to add a new block of 56-bytes of 0s and 8-byte
   * length field at the end
   * */

  int zero_filler_cnt = (payload_rem_bytes < 8) ? (payload_rem_bytes + 56)
                                                : (payload_rem_bytes - 8);
  for (int i = 0; i < zero_filler_cnt; i++)
    payload.push_back(0x00);

  // adding right justified file size in bits
  for (int i = 7; i >= 0; --i)
    payload.push_back(
        static_cast<uint8_t>((buffer_size_bits >> (i * 8)) & 0xFF));

  if (payload.size() % 64 != 0)
    throw std::runtime_error("Issue in padding. Size not multiple of 512 bits");

  return payload;
}

std::string vcs::SHA256::scramble(const ByteStream &buffer) {
  ByteStream padded_bytes = pad_byte_stream(buffer);

  std::vector<uint32_t> H(8);
  std::vector<uint32_t> K(64);

  std::vector<int> primes = {
      2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,
      43,  47,  53,  59,  61,  67,  71,  73,  79,  83,  89,  97,  101,
      103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167,
      173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239,
      241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311};

  // lambda function for hash
  auto sqrt_frac32 = [](int prime) {
    double squareroot = std::sqrt(prime);
    double sqrt_frac = squareroot - std::floor(squareroot);

    return static_cast<uint32_t>(std::floor(sqrt_frac * TWO_POWER_32));
  };

  // lambda function for round constant
  auto cbrt_frac32 = [](int prime) {
    double cuberoot = std::cbrt(prime);
    double cbrt_frac = cuberoot - std::floor(cuberoot);

    return static_cast<uint32_t>(std::floor(cbrt_frac * TWO_POWER_32));
  };

  // shift bits right
  auto shr = [](uint32_t num, int shift) { return num >> shift; };

  // rotate bits right
  auto rotr = [](uint32_t num, int shift) {
    return (num << (32 - shift)) | (num >> shift);
  };

  // calculate the 8 initial hashes and 64 round constants
  for (int i = 0; i < 64; i++) {
    // calculate the initial hashes
    if (i < 8)
      H[i] = sqrt_frac32(primes[i]);

    // calculate the round constants
    K[i] = cbrt_frac32(primes[i]);
  }

  int block_cnt = padded_bytes.size() / 64;

  // iterating through each 512-bit (64-byte) block
  int curr_block_ptr = 0;
  for (int n = 0; n < block_cnt; n++) {
    ByteStream N(padded_bytes.begin() + curr_block_ptr,
                 padded_bytes.begin() + curr_block_ptr + BLOCK_SIZE_BYTES);

    std::vector<uint32_t> words(64);

    // initialize the first 16 words formed from the current block
    for (int i = 0; i < 16; i++) {
      words[i] = (static_cast<uint32_t>(N[i * 4]) << 24) |
                 (static_cast<uint32_t>(N[i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(N[i * 4 + 2]) << 8) |
                 static_cast<uint32_t>(N[i * 4 + 3]);
    }

    for (int i = 16; i < 64; i++) {
      auto s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                (shr(words[i - 15], 3));
      auto s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                (shr(words[i - 2], 10));
      words[i] = (words[i - 16] + s0 + words[i - 7] + s1);
    }

    uint32_t a = H[0];
    uint32_t b = H[1];
    uint32_t c = H[2];
    uint32_t d = H[3];
    uint32_t e = H[4];
    uint32_t f = H[5];
    uint32_t g = H[6];
    uint32_t h = H[7];

    for (int i = 0; i < 64; i++) {
      auto S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      auto Ch = (e & f) ^ (~e & g);
      auto T1 = h + S1 + Ch + K[i] + words[i];

      auto S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      auto Maj = (a & b) ^ (a & c) ^ (b & c);
      auto T2 = S0 + Maj;

      h = g;
      g = f;
      f = e;
      e = d + T1;
      d = c;
      c = b;
      b = a;
      a = T1 + T2;
    }

    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
    H[5] += f;
    H[6] += g;
    H[7] += h;

    curr_block_ptr += BLOCK_SIZE_BYTES;
  }

  std::string digest;
  digest.reserve(64);

  const char *hex_digits = "0123456789abcdef";

  for (uint32_t h : H) {
    for (int byte_idx = 3; byte_idx >= 0; --byte_idx) {
      uint8_t byte = static_cast<uint8_t>((h >> (byte_idx * 8)) & 0xFF);
      digest.push_back(hex_digits[byte >> 4]);   // high nibble
      digest.push_back(hex_digits[byte & 0x0F]); // low nibble
    }
  }

  return digest;
}
