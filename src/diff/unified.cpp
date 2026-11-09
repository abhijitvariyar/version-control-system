#include "vcs/diff/unified.hpp"

#include <algorithm>

#include "vcs/diff/myers.hpp"

namespace {

constexpr size_t BINARY_CHECK_BYTES = 8000;

// "-start,count" without the sign. A range of 0 lines starts at the line
// before it, and a count of 1 is left out.
std::string range(size_t lines_before, size_t count) {
  if (count == 0)
    return std::to_string(lines_before) + ",0";
  if (count == 1)
    return std::to_string(lines_before + 1);
  return std::to_string(lines_before + 1) + "," + std::to_string(count);
}

void append_line(std::string &out, char prefix, const std::string &line) {
  out += prefix;
  out += line;
  if (line.empty() || line.back() != '\n')
    out += "\n\\ No newline at end of file\n";
}

std::vector<std::string> lines_of(const std::optional<vcs::ByteStream> &content) {
  if (!content)
    return {};
  return vcs::split_lines(std::string(content->begin(), content->end()));
}

} // namespace

std::string vcs::unified_hunks(const std::vector<std::string> &old_lines,
                               const std::vector<std::string> &new_lines,
                               size_t context) {
  std::vector<DiffOp> ops = diff_lines(old_lines, new_lines);

  std::vector<size_t> changes;
  for (size_t i = 0; i < ops.size(); i++)
    if (ops[i].type != DiffOpType::Equal)
      changes.push_back(i);

  std::string out;
  size_t next = 0;

  while (next < changes.size()) {
    // grow the hunk while the next change is close enough to share context
    size_t last = next;
    while (last + 1 < changes.size() &&
           changes[last + 1] - changes[last] - 1 <= 2 * context)
      last++;

    size_t first_op = changes[next] - std::min(changes[next], context);
    size_t end_op = std::min(ops.size(), changes[last] + 1 + context);

    size_t old_count = 0, new_count = 0;
    for (size_t i = first_op; i < end_op; i++) {
      if (ops[i].type != DiffOpType::Insert)
        old_count++;
      if (ops[i].type != DiffOpType::Delete)
        new_count++;
    }

    out += "@@ -" + range(ops[first_op].old_index, old_count) + " +" +
           range(ops[first_op].new_index, new_count) + " @@\n";

    for (size_t i = first_op; i < end_op; i++) {
      const DiffOp &op = ops[i];
      if (op.type == DiffOpType::Equal)
        append_line(out, ' ', old_lines[op.old_index]);
      else if (op.type == DiffOpType::Delete)
        append_line(out, '-', old_lines[op.old_index]);
      else
        append_line(out, '+', new_lines[op.new_index]);
    }

    next = last + 1;
  }

  return out;
}

bool vcs::is_binary(const ByteStream &content) {
  size_t limit = std::min(content.size(), BINARY_CHECK_BYTES);
  return std::find(content.begin(), content.begin() + limit, 0) !=
         content.begin() + limit;
}

std::string vcs::file_diff(const std::string &path,
                           const std::optional<ByteStream> &old_content,
                           const std::optional<ByteStream> &new_content) {
  if (old_content == new_content)
    return "";

  std::string old_name = old_content ? "a/" + path : "/dev/null";
  std::string new_name = new_content ? "b/" + path : "/dev/null";

  std::string out = "diff --git a/" + path + " b/" + path + "\n";
  if (!old_content)
    out += "new file mode 100644\n";
  if (!new_content)
    out += "deleted file mode 100644\n";

  if ((old_content && is_binary(*old_content)) ||
      (new_content && is_binary(*new_content)))
    return out + "Binary files " + old_name + " and " + new_name + " differ\n";

  std::string hunks = unified_hunks(lines_of(old_content), lines_of(new_content));

  // an empty file being added or deleted has a header but nothing to show
  if (hunks.empty())
    return out;

  return out + "--- " + old_name + "\n+++ " + new_name + "\n" + hunks;
}
