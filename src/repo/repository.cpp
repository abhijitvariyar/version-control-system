#include "vcs/repo/repository.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "vcs/hash/sha256.hpp"
#include "vcs/objects/blob.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/tree.hpp"
#include "vcs/util/file.hpp"

namespace fs = std::filesystem;

namespace {

typedef std::map<std::string, std::string> ConfigMap;

ConfigMap read_config(const fs::path &file) {
  ConfigMap config;

  if (!fs::exists(file))
    return config;

  std::istringstream lines(vcs::read_file(file));
  std::string line;

  while (std::getline(lines, line)) {
    size_t equals = line.find('=');
    if (equals == std::string::npos)
      throw std::runtime_error("Malformed config line: " + line);

    config[line.substr(0, equals)] = line.substr(equals + 1);
  }

  return config;
}

std::string join_path(const std::string &dir, const std::string &name) {
  return dir.empty() ? name : dir + "/" + name;
}

// Every regular file under `dir`, as repository paths, sorted. Ignored
// directories aren't entered, and symlinks are skipped.
std::vector<std::string> list_files(const fs::path &dir,
                                    const std::string &repo_path,
                                    const vcs::IgnoreRules &ignore) {
  std::vector<std::string> files;

  fs::recursive_directory_iterator walker(dir), end;

  for (; walker != end; ++walker) {
    if (walker->path().filename() == ".vcs") {
      walker.disable_recursion_pending();
      continue;
    }

    // symlinks aren't supported, and the walker doesn't follow them
    if (walker->is_symlink())
      continue;

    std::string relative =
        walker->path().lexically_relative(dir).generic_string();
    std::string file_path = join_path(repo_path, relative);

    if (ignore.matches(file_path, walker->is_directory())) {
      walker.disable_recursion_pending();
      continue;
    }

    if (walker->is_regular_file())
      files.push_back(file_path);
  }

  std::sort(files.begin(), files.end());
  return files;
}

std::string hash_file(const fs::path &file) {
  return vcs::SHA256().scramble(vcs::Blob(file).get_blob());
}

void sort_by_path(std::vector<vcs::TreeChange> &changes) {
  std::sort(changes.begin(), changes.end(),
            [](const vcs::TreeChange &x, const vcs::TreeChange &y) {
              return x.path < y.path;
            });
}

// The tree the index describes
vcs::Tree build_tree(const vcs::Index &index) {
  auto root = std::make_unique<vcs::TreeNode>(vcs::TreeNodeType::Dir, "");

  for (const auto &[path, hash] : index.get_entries()) {
    vcs::FilePath file_path(path);
    root->insert(file_path, vcs::from_hex(hash));
  }

  return vcs::Tree(std::move(root));
}

} // namespace

vcs::Repository::Repository(const fs::path &root_in)
    : root(fs::canonical(root_in)), vcs_dir(root / ".vcs"),
      store(vcs_dir / "objects"), refs(vcs_dir) {}

vcs::Repository vcs::Repository::init(const fs::path &root) {
  fs::path vcs_dir = root / ".vcs";

  if (fs::exists(vcs_dir))
    throw std::runtime_error("Repository already exists at " + vcs_dir.string());

  fs::create_directories(vcs_dir / "objects");
  fs::create_directories(vcs_dir / "refs" / "heads");

  write_file_atomic(vcs_dir / "HEAD", "ref: refs/heads/main\n");
  write_file_atomic(vcs_dir / "index", "");

  return Repository(root);
}

vcs::Repository vcs::Repository::discover(const fs::path &start) {
  fs::path dir = fs::absolute(start);

  while (true) {
    if (fs::exists(dir / ".vcs"))
      return Repository(dir);

    fs::path parent = dir.parent_path();
    if (parent == dir)
      break;
    dir = parent;
  }

  throw std::runtime_error("Not a vcs repository (or any parent directory)");
}

// The path relative to the repository root, or "" for the root itself. Only
// the parent is resolved: a symlink has to stay visible as a symlink.
std::string vcs::Repository::to_repo_path(const fs::path &path) const {
  fs::path absolute = fs::absolute(path).lexically_normal();
  if (!absolute.has_filename())
    absolute = absolute.parent_path();

  fs::path resolved =
      fs::weakly_canonical(absolute.parent_path()) / absolute.filename();
  fs::path relative = resolved.lexically_relative(root);

  if (relative.empty() || *relative.begin() == "..")
    throw std::runtime_error("Path is outside the repository: " + path.string());

  if (*relative.begin() == ".vcs")
    throw std::runtime_error("Can't add files inside .vcs: " + path.string());

  return relative == "." ? "" : relative.generic_string();
}

