#include "vcs/repo/refs.hpp"

#include <algorithm>
#include <stdexcept>

#include "vcs/util/file.hpp"

namespace {

const std::string REF_PREFIX = "ref: ";
const std::string HEADS_PREFIX = "refs/heads/";

std::string strip_newlines(std::string text) {
  while (!text.empty() && text.back() == '\n')
    text.pop_back();
  return text;
}

std::string validated_hash(const std::string &hash) {
  bool valid = hash.size() == 64;

  for (char chr : hash)
    if (!((chr >= '0' && chr <= '9') || (chr >= 'a' && chr <= 'f')))
      valid = false;

  if (!valid)
    throw std::runtime_error("Invalid commit hash in ref: " + hash);

  return hash;
}

} // namespace

void vcs::validate_branch_name(const std::string &name) {
  auto fail = [&name]() {
    throw std::runtime_error("Invalid branch name: '" + name + "'");
  };

  if (name.empty() || name.front() == '-' || name.back() == '.' ||
      name == "HEAD" || name.find("..") != std::string::npos ||
      name.find("@{") != std::string::npos)
    fail();

  for (unsigned char chr : name)
    if (chr <= ' ' || chr == 0x7f ||
        std::string("~^:?*[\\").find(static_cast<char>(chr)) != std::string::npos)
      fail();

  size_t start = 0;
  while (start <= name.size()) {
    size_t end = name.find('/', start);
    if (end == std::string::npos)
      end = name.size();

    std::string part = name.substr(start, end - start);
    if (part.empty() || part.front() == '.' || part.ends_with(".lock"))
      fail();

    start = end + 1;
  }
}

vcs::Refs::Refs(std::filesystem::path vcs_dir_in)
    : vcs_dir(std::move(vcs_dir_in)) {}

std::optional<std::string> vcs::Refs::head_ref() const {
  std::string head = strip_newlines(read_file(vcs_dir / "HEAD"));

  if (!head.starts_with(REF_PREFIX))
    return std::nullopt;

  std::string ref = head.substr(REF_PREFIX.size());

  if (!ref.starts_with(HEADS_PREFIX))
    throw std::runtime_error("Invalid ref in HEAD: " + ref);

  validate_branch_name(ref.substr(HEADS_PREFIX.size()));

  return ref;
}

std::optional<std::string> vcs::Refs::current_branch() const {
  std::optional<std::string> ref = head_ref();
  if (!ref)
    return std::nullopt;

  return ref->substr(HEADS_PREFIX.size());
}

std::optional<std::string> vcs::Refs::head_commit() const {
  std::optional<std::string> ref = head_ref();

  if (!ref)
    return validated_hash(strip_newlines(read_file(vcs_dir / "HEAD")));

  std::filesystem::path ref_path = vcs_dir / *ref;

  if (!std::filesystem::exists(ref_path))
    return std::nullopt;

  return validated_hash(strip_newlines(read_file(ref_path)));
}

void vcs::Refs::update_head(const std::string &hex_hash) {
  validated_hash(hex_hash);

  std::optional<std::string> ref = head_ref();
  std::filesystem::path path = vcs_dir / (ref ? *ref : "HEAD");

  std::filesystem::create_directories(path.parent_path());
  write_file_atomic(path, hex_hash + "\n");
}

std::vector<std::string> vcs::Refs::branches() const {
  std::vector<std::string> names;
  std::filesystem::path heads = vcs_dir / "refs" / "heads";

  if (!std::filesystem::exists(heads))
    return names;

  for (const auto &entry : std::filesystem::recursive_directory_iterator(heads))
    if (entry.is_regular_file())
      names.push_back(entry.path().lexically_relative(heads).generic_string());

  std::sort(names.begin(), names.end());
  return names;
}

std::optional<std::string>
vcs::Refs::branch_commit(const std::string &name) const {
  try {
    validate_branch_name(name);
  } catch (const std::runtime_error &) {
    return std::nullopt;
  }

  std::filesystem::path path = vcs_dir / "refs" / "heads" / name;
  if (!std::filesystem::is_regular_file(path))
    return std::nullopt;

  return validated_hash(strip_newlines(read_file(path)));
}

void vcs::Refs::create_branch(const std::string &name,
                              const std::string &hex_hash) {
  validate_branch_name(name);
  validated_hash(hex_hash);

  std::filesystem::path heads = vcs_dir / "refs" / "heads";
  std::filesystem::path path = heads / name;

  if (std::filesystem::exists(path))
    throw std::runtime_error("A branch named '" + name + "' already exists");

  for (size_t slash = name.find('/'); slash != std::string::npos;
       slash = name.find('/', slash + 1))
    if (std::filesystem::is_regular_file(heads / name.substr(0, slash)))
      throw std::runtime_error("Branch '" + name + "' clashes with branch '" +
                               name.substr(0, slash) + "'");

  std::filesystem::create_directories(path.parent_path());
  write_file_atomic(path, hex_hash + "\n");
}

void vcs::Refs::point_head_at_branch(const std::string &name) {
  validate_branch_name(name);
  write_file_atomic(vcs_dir / "HEAD", REF_PREFIX + HEADS_PREFIX + name + "\n");
}

void vcs::Refs::detach_head(const std::string &hex_hash) {
  validated_hash(hex_hash);
  write_file_atomic(vcs_dir / "HEAD", hex_hash + "\n");
}
