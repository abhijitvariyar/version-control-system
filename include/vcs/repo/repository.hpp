#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vcs/diff/tree_diff.hpp"
#include "vcs/objects/commit.hpp"
#include "vcs/objects/object_store.hpp"
#include "vcs/repo/ignore.hpp"
#include "vcs/repo/index.hpp"
#include "vcs/repo/refs.hpp"

namespace vcs {

struct LogEntry {
  std::string hash;
  ParsedCommit commit;
};

struct StatusReport {
  std::optional<std::string> branch; // empty when HEAD is detached
  bool has_commits;
  std::vector<TreeChange> staged;    // HEAD -> index
  std::vector<TreeChange> unstaged;  // index -> working files
  std::vector<std::string> untracked; // a directory with nothing tracked in it
                                      // is listed once, ending in "/"
};

// One file's contents on each side of a change. An empty side means the file
// doesn't exist there.
struct FileChange {
  std::string path;
  std::optional<ByteStream> old_content;
  std::optional<ByteStream> new_content;
};

class Repository {
private:
  std::filesystem::path root;
  std::filesystem::path vcs_dir;
  ObjectStore store;
  Refs refs;

  std::string to_repo_path(const std::filesystem::path &path) const;
  void add_file(Index &index, const std::filesystem::path &file,
                const std::string &repo_path);
  void add_dir(Index &index, const IgnoreRules &ignore,
               const std::filesystem::path &dir, const std::string &repo_path);
  ParsedCommit read_commit(const std::string &hex_hash) const;
  ByteStream read_blob(const std::string &hex_hash) const;
  std::optional<std::string> head_tree() const;
  std::vector<TreeChange> staged_changes(const Index &index) const;
  std::vector<TreeChange> unstaged_changes(const Index &index) const;

public:
  // `root` is the directory that holds .vcs
  explicit Repository(const std::filesystem::path &root);

  static Repository init(const std::filesystem::path &root);

  // Walks up from `start` looking for a .vcs directory
  static Repository discover(const std::filesystem::path &start);

  inline ObjectStore &objects() { return store; }

  // Stages files and directories. A tracked path that no longer exists is
  // staged as a deletion. Walking a directory skips what .vcsignore matches,
  // unless it is already tracked; a file named directly is always added. The
  // index is only saved if every path succeeds.
  void add(const std::vector<std::filesystem::path> &paths);

  // `signature` is "Name <email> <unix time> <+hhmm>". Returns the new hash.
  std::string commit(std::string message, const std::string &signature);

  // Newest first, following the first parent of each commit
  std::vector<LogEntry> log() const;

  // Compares HEAD, the index and the working files. Paths are sorted.
  StatusReport status() const;

  // With `staged`, HEAD against the index; without, the index against the
  // working files. Sorted by path.
  std::vector<FileChange> diff(bool staged) const;

  std::optional<std::string> get_config(const std::string &key) const;
  void set_config(const std::string &key, const std::string &value);
};

} // namespace vcs
