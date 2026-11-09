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

// Reads trees from the store, or from `pending` first: trees that exist only
// in memory, such as the index's
vcs::TreeReader vcs::Repository::tree_reader(
    const std::map<std::string, ByteStream> *pending) const {
  return [this, pending](const std::string &hash) {
    bool in_memory = pending && pending->contains(hash);
    ByteStream buffer = in_memory ? pending->at(hash) : store.read(hash);

    UnwrappedObject object = unwrap(buffer);

    if (object.get_object_type() != ObjectType::Tree)
      throw std::runtime_error("Not a tree: " + hash);

    return parse_tree_entries(object.get_content());
  };
}

std::vector<vcs::TreeChange>
vcs::Repository::staged_changes(const Index &index) const {
  // The index's trees exist only in memory: status shouldn't write objects
  Tree tree = build_tree(index);

  std::map<std::string, ByteStream> pending;
  for (const ByteStream &object : tree.get_objects())
    pending[SHA256().scramble(object)] = object;

  std::vector<TreeChange> changes = diff_trees(
      tree_reader(&pending), head_tree(), to_hex(tree.get_root()->hash));
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
  report.head = refs.head_commit();
  report.has_commits = report.head.has_value();
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

// -------- branches and checkout --------

std::optional<std::string> vcs::Repository::head_commit() const {
  return refs.head_commit();
}

std::optional<std::string> vcs::Repository::current_branch() const {
  return refs.current_branch();
}

std::vector<std::string> vcs::Repository::branches() const {
  return refs.branches();
}

void vcs::Repository::create_branch(const std::string &name) {
  std::optional<std::string> head = refs.head_commit();
  if (!head)
    throw std::runtime_error("No commits yet, so there is nothing to branch from");

  refs.create_branch(name, *head);
}

// A full hash, or a prefix of at least 4 that matches exactly one commit
std::string vcs::Repository::resolve_commit(const std::string &spec) const {
  bool is_hex = spec.size() >= 4 && spec.size() <= 64;
  for (char chr : spec)
    if (!((chr >= '0' && chr <= '9') || (chr >= 'a' && chr <= 'f')))
      is_hex = false;

  if (!is_hex)
    throw std::runtime_error("Not a branch or commit: " + spec);

  std::vector<std::string> candidates;
  fs::path shard = vcs_dir / "objects" / spec.substr(0, 2);

  if (fs::is_directory(shard))
    for (const auto &entry : fs::directory_iterator(shard)) {
      std::string hash = spec.substr(0, 2) + entry.path().filename().string();
      if (hash.size() == 64 && hash.starts_with(spec))
        candidates.push_back(hash);
    }

  // only commits count: a blob or tree with the same prefix isn't ambiguous
  std::vector<std::string> commits;
  for (const std::string &hash : candidates) {
    ByteStream buffer = store.read(hash);
    if (unwrap(buffer).get_object_type() == ObjectType::Commit)
      commits.push_back(hash);
  }

  if (commits.empty())
    throw std::runtime_error("Not a branch or commit: " + spec);

  if (commits.size() > 1)
    throw std::runtime_error("Commit prefix is ambiguous: " + spec);

  return commits.front();
}

namespace {

// Removes the parents of `path` that are left empty, stopping at the root
void remove_empty_parents(const fs::path &root, const std::string &path) {
  fs::path dir = fs::path(path).parent_path();

  while (!dir.empty()) {
    fs::path on_disk = root / dir;
    if (!fs::is_directory(fs::symlink_status(on_disk)) || !fs::is_empty(on_disk))
      return;

    fs::remove(on_disk);
    dir = dir.parent_path();
  }
}

std::string list_lines(const std::vector<std::string> &paths) {
  std::string out;
  for (const std::string &path : paths)
    out += "\n\t" + path;
  return out;
}

} // namespace

vcs::CheckoutResult vcs::Repository::checkout(const std::string &target) {
  std::optional<std::string> branch;
  std::string commit_hash;

  if (std::optional<std::string> tip = refs.branch_commit(target)) {
    branch = target;
    commit_hash = *tip;
  } else {
    commit_hash = resolve_commit(target);
  }

  if (branch && branch == refs.current_branch())
    return {branch, commit_hash, true};

  std::string target_tree = read_commit(commit_hash).tree_hash;
  Index index = Index::load(vcs_dir / "index");

  std::vector<TreeChange> changes =
      diff_trees(tree_reader(nullptr), head_tree(), target_tree);

  // The new index comes first. Index::add refuses paths like "../x" or
  // ".vcs/HEAD", so a tree that could write outside the repository fails here,
  // before anything on disk is touched.
  Index updated = index;
  std::set<std::string> deleting;

  for (const TreeChange &change : changes) {
    if (change.type == ChangeType::Deleted) {
      updated.remove(change.path);
      deleting.insert(change.path);
    } else {
      updated.add(change.path, change.new_hash);
    }
  }

  // Uncommitted work on a path the checkout changes would be lost
  std::set<std::string> dirty;
  for (const TreeChange &change : staged_changes(index))
    dirty.insert(change.path);
  for (const TreeChange &change : unstaged_changes(index))
    dirty.insert(change.path);

  std::vector<std::string> local_changes;
  for (const TreeChange &change : changes)
    if (dirty.contains(change.path))
      local_changes.push_back(change.path);

  if (!local_changes.empty())
    throw std::runtime_error(
        "Your local changes to these files would be overwritten by checkout:" +
        list_lines(local_changes) + "\nCommit them or undo them first.");

  // So would anything untracked where a file has to be written
  std::set<std::string> in_the_way;

  for (const TreeChange &change : changes) {
    if (change.type == ChangeType::Deleted)
      continue;

    // a parent directory that is currently a file
    for (size_t slash = change.path.find('/'); slash != std::string::npos;
         slash = change.path.find('/', slash + 1)) {
      std::string parent = change.path.substr(0, slash);
      fs::file_status status = fs::symlink_status(root / parent);

      if (fs::exists(status) && !fs::is_directory(status) &&
          !deleting.contains(parent))
        in_the_way.insert(parent);
    }

    fs::path on_disk = root / change.path;
    fs::file_status status = fs::symlink_status(on_disk);

    if (!fs::exists(status))
      continue;

    if (!fs::is_directory(status)) {
      if (!index.get_entries().contains(change.path))
        in_the_way.insert(change.path);
      continue;
    }

    // a directory where the file goes: fine only if checkout empties it
    for (const auto &entry : fs::recursive_directory_iterator(on_disk)) {
      if (entry.is_directory() && !entry.is_symlink())
        continue;

      std::string inside = (fs::path(change.path) /
                            entry.path().lexically_relative(on_disk))
                               .generic_string();
      if (!deleting.contains(inside))
        in_the_way.insert(inside);
    }
  }

  if (!in_the_way.empty())
    throw std::runtime_error(
        "These untracked files would be overwritten by checkout:" +
        list_lines({in_the_way.begin(), in_the_way.end()}) +
        "\nMove or remove them first.");

  for (const TreeChange &change : changes)
    if (change.type != ChangeType::Deleted && !store.exists(change.new_hash))
      throw std::runtime_error("Missing object " + change.new_hash + " for " +
                               change.path);

  // Every check has passed. Deletions go first, so files can replace
  // directories and the other way round.
  for (const TreeChange &change : changes) {
    if (change.type != ChangeType::Deleted)
      continue;

    fs::remove(root / change.path);
    remove_empty_parents(root, change.path);
  }

  for (const TreeChange &change : changes) {
    if (change.type == ChangeType::Deleted)
      continue;

    fs::path on_disk = root / change.path;

    // only empty directories can be left here, after the checks above
    if (fs::is_directory(fs::symlink_status(on_disk)))
      fs::remove_all(on_disk);

    fs::create_directories(on_disk.parent_path());

    ByteStream content = read_blob(change.new_hash);
    write_file_atomic(on_disk, std::string(content.begin(), content.end()));
  }

  updated.save(vcs_dir / "index");

  if (branch)
    refs.point_head_at_branch(*branch);
  else
    refs.detach_head(commit_hash);

  return {branch, commit_hash, false};
}
