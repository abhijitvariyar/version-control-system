#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "vcs/objects/tree.hpp"

namespace vcs {

enum class ChangeType { Added, Deleted, Modified };

// One file that differs. Hashes are in hex; the side that doesn't exist (old
// for Added, new for Deleted) is empty.
struct TreeChange {
  std::string path;
  ChangeType type;
  std::string old_hash;
  std::string new_hash;
};

// Returns the entries of the tree object with this hex hash
typedef std::function<std::vector<TreeNode>(const std::string &)> TreeReader;

// Every file that differs between two trees, in tree order. An empty side is
// an empty tree. Directories with the same hash on both sides are skipped
// without being read, and a path that changes between file and directory shows
// up as a deletion plus additions.
std::vector<TreeChange> diff_trees(const TreeReader &read_tree,
                                   const std::optional<std::string> &old_tree,
                                   const std::optional<std::string> &new_tree);

} // namespace vcs
