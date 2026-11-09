#include "vcs/objects/tree.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "vcs/hash/sha256.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/util/bytes.hpp"

std::vector<vcs::TreeNode> vcs::parse_tree_entries(const ByteStream &payload) {
  std::vector<TreeNode> entries;

  size_t idx = 0;
  while (idx < payload.size()) {
    size_t space_idx = idx;
    while (space_idx < payload.size() && payload[space_idx] != ' ')
      space_idx++;
    if (space_idx >= payload.size())
      throw std::runtime_error("Malformed tree entry: missing mode separator");

    std::string mode_str(payload.begin() + idx, payload.begin() + space_idx);
    TreeNodeType mode = tree_type_from_str(mode_str);

    size_t nul_idx = space_idx + 1;
    while (nul_idx < payload.size() && payload[nul_idx] != 0x00)
      nul_idx++;
    if (nul_idx >= payload.size())
      throw std::runtime_error("Malformed tree entry: missing name terminator");

    std::string name(payload.begin() + space_idx + 1, payload.begin() + nul_idx);

    size_t hash_start = nul_idx + 1;
    if (hash_start + 32 > payload.size())
      throw std::runtime_error("Malformed tree entry: truncated hash");

    ByteStream hash(payload.begin() + hash_start,
                    payload.begin() + hash_start + 32);

    TreeNode entry(mode, name);
    entry.hash = std::move(hash);
    entries.push_back(std::move(entry));

    idx = hash_start + 32;
  }

  return entries;
}

static void insert_at(vcs::TreeNode *node, const vcs::StringVec &parts,
                      size_t idx, const vcs::ByteStream &blob_hash) {
  using namespace vcs;

  bool is_last = (idx == parts.size() - 1);

  for (TreeNodePtr &child : node->children) {
    if (child->name != parts[idx])
      continue;

    if (is_last)
      throw std::runtime_error("Path already exists: " + parts[idx]);

    if (child->mode == TreeNodeType::File)
      throw std::runtime_error("Expected directory but found file: " +
                               parts[idx]);

    insert_at(child.get(), parts, idx + 1, blob_hash);
    return;
  }

  if (is_last) {
    auto file_node = std::make_unique<TreeNode>(TreeNodeType::File, parts[idx]);
    file_node->hash = blob_hash;
    node->children.push_back(std::move(file_node));
    return;
  }

  auto dir_node = std::make_unique<TreeNode>(TreeNodeType::Dir, parts[idx]);
  TreeNode *dir_ptr = dir_node.get();
  node->children.push_back(std::move(dir_node));
  insert_at(dir_ptr, parts, idx + 1, blob_hash);
}

// -------- TreeNode -----------------------
vcs::TreeNode::TreeNode(TreeNodeType type, std::string node_name)
    : mode(type), name(node_name) {}

vcs::Tree::Tree(TreeNodePtr root_node) : root(std::move(root_node)) {
  payload = generate_tree(root);
}

void vcs::TreeNode::insert(FilePath &path, const ByteStream &blob_hash) {
  std::vector<std::string> parts;
  for (const auto &part : path)
    parts.push_back(part.string());

  if (parts.size() == 0)
    throw std::runtime_error("Path is empty");

  if (path.is_absolute())
    throw std::runtime_error("Absolute path provided - must be relative: " +
                             path.string());

  insert_at(this, parts, 0, blob_hash);
}

// --------- Tree --------------------------
vcs::ByteStream vcs::Tree::generate_tree(const TreeNodePtr &node) {
  // writing lambda comparator function
  static auto cmp = [](const TreeNodePtr &a, const TreeNodePtr &b) {
    std::string key_a = a->name + (a->mode == TreeNodeType::Dir ? "/" : "");
    std::string key_b = b->name + (b->mode == TreeNodeType::Dir ? "/" : "");

    return key_a < key_b;
  };

  // checking for duplicate file/dir names
  std::unordered_set<std::string> seen_names;
  for (const TreeNodePtr &child : node.get()->children) {
    if (!seen_names.insert(child->name).second)
      throw std::runtime_error("Duplicate file name in the dir: " +
                               child->name);

    if (child->mode == vcs::TreeNodeType::File) {
      continue;
    } else if (child->mode == vcs::TreeNodeType::Dir) {
      generate_tree(child);
    } else {
      throw std::runtime_error("Encountered invalid Tree Node Type: " +
                               tree_type_to_string(child->mode));
    }
  }

  std::sort(node->children.begin(), node->children.end(), cmp);

  ByteStream content;
  for (const TreeNodePtr &child : node.get()->children) {
    std::string mode_str = tree_type_to_string(child->mode);
    content.insert(content.end(), mode_str.begin(), mode_str.end());
    content.push_back(' ');
    content.insert(content.end(), child->name.begin(), child->name.end());
    content.push_back('\0');
    content.insert(content.end(), child->hash.begin(), child->hash.end());
  }

  ByteStream wrapped_buffer = vcs::wrap(ObjectType::Tree, content);
  node->hash = from_hex(SHA256().scramble(wrapped_buffer));
  objects.push_back(wrapped_buffer);

  return wrapped_buffer;
}
