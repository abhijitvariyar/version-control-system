#pragma once

#include <filesystem>

#include "vcs/util/bytes.hpp"

namespace vcs {

class Blob {
private:
  std::filesystem::path path;
  ByteStream payload;

  ByteStream read_file_bytes(const std::filesystem::path &);
  void generate_blob();

public:
  Blob(const std::filesystem::path &);
  ByteStream get_blob();
};

} // namespace vcs
