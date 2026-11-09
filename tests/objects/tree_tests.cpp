#include "../test_harness.hpp"

#include <memory>

#include "vcs/hash/sha256.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/tree.hpp"

using vcs::ByteStream;
using vcs::TreeNode;
using vcs::TreeNodeType;

namespace {

// Distinct fake 32-byte blob hashes
ByteStream fake_hash(uint8_t fill) { return ByteStream(32, fill); }

vcs::TreeNodePtr make_root() {
  return std::make_unique<TreeNode>(TreeNodeType::Dir, "");
}

void insert(TreeNode &root, const std::string &path, const ByteStream &hash) {
  vcs::FilePath p(path);
  root.insert(p, hash);
}

// Appends one raw tree entry: "<mode> <name>\0<32-byte hash>"
void append_entry(ByteStream &out, const std::string &mode,
                  const std::string &name, const ByteStream &hash) {
  out.insert(out.end(), mode.begin(), mode.end());
  out.push_back(' ');
  out.insert(out.end(), name.begin(), name.end());
  out.push_back('\0');
  out.insert(out.end(), hash.begin(), hash.end());
}

std::vector<std::string> child_names(const TreeNode &node) {
  std::vector<std::string> names;
  for (const vcs::TreeNodePtr &child : node.children)
    names.push_back(child->name);
  return names;
}

std::string join(const std::vector<std::string> &v) {
  std::string out;
  for (const std::string &s : v)
    out += (out.empty() ? "" : ",") + s;
  return out;
}

} // namespace

// -------- mode strings --------

TEST(tree, mode_strings) {
  CHECK_EQ(vcs::tree_type_to_string(TreeNodeType::File), std::string("100644"));
  CHECK_EQ(vcs::tree_type_to_string(TreeNodeType::Dir), std::string("40000"));
  CHECK_EQ(vcs::tree_type_from_str("100644"), TreeNodeType::File);
  CHECK_EQ(vcs::tree_type_from_str("40000"), TreeNodeType::Dir);
  CHECK_THROWS(vcs::tree_type_from_str("100755"));
}

// -------- serialization --------

TEST(tree, empty_tree) {
  vcs::Tree tree(make_root());
  CHECK_EQ(tree.get_payload(), test::bytes_n("tree 0\0"));
}

TEST(tree, flat_payload_bytes) {
  auto root = make_root();
  insert(*root, "b.txt", fake_hash(0xbb));
  insert(*root, "a.txt", fake_hash(0xaa));

  vcs::Tree tree(std::move(root));

  ByteStream content;
  append_entry(content, "100644", "a.txt", fake_hash(0xaa));
  append_entry(content, "100644", "b.txt", fake_hash(0xbb));

  CHECK_EQ(tree.get_payload(), vcs::wrap(vcs::ObjectType::Tree, content));
}

TEST(tree, nested_payload_bytes) {
  auto root = make_root();
  insert(*root, "b.txt", fake_hash(0xbb));
  insert(*root, "dir/c.txt", fake_hash(0xcc));
  insert(*root, "a.txt", fake_hash(0xaa));

  vcs::Tree tree(std::move(root));

  ByteStream sub_content;
  append_entry(sub_content, "100644", "c.txt", fake_hash(0xcc));
  ByteStream sub_hash = vcs::from_hex(
      vcs::SHA256().scramble(vcs::wrap(vcs::ObjectType::Tree, sub_content)));

  ByteStream content;
  append_entry(content, "100644", "a.txt", fake_hash(0xaa));
  append_entry(content, "100644", "b.txt", fake_hash(0xbb));
  append_entry(content, "40000", "dir", sub_hash);

  CHECK_EQ(tree.get_payload(), vcs::wrap(vcs::ObjectType::Tree, content));
}

