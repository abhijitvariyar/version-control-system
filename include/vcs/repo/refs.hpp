#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vcs {

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
};

} // namespace vcs
