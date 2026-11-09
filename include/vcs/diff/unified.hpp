#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vcs/util/bytes.hpp"

namespace vcs {

// The hunks of a unified diff, from the first `@@` line on. Changes closer
// than 2 * context lines apart share a hunk. Empty when nothing changed.
std::string unified_hunks(const std::vector<std::string> &old_lines,
                          const std::vector<std::string> &new_lines,
                          size_t context = 3);

// Whether git would treat the content as binary: a NUL in the first 8000 bytes
bool is_binary(const ByteStream &content);

// One file's diff in git's format. An empty side means the file doesn't exist
// there, so it was added or deleted. Empty when the two sides are the same.
std::string file_diff(const std::string &path,
                      const std::optional<ByteStream> &old_content,
                      const std::optional<ByteStream> &new_content);

} // namespace vcs