TEST(tree, node_hashes_are_set) {
  auto root = make_root();
  insert(*root, "dir/c.txt", fake_hash(0xcc));
  vcs::Tree tree(std::move(root));

  // root hash = sha256 of the root payload
  CHECK_EQ(tree.get_root()->hash,
           vcs::from_hex(vcs::SHA256().scramble(tree.get_payload())));

  // subdir hash = sha256 of its own wrapped tree
  ByteStream sub_content;
  append_entry(sub_content, "100644", "c.txt", fake_hash(0xcc));
  const TreeNode &dir = *tree.get_root()->children.at(0);
  CHECK_EQ(dir.name, std::string("dir"));
  CHECK_EQ(dir.hash, vcs::from_hex(vcs::SHA256().scramble(
                         vcs::wrap(vcs::ObjectType::Tree, sub_content))));
}

TEST(tree, insertion_order_does_not_matter) {
  auto r1 = make_root();
  insert(*r1, "x/1", fake_hash(1));
  insert(*r1, "a", fake_hash(2));
  insert(*r1, "x/0", fake_hash(3));

  auto r2 = make_root();
  insert(*r2, "x/0", fake_hash(3));
  insert(*r2, "a", fake_hash(2));
  insert(*r2, "x/1", fake_hash(1));

  vcs::Tree t1(std::move(r1));
  vcs::Tree t2(std::move(r2));
  CHECK_EQ(t1.get_payload(), t2.get_payload());
}

TEST(tree, git_sort_order_dirs_compare_with_slash) {
  // Dirs sort as if their name ended in '/'.
  //   '-' (0x2d) and '.' (0x2e) sort before '/' (0x2f); '0' (0x30) after.
  auto root = make_root();
  insert(*root, "foo/inner", fake_hash(1));
  insert(*root, "foo.txt", fake_hash(2));
  insert(*root, "foo-bar", fake_hash(3));
  insert(*root, "foo0", fake_hash(4));

  vcs::Tree tree(std::move(root));
  CHECK_EQ(join(child_names(*tree.get_root())),
           std::string("foo-bar,foo.txt,foo,foo0"));
}

TEST(tree, deeply_nested) {
  auto root = make_root();
  insert(*root, "a/b/c/d/e.txt", fake_hash(5));
  vcs::Tree tree(std::move(root));

  const TreeNode *node = tree.get_root();
  for (const char *name : {"a", "b", "c", "d"}) {
    CHECK_EQ(node->children.size(), size_t(1));
    node = node->children.at(0).get();
    CHECK_EQ(node->name, std::string(name));
    CHECK_EQ(node->mode, TreeNodeType::Dir);
    CHECK_EQ(node->hash.size(), size_t(32));
  }
  CHECK_EQ(node->children.at(0)->name, std::string("e.txt"));
  CHECK_EQ(node->children.at(0)->hash, fake_hash(5));
}

// -------- parse_tree_entries --------

TEST(tree, parse_roundtrip) {
  auto root = make_root();
  insert(*root, "b.txt", fake_hash(0xbb));
  insert(*root, "dir/c.txt", fake_hash(0xcc));
  insert(*root, "a.txt", fake_hash(0xaa));
  vcs::Tree tree(std::move(root));

  vcs::UnwrappedObject obj = vcs::unwrap(tree.get_payload());
  CHECK_EQ(obj.get_object_type(), vcs::ObjectType::Tree);

  std::vector<TreeNode> entries = vcs::parse_tree_entries(obj.get_content());
  CHECK_EQ(entries.size(), size_t(3));
  if (entries.size() != 3)
    return;

  CHECK_EQ(entries[0].name, std::string("a.txt"));
  CHECK_EQ(entries[0].mode, TreeNodeType::File);
  CHECK_EQ(entries[0].hash, fake_hash(0xaa));

  CHECK_EQ(entries[1].name, std::string("b.txt"));
  CHECK_EQ(entries[1].hash, fake_hash(0xbb));

  CHECK_EQ(entries[2].name, std::string("dir"));
  CHECK_EQ(entries[2].mode, TreeNodeType::Dir);
  CHECK_EQ(entries[2].hash, tree.get_root()->children.at(2)->hash);
  CHECK(entries[2].children.empty());
}

