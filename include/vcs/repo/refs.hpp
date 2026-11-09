#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vcs {

// Branch names follow a subset of git's rules: slash-separated parts, none
// empty or starting with '.', no "..", no spaces or ~^:?*[\ characters, not
// starting with '-' and not ending in '.' or ".lock". Throws otherwise.
void validate_branch_name(const std::string &name);

class Refs {
private:
  std::filesystem::path vcs_dir;

  // The ref HEAD points at, such as refs/heads/main. Empty when HEAD is
  // detached, meaning it holds a commit hash directly.
  std::optional<std::string> head_ref() const;

public:
  explicit Refs(std::filesystem::path vcs_dir);

  // The branch HEAD points at, such as "main". Empty when HEAD is detached.
  std::optional<std::string> current_branch() const;

  // The commit HEAD resolves to. Empty before the first commit on a branch.
  std::optional<std::string> head_commit() const;

  // Moves the current branch to `hex_hash`, or HEAD itself when detached
  void update_head(const std::string &hex_hash);

  // Every branch with a commit, sorted. Nested names like "feature/x" are
  // included.
  std::vector<std::string> branches() const;

  // The commit a branch points at. Empty if there is no such branch, including
  // when the name isn't a valid branch name.
  std::optional<std::string> branch_commit(const std::string &name) const;

  // Throws if the branch exists, or would clash with one: "a" and "a/b" can't
  // both exist, since one would be a file and the other a directory
  void create_branch(const std::string &name, const std::string &hex_hash);

  // Points HEAD at a branch, or straight at a commit
  void point_head_at_branch(const std::string &name);
  void detach_head(const std::string &hex_hash);
};

} // namespace vcs
