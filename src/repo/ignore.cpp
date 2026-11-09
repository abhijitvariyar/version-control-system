#include "vcs/repo/ignore.hpp"

#include <sstream>
#include <stdexcept>

#include "vcs/util/file.hpp"

namespace {

std::vector<std::string> split_path(const std::string &path) {
  std::vector<std::string> parts;
  std::istringstream stream(path);
  std::string part;

  while (std::getline(stream, part, '/'))
    parts.push_back(part);

  return parts;
}

// Matches one path component. When a character doesn't fit, it goes back to
// the last `*` and lets it take one more character, so there is no recursion.
bool glob_match(const std::string &pattern, const std::string &text) {
  size_t p = 0;
  size_t t = 0;
  size_t star = std::string::npos;
  size_t star_text = 0;

  while (t < text.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      star_text = t;
    } else if (p < pattern.size() &&
               (pattern[p] == '?' || pattern[p] == text[t])) {
      p++;
      t++;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++star_text;
    } else {
      return false;
    }
  }

  while (p < pattern.size() && pattern[p] == '*')
    p++;

  return p == pattern.size();
}

} // namespace

vcs::IgnoreRules vcs::IgnoreRules::load(const std::filesystem::path &file) {
  if (!std::filesystem::exists(file))
    return IgnoreRules();

  return parse(read_file(file));
}

vcs::IgnoreRules vcs::IgnoreRules::parse(const std::string &text) {
  IgnoreRules rules;

  std::istringstream lines(text);
  std::string line;

  while (std::getline(lines, line)) {
    // trailing whitespace is never meant, and \r comes from Windows line ends
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r'))
      line.pop_back();

    if (line.empty() || line.front() == '#')
      continue;

    if (line.front() == '!')
      throw std::runtime_error("Negated ignore patterns aren't supported: " +
                               line);

    Pattern pattern;
    std::string body = line;

    pattern.dir_only = body.back() == '/';
    if (pattern.dir_only)
      body.pop_back();

    pattern.anchored = body.find('/') != std::string::npos;
    if (!body.empty() && body.front() == '/')
      body.erase(0, 1);

    if (body.empty())
      throw std::runtime_error("Invalid ignore pattern: " + line);

    pattern.parts = split_path(body);
    rules.patterns.push_back(std::move(pattern));
  }

  return rules;
}

bool vcs::IgnoreRules::matches(const std::string &repo_path,
                               bool is_dir) const {
  std::vector<std::string> path = split_path(repo_path);

  for (const Pattern &pattern : patterns) {
    if (pattern.dir_only && !is_dir)
      continue;

    if (!pattern.anchored) {
      if (glob_match(pattern.parts.front(), path.back()))
        return true;
      continue;
    }

    if (pattern.parts.size() != path.size())
      continue;

    bool all_match = true;
    for (size_t i = 0; i < path.size() && all_match; i++)
      all_match = glob_match(pattern.parts[i], path[i]);

    if (all_match)
      return true;
  }

  return false;
}
