#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vcs/util/bytes.hpp"

namespace vcs {

enum class TreeNodeType : uint32_t { File = 0100644, Dir = 040000 };

// Forward declaring TreeNode to typedef long types
struct TreeNode;

typedef std::unique_ptr<TreeNode> TreeNodePtr;
typedef std::vector<TreeNodePtr> TreeNodeList;

typedef std::vector<std::string> StringVec;

inline std::string tree_type_to_string(TreeNodeType type) {
  switch (type) {
  case vcs::TreeNodeType::File:
    return "100644";
  case vcs::TreeNodeType::Dir:
    return "40000";
  }

  throw std::runtime_error("Invalid Tree Node Type");
}

inline TreeNodeType tree_type_from_str(std::string_view type) {
  if (type == "100644")
    return TreeNodeType::File;
  else if (type == "40000")
    return TreeNodeType::Dir;
  else
    throw std::runtime_error("Invalid Tree Node Type");
}

struct TreeNode {
  TreeNodeType mode;
  std::string name;
  ByteStream hash;
  TreeNodeList children;

  TreeNode(TreeNodeType, std::string);
  void insert(FilePath &path, const ByteStream &blob_hash);
};

// Parses one level of a tree object's payload (the bytes between a tree
// object's header and its end) into flat entries. Does not recurse into
// subdirectories - each returned entry's own `children` stays empty.
std::vector<TreeNode> parse_tree_entries(const ByteStream &payload);

class Tree {
private:
  TreeNodePtr root;
  ByteStream payload;
  std::vector<ByteStream> objects; // every level, subtrees before their parent

  ByteStream generate_tree(const TreeNodePtr &node);

public:
  Tree(TreeNodePtr);

  inline const ByteStream &get_payload() { return payload; };
  inline TreeNode *get_root() { return root.get(); };

  // One wrapped tree object per directory, the root last
  inline const std::vector<ByteStream> &get_objects() { return objects; };
};

} // namespace vcs
