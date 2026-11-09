#include "vcs/objects/object_store.hpp"

#include <fstream>
#include <stdexcept>

#include "vcs/compress/deflate.hpp"
#include "vcs/hash/sha256.hpp"

// ------------------------------------------------------------------------------
vcs::ObjectStore::ObjectStore(std::filesystem::path objects_dir_in)
    : objects_dir(std::move(objects_dir_in)) {}

// ------------------------------------------------------------------------------
std::filesystem::path
vcs::ObjectStore::path_for(const std::string &hex_hash) const {
  /*
    Function: path_for (const)
    Return type: std::filesystem::path
    Params: hex_hash (const std::string &) 
  */
  if (hex_hash.size() != 64)
    throw std::runtime_error("Invalid object hash length: " + hex_hash);

  for (char chr: hex_hash) {
    bool is_digit = (chr >= '0' && chr <= '9');
    bool is_lower = (chr >= 'a' && chr <= 'f');

    if (!is_digit && !is_lower)
      throw std::runtime_error("Invalid object hash: " + hex_hash);
  }

  return objects_dir / hex_hash.substr(0, 2) / hex_hash.substr(2);
}

// ------------------------------------------------------------------------------
std::string vcs::ObjectStore::write(const ByteStream &buffer) {
  std::string hex_hash = SHA256().scramble(buffer);

  if (exists(hex_hash))
    return hex_hash;

  // the hash above is of the uncompressed buffer; only the file is compressed
  ByteStream compressed = compress(buffer);

  std::filesystem::path final_path = path_for(hex_hash);
  std::filesystem::create_directories(final_path.parent_path());

  std::filesystem::path tmp_path = final_path;
  tmp_path += ".tmp";

  std::ofstream out(tmp_path, std::ios::binary);
  if (!out)
    throw std::runtime_error("Failed to open temp file for object: " +
                             tmp_path.string());

  out.write(reinterpret_cast<const char *>(compressed.data()),
            compressed.size());
  out.close();

  std::filesystem::rename(tmp_path, final_path);

  return hex_hash;
}

// ------------------------------------------------------------------------------
vcs::ByteStream vcs::ObjectStore::read(const std::string &hex_hash) const {
  std::filesystem::path path = path_for(hex_hash);

  if (!std::filesystem::exists(path))
    throw std::runtime_error("Object not found: " + hex_hash);

  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Failed to open object: " + path.string());

  auto size = std::filesystem::file_size(path);
  ByteStream bytes(size);
  in.read(reinterpret_cast<char *>(bytes.data()), size);

  ByteStream object = decompress(bytes);

  // decompress can't catch every kind of damage, but the hash can
  if (SHA256().scramble(object) != hex_hash)
    throw std::runtime_error("Object is corrupt: " + hex_hash);

  return object;
}

// ------------------------------------------------------------------------------
bool vcs::ObjectStore::exists(const std::string &hex_hash) const {
  return std::filesystem::exists(path_for(hex_hash));
}

// ------------------------------------------------------------------------------