void vcs::Repository::add_file(Index &index, const fs::path &file,
                               const std::string &repo_path) {
  Blob blob(file);
  index.add(repo_path, store.write(blob.get_blob()));
}

void vcs::Repository::add_dir(Index &index, const IgnoreRules &ignore,
                              const fs::path &dir,
                              const std::string &repo_path) {
  std::set<std::string> found;

  for (const std::string &file_path : list_files(dir, repo_path, ignore)) {
    add_file(index, root / file_path, file_path);
    found.insert(file_path);
  }

  // Whatever is tracked but wasn't reached is either gone or was skipped by
  // the ignore rules. Ignoring a file doesn't stop tracking it.
  for (const std::string &tracked : index.paths_under(repo_path)) {
    if (found.contains(tracked))
      continue;

    fs::path on_disk = root / tracked;

    if (fs::is_regular_file(fs::symlink_status(on_disk)))
      add_file(index, on_disk, tracked);
    else
      index.remove(tracked);
  }
}

void vcs::Repository::add(const std::vector<fs::path> &paths) {
  Index index = Index::load(vcs_dir / "index");
  IgnoreRules ignore = IgnoreRules::load(root / ".vcsignore");

  for (const fs::path &path : paths) {
    std::string repo_path = to_repo_path(path);
    fs::path on_disk = root / repo_path;
    fs::file_status status = fs::symlink_status(on_disk);

    if (fs::is_symlink(status))
      throw std::runtime_error("Symlinks aren't supported: " + path.string());

    if (fs::is_directory(status))
      add_dir(index, ignore, on_disk, repo_path);
    else if (fs::is_regular_file(status))
      add_file(index, on_disk, repo_path);
    else if (index.remove(repo_path) == 0)
      throw std::runtime_error("Path not found: " + path.string());
  }

  index.save(vcs_dir / "index");
}

vcs::ParsedCommit vcs::Repository::read_commit(const std::string &hex_hash) const {
  ByteStream buffer = store.read(hex_hash);
  UnwrappedObject object = unwrap(buffer);

  if (object.get_object_type() != ObjectType::Commit)
    throw std::runtime_error("Not a commit: " + hex_hash);

  return parse_commit(object.get_content());
}

std::string vcs::Repository::commit(std::string message,
                                    const std::string &signature) {
  if (message.empty())
    throw std::runtime_error("Commit message is empty");

  if (message.back() != '\n')
    message.push_back('\n');

  Index index = Index::load(vcs_dir / "index");
  std::optional<std::string> parent = refs.head_commit();

  if (!parent && index.get_entries().empty())
    throw std::runtime_error("Nothing to commit");

  Tree tree = build_tree(index);
  std::string tree_hash = to_hex(tree.get_root()->hash);

  std::vector<ByteStream> parents;

  if (parent) {
    if (read_commit(*parent).tree_hash == tree_hash)
      throw std::runtime_error("Nothing to commit");

    parents.push_back(from_hex(*parent));
  }

  // The branch moves last, so a failure part way leaves it on a whole commit
  for (const ByteStream &object : tree.get_objects())
    store.write(object);

  Commit commit(tree.get_root()->hash, parents, signature, signature, message);
  std::string commit_hash = store.write(commit.get_payload());

  refs.update_head(commit_hash);

  return commit_hash;
}

vcs::ByteStream vcs::Repository::read_blob(const std::string &hex_hash) const {
  ByteStream buffer = store.read(hex_hash);
  UnwrappedObject object = unwrap(buffer);

  if (object.get_object_type() != ObjectType::Blob)
    throw std::runtime_error("Not a blob: " + hex_hash);

  return object.get_content();
}

std::optional<std::string> vcs::Repository::head_tree() const {
  std::optional<std::string> head = refs.head_commit();
  if (!head)
    return std::nullopt;

  return read_commit(*head).tree_hash;
}

