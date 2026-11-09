#include "vcs/repo/index.hpp"

#include <sstream>
#include <stdexcept>

#include "vcs/objects/tree.hpp"
#include "vcs/util/file.hpp"

namespace {

constexpr size_t HASH_LENGTH = 64;

// One line per entry: <mode> <hash> <path>
const std::string FILE_MODE = vcs::tree_type_to_string(vcs::TreeNodeType::File);

// The index decides which files get written during a checkout, so a path that
// could point outside the repository is refused here
void validate_path(const std::string &path) {
  if (path.empty() || path.front() == '/' ||
      path.find('\n') != std::string::npos)
    throw std::runtime_error("Invalid path in index: " + path);

  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos)
      end = path.size();

    std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part == ".vcs")
      throw std::runtime_error("Invalid path in index: " + path);

    start = end + 1;
  }
}

void validate_hash(const std::string &hash) {
  if (hash.size() != HASH_LENGTH)
    throw std::runtime_error("Invalid hash in index: " + hash);

  for (char chr : hash)
    if (!((chr >= '0' && chr <= '9') || (chr >= 'a' && chr <= 'f')))
      throw std::runtime_error("Invalid hash in index: " + hash);
}

bool is_under(const std::string &path, const std::string &dir) {
  return dir.empty() || path.starts_with(dir + "/");
}

} // namespace

vcs::Index vcs::Index::load(const std::filesystem::path &file) {
  Index index;

  std::istringstream lines(read_file(file));
  std::string line;

  while (std::getline(lines, line)) {
    size_t mode_end = line.find(' ');
    if (mode_end == std::string::npos || line.substr(0, mode_end) != FILE_MODE)
      throw std::runtime_error("Malformed index line: " + line);

    size_t hash_start = mode_end + 1;
    size_t hash_end = line.find(' ', hash_start);
    if (hash_end == std::string::npos)
      throw std::runtime_error("Malformed index line: " + line);

    // the path is everything left, so it can contain spaces
    index.add(line.substr(hash_end + 1),
              line.substr(hash_start, hash_end - hash_start));
  }

  return index;
}

void vcs::Index::save(const std::filesystem::path &file) const {
  std::string content;

  for (const auto &[path, hash] : entries)
    content += FILE_MODE + " " + hash + " " + path + "\n";

  write_file_atomic(file, content);
}

void vcs::Index::add(const std::string &path, const std::string &hex_hash) {
  validate_path(path);
  validate_hash(hex_hash);

  for (size_t slash = path.find('/'); slash != std::string::npos;
       slash = path.find('/', slash + 1))
    entries.erase(path.substr(0, slash));

  for (const std::string &child : paths_under(path))
    entries.erase(child);

  entries[path] = hex_hash;
}

size_t vcs::Index::remove(const std::string &path) {
  size_t removed = entries.erase(path);

  for (const std::string &child : paths_under(path))
    removed += entries.erase(child);

  return removed;
}

std::vector<std::string> vcs::Index::paths_under(const std::string &dir) const {
  std::vector<std::string> paths;

  for (const auto &[path, hash] : entries)
    if (is_under(path, dir))
      paths.push_back(path);

  return paths;
}
