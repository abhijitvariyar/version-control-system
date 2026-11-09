#pragma once

#include <string>

#include "../util/bytes.hpp"

namespace vcs {

enum class ObjectType { Blob, Tree, Commit };

class UnwrappedObject {
private:
  ObjectType type;
  size_t size;
  ByteStream content;

public:
  inline ObjectType get_object_type() { return type; }
  inline size_t get_content_size() { return size; }
  inline ByteStream &get_content() { return content; }

  UnwrappedObject(ObjectType, size_t, ByteStream &);
};

std::string to_string(ObjectType type);
ObjectType object_type_from_string(std::string_view s);

UnwrappedObject unwrap(const ByteStream &);
ByteStream wrap(ObjectType, const ByteStream &);
} // namespace vcs