std::vector<vcs::TreeChange>
vcs::Repository::staged_changes(const Index &index) const {
  // The index's trees exist only in memory: status shouldn't write objects
  Tree tree = build_tree(index);

  std::map<std::string, ByteStream> pending;
  for (const ByteStream &object : tree.get_objects())
    pending[SHA256().scramble(object)] = object;

  TreeReader read_tree = [this, &pending](const std::string &hash) {
    auto found = pending.find(hash);
    ByteStream buffer =
        found != pending.end() ? found->second : store.read(hash);
    UnwrappedObject object = unwrap(buffer);

    if (object.get_object_type() != ObjectType::Tree)
      throw std::runtime_error("Not a tree: " + hash);

    return parse_tree_entries(object.get_content());
  };

  std::vector<TreeChange> changes =
      diff_trees(read_tree, head_tree(), to_hex(tree.get_root()->hash));
  sort_by_path(changes);
  return changes;
}

std::vector<vcs::TreeChange>
vcs::Repository::unstaged_changes(const Index &index) const {
  std::vector<TreeChange> changes;

  for (const auto &[path, hash] : index.get_entries()) {
    fs::path on_disk = root / path;

    if (!fs::is_regular_file(fs::symlink_status(on_disk))) {
      changes.push_back({path, ChangeType::Deleted, hash, ""});
      continue;
    }

    std::string current = hash_file(on_disk);
    if (current != hash)
      changes.push_back({path, ChangeType::Modified, hash, current});
  }

  return changes;
}

vcs::StatusReport vcs::Repository::status() const {
  Index index = Index::load(vcs_dir / "index");
  IgnoreRules ignore = IgnoreRules::load(root / ".vcsignore");

  StatusReport report;
  report.branch = refs.current_branch();
  report.has_commits = refs.head_commit().has_value();
  report.staged = staged_changes(index);
  report.unstaged = unstaged_changes(index);

  // every directory that holds a tracked file
  std::set<std::string> tracked_dirs;
  for (const auto &[path, hash] : index.get_entries())
    for (size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1))
      tracked_dirs.insert(path.substr(0, slash));

  std::set<std::string> untracked;

  for (const std::string &file : list_files(root, "", ignore)) {
    if (index.get_entries().contains(file))
      continue;

    // show the highest directory with nothing tracked in it, not every file
    std::string shown = file;
    for (size_t slash = file.find('/'); slash != std::string::npos;
         slash = file.find('/', slash + 1)) {
      std::string dir = file.substr(0, slash);
      if (!tracked_dirs.contains(dir)) {
        shown = dir + "/";
        break;
      }
    }

    untracked.insert(shown);
  }

  report.untracked.assign(untracked.begin(), untracked.end());
  return report;
}

std::vector<vcs::FileChange> vcs::Repository::diff(bool staged) const {
  Index index = Index::load(vcs_dir / "index");

  std::vector<TreeChange> changes =
      staged ? staged_changes(index) : unstaged_changes(index);

  std::vector<FileChange> files;

  for (const TreeChange &change : changes) {
    FileChange file{change.path, std::nullopt, std::nullopt};

    if (!change.old_hash.empty())
      file.old_content = read_blob(change.old_hash);

    if (!change.new_hash.empty() && staged) {
      file.new_content = read_blob(change.new_hash);
    } else if (!change.new_hash.empty()) {
      std::string text = read_file(root / change.path);
      file.new_content = ByteStream(text.begin(), text.end());
    }

    files.push_back(std::move(file));
  }

  return files;
}

std::vector<vcs::LogEntry> vcs::Repository::log() const {
  std::vector<LogEntry> entries;
  std::optional<std::string> hash = refs.head_commit();

  while (hash) {
    ParsedCommit commit = read_commit(*hash);
    std::optional<std::string> next;

    if (!commit.parent_hashes.empty())
      next = commit.parent_hashes.front();

    entries.push_back({*hash, std::move(commit)});
    hash = std::move(next);
  }

  return entries;
}

std::optional<std::string>
vcs::Repository::get_config(const std::string &key) const {
  ConfigMap config = read_config(vcs_dir / "config");

  auto entry = config.find(key);
  if (entry == config.end())
    return std::nullopt;

  return entry->second;
}

void vcs::Repository::set_config(const std::string &key,
                                 const std::string &value) {
  if (key.empty() || key.find_first_of("=\n") != std::string::npos)
    throw std::runtime_error("Invalid config key: " + key);

  // a newline would also let a name inject extra lines into a commit header
  if (value.find('\n') != std::string::npos)
    throw std::runtime_error("Config value can't contain a newline");

  ConfigMap config = read_config(vcs_dir / "config");
  config[key] = value;

  std::string content;
  for (const auto &[name, setting] : config)
    content += name + "=" + setting + "\n";

  write_file_atomic(vcs_dir / "config", content);
}
