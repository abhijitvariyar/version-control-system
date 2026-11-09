#pragma once

#include <string>
#include <vector>

#include "vcs/util/bytes.hpp"

namespace vcs {

// The fields of a commit object, with hashes in hex
struct ParsedCommit {
  std::string tree_hash;
  std::vector<std::string> parent_hashes;
  std::string author;
  std::string committer;
  std::string message;
};

// Parses a commit object's content (the bytes after its header)
ParsedCommit parse_commit(const ByteStream &content);

class Commit {
private:
  ByteStream tree_hash;
  std::vector<ByteStream> parent_hashes;
  std::string author;
  std::string committer;
  std::string message;
  ByteStream payload;

  void generate_commit();

public:
  Commit(ByteStream tree_hash, std::vector<ByteStream> parent_hashes,
         std::string author, std::string committer, std::string message);

  inline const ByteStream &get_payload() { return payload; }
};

} // namespace vcs
