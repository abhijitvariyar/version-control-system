#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vcs {

enum class DiffOpType { Equal, Delete, Insert };

// One line of an edit script. `old_index` and `new_index` are how many old and
// new lines come before this one, so for an Equal or Delete `old_index` is the
// old line itself, and for an Equal or Insert `new_index` is the new line.
struct DiffOp {
  DiffOpType type;
  size_t old_index;
  size_t new_index;
};

// Splits text into lines, each keeping its '\n'. Only the last line can lack
// one, which is how a missing newline at the end of a file shows up.
std::vector<std::string> split_lines(std::string_view text);

// A shortest edit script from `old_lines` to `new_lines` (Myers' algorithm, in
// linear space). Within each change, deletions come before insertions.
std::vector<DiffOp> diff_lines(const std::vector<std::string> &old_lines,
                               const std::vector<std::string> &new_lines);

} // namespace vcs
