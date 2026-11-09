#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vcs {

typedef std::filesystem::path FilePath;

typedef std::vector<uint8_t> ByteStream;

inline std::string to_hex(std::span<const uint8_t> bytes) {
  std::string hex_string;
  hex_string.reserve(bytes.size() * 2);

  const char *hex_digits = "0123456789abcdef";

  for (uint8_t byte : bytes) {
    hex_string.push_back(hex_digits[byte >> 4]);   // high nibble
    hex_string.push_back(hex_digits[byte & 0x0F]); // low nibble
  }

  return hex_string;
}

inline uint8_t hex_char_to_nibble(char c) {
  if (c >= '0' && c <= '9')
    return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<uint8_t>(c - 'a' + 10);
  throw std::invalid_argument("Invalid hex character");
}

inline ByteStream from_hex(std::string_view hex) {
  if (hex.size() % 2 != 0)
    throw std::invalid_argument("Hex string must have an even length");

  ByteStream bytes;
  bytes.reserve(hex.size() / 2);

  for (size_t i = 0; i < hex.size(); i += 2) {
    uint8_t high = hex_char_to_nibble(hex[i]);
    uint8_t low = hex_char_to_nibble(hex[i + 1]);
    bytes.push_back(static_cast<uint8_t>((high << 4) | low));
  }

  return bytes;
}

} // namespace vcs
