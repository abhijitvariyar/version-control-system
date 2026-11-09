#pragma once

#include <filesystem>
#include <string>

#include "vcs/util/bytes.hpp"

namespace vcs {

class ObjectStore {
private:
  std::filesystem::path objects_dir;

  std::filesystem::path path_for(const std::string &hex_hash) const;

public:
  ObjectStore(std::filesystem::path objects_dir);

  std::string write(const ByteStream &buffer);
  ByteStream read(const std::string &hex_hash) const;
  bool exists(const std::string &hex_hash) const;
};

} // namespace vcs