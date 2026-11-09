#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace vcs {

inline std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Failed to open file: " + path.string());

  return std::string(std::istreambuf_iterator<char>(in), {});
}

// Writes to a temp file and renames it into place, so a crash part way through
// can't leave a half-written file behind
inline void write_file_atomic(const std::filesystem::path &path,
                              const std::string &content) {
  std::filesystem::path tmp_path = path;
  tmp_path += ".tmp";

  std::ofstream out(tmp_path, std::ios::binary);
  if (!out)
    throw std::runtime_error("Failed to open file: " + tmp_path.string());

  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.close();
  if (!out)
    throw std::runtime_error("Failed to write file: " + tmp_path.string());

  std::filesystem::rename(tmp_path, path);
}

} // namespace vcs
