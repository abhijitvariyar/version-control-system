#include "vcs/objects/commit.hpp"

#include <stdexcept>

#include "vcs/objects/object.hpp"
#include "vcs/util/bytes.hpp"

vcs::ParsedCommit vcs::parse_commit(const ByteStream &content) {
  std::string text(content.begin(), content.end());
  ParsedCommit commit;

  size_t pos = 0;

  while (true) {
    size_t line_end = text.find('\n', pos);
    if (line_end == std::string::npos)
      throw std::runtime_error("Malformed commit: missing blank line");

    // the blank line ends the headers; the rest is the message
    if (line_end == pos) {
      commit.message = text.substr(pos + 1);
      break;
    }

    std::string line = text.substr(pos, line_end - pos);
    pos = line_end + 1;

    size_t space = line.find(' ');
    if (space == std::string::npos)
      throw std::runtime_error("Malformed commit header: " + line);

    std::string key = line.substr(0, space);
    std::string value = line.substr(space + 1);

    if (key == "tree")
      commit.tree_hash = value;
    else if (key == "parent")
      commit.parent_hashes.push_back(value);
    else if (key == "author")
      commit.author = value;
    else if (key == "committer")
      commit.committer = value;
    else
      throw std::runtime_error("Unknown commit header: " + key);
  }

  if (commit.tree_hash.empty() || commit.author.empty() ||
      commit.committer.empty())
    throw std::runtime_error("Malformed commit: missing a required header");

  return commit;
}

vcs::Commit::Commit(ByteStream tree_hash_in,
                    std::vector<ByteStream> parent_hashes_in,
                    std::string author_in, std::string committer_in,
                    std::string message_in)
    : tree_hash(std::move(tree_hash_in)),
      parent_hashes(std::move(parent_hashes_in)),
      author(std::move(author_in)), committer(std::move(committer_in)),
      message(std::move(message_in)) {
  generate_commit();
}

void vcs::Commit::generate_commit() {
  if (tree_hash.size() != 32)
    throw std::runtime_error("Commit tree hash must be 32 bytes");

  if (parent_hashes.size() > 2)
    throw std::runtime_error("Commit cannot have more than 2 parents");

  for (const ByteStream &parent_hash : parent_hashes) {
    if (parent_hash.size() != 32)
      throw std::runtime_error("Commit parent hash must be 32 bytes");
  }

  std::string content;

  content += "tree " + to_hex(tree_hash) + "\n";

  for (const ByteStream &parent_hash : parent_hashes)
    content += "parent " + to_hex(parent_hash) + "\n";

  content += "author " + author + "\n";
  content += "committer " + committer + "\n";
  content += "\n";
  content += message;

  ByteStream content_bytes(content.begin(), content.end());

  payload = vcs::wrap(ObjectType::Commit, content_bytes);
}
