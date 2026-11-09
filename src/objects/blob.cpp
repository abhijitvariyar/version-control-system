#include "vcs/objects/blob.hpp"
#include "vcs/objects/object.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

vcs::Blob::Blob(const std::filesystem::path &filepath) : path(filepath) {
  generate_blob();
}

vcs::ByteStream
vcs::Blob::read_file_bytes(const std::filesystem::path &file_path) {
  /*
   * This function reads raw file text as a stream of bytes and stores them into
   * a vector
   * */
  std::ifstream file(file_path, std::ios::binary);

  if (!file)
    throw std::runtime_error("File does not exist");

  auto size = std::filesystem::file_size(file_path);
  ByteStream bytes(size);

  file.read(reinterpret_cast<char *>(bytes.data()), size);

  return bytes;
}

void vcs::Blob::generate_blob() {
  payload = vcs::wrap(ObjectType::Blob, read_file_bytes(path));
}

vcs::ByteStream vcs::Blob::get_blob() { return payload; }