TEST(tree, parse_empty) {
  CHECK(vcs::parse_tree_entries({}).empty());
}

TEST(tree, parse_hash_with_nul_and_space_bytes) {
  // raw hash bytes may contain ' ' and '\0'; the parser must take 32 bytes
  // verbatim rather than scanning for delimiters
  ByteStream hash(32, 0x00);
  hash[5] = ' ';
  ByteStream content;
  append_entry(content, "100644", "f", hash);
  append_entry(content, "100644", "g", fake_hash(0x20));

  std::vector<TreeNode> entries = vcs::parse_tree_entries(content);
  CHECK_EQ(entries.size(), size_t(2));
  if (entries.size() == 2) {
    CHECK_EQ(entries[0].hash, hash);
    CHECK_EQ(entries[1].name, std::string("g"));
  }
}

TEST(tree, parse_rejects_missing_space) {
  CHECK_THROWS(vcs::parse_tree_entries(test::bytes("100644")));
}

TEST(tree, parse_rejects_missing_nul) {
  CHECK_THROWS(vcs::parse_tree_entries(test::bytes("100644 name")));
}

TEST(tree, parse_rejects_truncated_hash) {
  ByteStream content;
  append_entry(content, "100644", "f", ByteStream(31, 0xab));
  CHECK_THROWS(vcs::parse_tree_entries(content));
}

TEST(tree, parse_rejects_bad_mode) {
  ByteStream content;
  append_entry(content, "123", "f", fake_hash(1));
  CHECK_THROWS(vcs::parse_tree_entries(content));
}

// -------- insert errors --------

TEST(tree, insert_duplicate_path_throws) {
  auto root = make_root();
  insert(*root, "a.txt", fake_hash(1));
  CHECK_THROWS(insert(*root, "a.txt", fake_hash(2)));
}

TEST(tree, insert_through_file_throws) {
  auto root = make_root();
  insert(*root, "a", fake_hash(1));
  CHECK_THROWS(insert(*root, "a/b", fake_hash(2)));
}

TEST(tree, insert_over_dir_throws) {
  auto root = make_root();
  insert(*root, "a/b", fake_hash(1));
  CHECK_THROWS(insert(*root, "a", fake_hash(2)));
}

TEST(tree, insert_empty_path_throws) {
  auto root = make_root();
  CHECK_THROWS(insert(*root, "", fake_hash(1)));
}

TEST(tree, insert_absolute_path_throws) {
  auto root = make_root();
  CHECK_THROWS(insert(*root, "/etc/passwd", fake_hash(1)));
}

TEST(tree, duplicate_children_rejected_at_build) {
  auto root = make_root();
  root->children.push_back(std::make_unique<TreeNode>(TreeNodeType::File, "x"));
  root->children.push_back(std::make_unique<TreeNode>(TreeNodeType::File, "x"));
  root->children[0]->hash = fake_hash(1);
  root->children[1]->hash = fake_hash(2);
  CHECK_THROWS(vcs::Tree(std::move(root)));
}

// -------- get_objects --------

TEST(tree, objects_has_one_per_directory_root_last) {
  auto root = make_root();
  insert(*root, "a/b/c.txt", ByteStream(32, 0x11));
  insert(*root, "a/d.txt", ByteStream(32, 0x22));
  insert(*root, "e/f.txt", ByteStream(32, 0x33));
  insert(*root, "g.txt", ByteStream(32, 0x44));
  vcs::Tree tree(std::move(root));

  // the root, a, a/b and e
  CHECK_EQ(tree.get_objects().size(), size_t(4));
  CHECK_EQ(tree.get_objects().back(), tree.get_payload());

  const TreeNode &a = *tree.get_root()->children.at(0);
  CHECK_EQ(vcs::from_hex(vcs::SHA256().scramble(tree.get_objects().at(1))),
           a.hash);
}

TEST(tree, objects_of_empty_tree_is_just_the_root) {
  vcs::Tree tree(make_root());
  CHECK_EQ(tree.get_objects().size(), size_t(1));
}
