#include "../test_harness.hpp"

#include <map>
#include <memory>

#include "vcs/diff/tree_diff.hpp"
#include "vcs/hash/sha256.hpp"
#include "vcs/objects/object.hpp"

using vcs::ChangeType;
using vcs::TreeChange;

namespace {

// Builds trees in memory and counts every tree read, to check that unchanged
// directories are skipped
class Trees {
private:
  std::map<std::string, vcs::ByteStream> objects;

public:
  std::vector<std::string> reads;

  // path -> one byte that stands in for the file's content
  std::string build(const std::map<std::string, uint8_t> &files) {
    auto root = std::make_unique<vcs::TreeNode>(vcs::TreeNodeType::Dir, "");
    for (const auto &[path, fill] : files) {
      vcs::FilePath p(path);
      root->insert(p, vcs::ByteStream(32, fill));
    }

    vcs::Tree tree(std::move(root));
    for (const vcs::ByteStream &object : tree.get_objects())
      objects[vcs::SHA256().scramble(object)] = object;

    return vcs::to_hex(tree.get_root()->hash);
  }

  vcs::TreeReader reader() {
    return [this](const std::string &hash) {
      reads.push_back(hash);
      vcs::UnwrappedObject object = vcs::unwrap(objects.at(hash));
      return vcs::parse_tree_entries(object.get_content());
    };
  }
};

std::string hash_of(uint8_t fill) { return vcs::to_hex(vcs::ByteStream(32, fill)); }

std::string describe_changes(const std::vector<TreeChange> &changes) {
  std::string out;
  for (const TreeChange &change : changes) {
    out += change.type == ChangeType::Added     ? "A "
           : change.type == ChangeType::Deleted ? "D "
                                                : "M ";
    out += change.path + "\n";
  }
  return out;
}

} // namespace

TEST(tree_diff, same_tree_is_empty) {
  Trees trees;
  std::string tree = trees.build({{"a.txt", 1}, {"src/b.txt", 2}});
  CHECK(vcs::diff_trees(trees.reader(), tree, tree).empty());
  CHECK(trees.reads.empty());
}

TEST(tree_diff, both_missing_is_empty) {
  Trees trees;
  CHECK(vcs::diff_trees(trees.reader(), std::nullopt, std::nullopt).empty());
}

TEST(tree_diff, from_nothing_everything_is_added) {
  Trees trees;
  std::string tree = trees.build({{"a.txt", 1}, {"src/deep/b.txt", 2}});

  std::vector<TreeChange> changes =
      vcs::diff_trees(trees.reader(), std::nullopt, tree);
  CHECK_EQ(describe_changes(changes), std::string("A a.txt\nA src/deep/b.txt\n"));
  CHECK_EQ(changes.at(1).new_hash, hash_of(2));
  CHECK_EQ(changes.at(1).old_hash, std::string());
}

TEST(tree_diff, to_nothing_everything_is_deleted) {
  Trees trees;
  std::string tree = trees.build({{"a.txt", 1}});
  std::vector<TreeChange> changes =
      vcs::diff_trees(trees.reader(), tree, std::nullopt);
  CHECK_EQ(describe_changes(changes), std::string("D a.txt\n"));
  CHECK_EQ(changes.at(0).old_hash, hash_of(1));
}

TEST(tree_diff, modified_added_and_deleted) {
  Trees trees;
  std::string before = trees.build({{"a.txt", 1}, {"b.txt", 2}, {"c.txt", 3}});
  std::string after = trees.build({{"a.txt", 1}, {"b.txt", 9}, {"d.txt", 4}});

  std::vector<TreeChange> changes = vcs::diff_trees(trees.reader(), before, after);
  CHECK_EQ(describe_changes(changes),
           std::string("M b.txt\nD c.txt\nA d.txt\n"));
  CHECK_EQ(changes.at(0).old_hash, hash_of(2));
  CHECK_EQ(changes.at(0).new_hash, hash_of(9));
}

TEST(tree_diff, nested_change_has_full_path) {
  Trees trees;
  std::string before = trees.build({{"src/deep/a.txt", 1}});
  std::string after = trees.build({{"src/deep/a.txt", 2}});
  CHECK_EQ(describe_changes(vcs::diff_trees(trees.reader(), before, after)),
           std::string("M src/deep/a.txt\n"));
}

TEST(tree_diff, unchanged_directory_is_not_read) {
  Trees trees;
  std::string before = trees.build({{"big/a", 1}, {"big/b", 2}, {"x", 3}});
  std::string after = trees.build({{"big/a", 1}, {"big/b", 2}, {"x", 4}});
  std::string big = trees.build({{"a", 1}, {"b", 2}});

  vcs::diff_trees(trees.reader(), before, after);

  CHECK_EQ(trees.reads.size(), size_t(2)); // the two roots only
  for (const std::string &read : trees.reads)
    CHECK(read != big);
}

TEST(tree_diff, file_replaced_by_directory) {
  Trees trees;
  std::string before = trees.build({{"a", 1}});
  std::string after = trees.build({{"a/b.txt", 2}});
  CHECK_EQ(describe_changes(vcs::diff_trees(trees.reader(), before, after)),
           std::string("D a\nA a/b.txt\n"));
}

TEST(tree_diff, directory_replaced_by_file) {
  Trees trees;
  std::string before = trees.build({{"a/b.txt", 1}, {"a/c.txt", 2}});
  std::string after = trees.build({{"a", 3}});
  CHECK_EQ(describe_changes(vcs::diff_trees(trees.reader(), before, after)),
           std::string("A a\nD a/b.txt\nD a/c.txt\n"));
}

TEST(tree_diff, similar_names_line_up) {
  // "a.txt" sorts before the directory "a" in a tree, as "a/" > "a."
  Trees trees;
  std::string before = trees.build({{"a.txt", 1}, {"a/x", 2}, {"a-b", 3}});
  std::string after = trees.build({{"a.txt", 1}, {"a/x", 9}, {"a-b", 3}});
  CHECK_EQ(describe_changes(vcs::diff_trees(trees.reader(), before, after)),
           std::string("M a/x\n"));
}

TEST(tree_diff, directory_removed_lists_every_file) {
  Trees trees;
  std::string before = trees.build({{"keep", 1}, {"gone/a", 2}, {"gone/sub/b", 3}});
  std::string after = trees.build({{"keep", 1}});
  CHECK_EQ(describe_changes(vcs::diff_trees(trees.reader(), before, after)),
           std::string("D gone/a\nD gone/sub/b\n"));
}
