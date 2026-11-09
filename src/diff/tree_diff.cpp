#include "vcs/diff/tree_diff.hpp"

#include <algorithm>

namespace {

struct Entry {
  std::string key; // the name, plus "/" for a directory, which is tree order
  std::string name;
  bool is_dir;
  std::string hash;
};

std::vector<Entry> entries_of(const vcs::TreeReader &read_tree,
                              const std::optional<std::string> &hash) {
  std::vector<Entry> entries;
  if (!hash)
    return entries;

  for (const vcs::TreeNode &node : read_tree(*hash)) {
    bool is_dir = node.mode == vcs::TreeNodeType::Dir;
    entries.push_back({node.name + (is_dir ? "/" : ""), node.name, is_dir,
                       vcs::to_hex(node.hash)});
  }

  // trees are written sorted, but one read from disk can't be trusted to be
  std::sort(entries.begin(), entries.end(),
            [](const Entry &x, const Entry &y) { return x.key < y.key; });

  return entries;
}

std::string join(const std::string &dir, const std::string &name) {
  return dir.empty() ? name : dir + "/" + name;
}

class Walker {
private:
  const vcs::TreeReader &read_tree;
  std::vector<vcs::TreeChange> &changes;

  // Every file under an entry that exists on one side only
  void one_side(const Entry &entry, const std::string &path, bool is_old) {
    if (!entry.is_dir) {
      if (is_old)
        changes.push_back({path, vcs::ChangeType::Deleted, entry.hash, ""});
      else
        changes.push_back({path, vcs::ChangeType::Added, "", entry.hash});
      return;
    }

    for (const Entry &child : entries_of(read_tree, entry.hash))
      one_side(child, join(path, child.name), is_old);
  }

public:
  Walker(const vcs::TreeReader &read_tree_in,
         std::vector<vcs::TreeChange> &changes_in)
      : read_tree(read_tree_in), changes(changes_in) {}

  // Steps through both sorted lists together, like the merge step of a sort
  void compare(const std::vector<Entry> &old_entries,
               const std::vector<Entry> &new_entries, const std::string &dir) {
    size_t i = 0, j = 0;

    while (i < old_entries.size() || j < new_entries.size()) {
      if (j == new_entries.size() ||
          (i < old_entries.size() && old_entries[i].key < new_entries[j].key)) {
        one_side(old_entries[i], join(dir, old_entries[i].name), true);
        i++;
        continue;
      }

      if (i == old_entries.size() || new_entries[j].key < old_entries[i].key) {
        one_side(new_entries[j], join(dir, new_entries[j].name), false);
        j++;
        continue;
      }

      const Entry &before = old_entries[i++];
      const Entry &after = new_entries[j++];

      if (before.hash == after.hash)
        continue;

      std::string path = join(dir, before.name);

      if (before.is_dir)
        compare(entries_of(read_tree, before.hash),
                entries_of(read_tree, after.hash), path);
      else
        changes.push_back(
            {path, vcs::ChangeType::Modified, before.hash, after.hash});
    }
  }
};

} // namespace

std::vector<vcs::TreeChange>
vcs::diff_trees(const TreeReader &read_tree,
                const std::optional<std::string> &old_tree,
                const std::optional<std::string> &new_tree) {
  std::vector<TreeChange> changes;

  if (old_tree == new_tree)
    return changes;

  Walker(read_tree, changes)
      .compare(entries_of(read_tree, old_tree), entries_of(read_tree, new_tree),
               "");

  return changes;
}
