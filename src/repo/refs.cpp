#include "vcs/repo/refs.hpp"

#include <stdexcept>

#include "vcs/util/file.hpp"

namespace {

const std::string REF_PREFIX = "ref: ";

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

vcs::Refs::Refs(std::filesystem::path vcs_dir_in)
    : vcs_dir(std::move(vcs_dir_in)) {}

std::optional<std::string> vcs::Refs::head_ref() const {
  std::string head = strip_newlines(read_file(vcs_dir / "HEAD"));

  if (!head.starts_with(REF_PREFIX))
    return std::nullopt;

  std::string ref = head.substr(REF_PREFIX.size());

  if (!ref.starts_with("refs/heads/") || ref.find("..") != std::string::npos)
    throw std::runtime_error("Invalid ref in HEAD: " + ref);

  return ref;
}

std::optional<std::string> vcs::Refs::current_branch() const {
  std::optional<std::string> ref = head_ref();
  if (!ref)
    return std::nullopt;

  return ref->substr(std::string("refs/heads/").size());
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
