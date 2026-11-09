#include "../../include/vcs/objects/object.hpp"
#include <cstddef>
#include <stdexcept>

vcs::UnwrappedObject::UnwrappedObject(vcs::ObjectType t_type, size_t t_size,
                                      ByteStream &t_content)
    : type(t_type), size(t_size), content(t_content) {}

std::string vcs::to_string(ObjectType type) {
  switch (type) {
  case ObjectType::Blob:
    return "blob";
  case ObjectType::Tree:
    return "tree";
  case ObjectType::Commit:
    return "commit";
  }

  throw std::invalid_argument("Unknown ObjectType");
}

vcs::ObjectType vcs::object_type_from_string(std::string_view s) {
  if (s == "blob")
    return ObjectType::Blob;
  if (s == "tree")
    return ObjectType::Tree;
  if (s == "commit")
    return ObjectType::Commit;

  throw std::invalid_argument("Unknown object type: " + std::string(s));
}

vcs::ByteStream vcs::wrap(ObjectType type, const ByteStream &content) {
  std::string header =
      vcs::to_string(type) + " " + std::to_string(content.size());

  ByteStream buffer;

  for (char chr : header)
    buffer.push_back(static_cast<uint8_t>(chr));

  buffer.push_back(0x00);
  buffer.insert(buffer.end(), content.begin(), content.end());

  return buffer;
}

vcs::UnwrappedObject vcs::unwrap(const ByteStream &buffer) {
  // Extracting object type
  std::string type = "";
  size_t ind = 0;
  while (ind < buffer.size() && buffer[ind] != ' ') {
    type += static_cast<char>(buffer[ind]);
    ind++;
  }

  if (ind >= buffer.size())
    throw std::runtime_error("Malformed object: missing space after type");

  vcs::ObjectType object_type = vcs::object_type_from_string(type);

  // Extracting content size
  ind++;
  size_t size_start = ind;
  size_t content_size = 0;
  while (ind < buffer.size() && buffer[ind] != 0x00) {
    uint8_t chr = buffer[ind];
    if (chr < '0' || chr > '9')
      throw std::runtime_error("Malformed object: non-digit in size field");

    content_size = (content_size * 10) + (chr - '0');

    // content can never be larger than the buffer
    if (content_size > buffer.size())
      throw std::runtime_error("Malformed object: size larger than buffer");

    ind++;
  }

  if (ind >= buffer.size())
    throw std::runtime_error("Malformed object: missing NUL after size");
 
  if (ind == size_start)
    throw std::runtime_error("Malformed object: empty size field");

  // Extracting content
  ind++;
  ByteStream content;
  content.insert(content.begin(), buffer.begin() + ind, buffer.end());

  if (content.size() != content_size)
    throw std::runtime_error("Buffer size does not match. File corrupted");

  vcs::UnwrappedObject obj(object_type, content_size, content);

  return obj;
}
