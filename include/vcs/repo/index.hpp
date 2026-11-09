#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace vcs {

// The staging area: what the next commit will contain. Paths are relative to
// the repository root and always use forward slashes.
class Index {
private:
  std::map<std::string, std::string> entries; // path -> blob hash in hex

public:
  // Throws on a malformed file
  static Index load(const std::filesystem::path &file);
  void save(const std::filesystem::path &file) const;

  // Adding a path replaces anything it conflicts with: a file `a` is dropped
  // when `a/b` is added, and everything under `a/` when the file `a` is added
  void add(const std::string &path, const std::string &hex_hash);

  // Removes `path` and everything under it, returning how many were removed
  size_t remove(const std::string &path);

  // Every path under `dir`. An empty `dir` means the whole repository.
  std::vector<std::string> paths_under(const std::string &dir) const;

  inline const std::map<std::string, std::string> &get_entries() const {
    return entries;
  }
};

} // namespace vcs
