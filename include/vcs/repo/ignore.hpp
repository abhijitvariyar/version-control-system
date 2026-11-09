#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vcs {

// The rules from a .vcsignore file at the repository root, one pattern a line.
// Blank lines and lines starting with # are skipped.
//
//   build.log   any file or directory with this name, at any depth
//   *.o         * and ? match within one path component
//   bin/        a trailing slash matches directories only
//   /notes.txt  a leading slash, or one in the middle, anchors to the root
//
// Unlike .gitignore there is no `!` negation, and `**` is the same as `*`.
class IgnoreRules {
private:
  struct Pattern {
    std::vector<std::string> parts;
    bool anchored;
    bool dir_only;
  };

  std::vector<Pattern> patterns;

public:
  // A missing file means no rules. Throws on a pattern that can't be used.
  static IgnoreRules load(const std::filesystem::path &file);
  static IgnoreRules parse(const std::string &text);

  // Whether this path itself matches a rule. Parents aren't checked: a walk is
  // expected to stop at an ignored directory rather than go inside it.
  bool matches(const std::string &repo_path, bool is_dir) const;
};

} // namespace vcs
