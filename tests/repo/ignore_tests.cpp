#include "../test_harness.hpp"

#include "vcs/repo/ignore.hpp"

namespace {

bool file_ignored(const std::string &rules, const std::string &path) {
  return vcs::IgnoreRules::parse(rules).matches(path, false);
}

bool dir_ignored(const std::string &rules, const std::string &path) {
  return vcs::IgnoreRules::parse(rules).matches(path, true);
}

} // namespace

TEST(ignore, no_rules_match_nothing) {
  CHECK(!file_ignored("", "a.txt"));
}

TEST(ignore, missing_file_means_no_rules) {
  test::TempDir dir;
  CHECK(!vcs::IgnoreRules::load(dir / ".vcsignore").matches("a", false));
}

TEST(ignore, load_reads_the_file) {
  test::TempDir dir;
  test::write_bytes(dir / ".vcsignore", test::bytes("a.txt\n"));
  CHECK(vcs::IgnoreRules::load(dir / ".vcsignore").matches("a.txt", false));
}

TEST(ignore, plain_name_matches_at_any_depth) {
  CHECK(file_ignored("a.log", "a.log"));
  CHECK(file_ignored("a.log", "x/y/a.log"));
  CHECK(dir_ignored("cache", "x/cache"));
  CHECK(!file_ignored("a.log", "b.log"));
  CHECK(!file_ignored("a.log", "a.log.txt"));
}

TEST(ignore, comments_and_blank_lines_are_skipped) {
  CHECK(!file_ignored("# a.txt\n\n   \n", "a.txt"));
  CHECK(!file_ignored("# a.txt\n", "# a.txt"));
}

TEST(ignore, trailing_whitespace_is_dropped) {
  CHECK(file_ignored("a.txt  \n", "a.txt"));
  CHECK(file_ignored("a.txt\r\n", "a.txt"));
}

TEST(ignore, last_line_needs_no_newline) {
  CHECK(file_ignored("a.txt\nb.txt", "b.txt"));
}

TEST(ignore, trailing_slash_is_directories_only) {
  CHECK(dir_ignored("bin/", "bin"));
  CHECK(dir_ignored("bin/", "src/bin"));
  CHECK(!file_ignored("bin/", "bin"));
}

TEST(ignore, leading_slash_anchors_to_root) {
  CHECK(file_ignored("/a.txt", "a.txt"));
  CHECK(!file_ignored("/a.txt", "src/a.txt"));
}

TEST(ignore, middle_slash_anchors_to_root) {
  CHECK(file_ignored("src/a.txt", "src/a.txt"));
  CHECK(!file_ignored("src/a.txt", "x/src/a.txt"));
  CHECK(!file_ignored("src/a.txt", "a.txt"));
}

TEST(ignore, anchored_directory) {
  CHECK(dir_ignored("/build/", "build"));
  CHECK(!dir_ignored("/build/", "src/build"));
  CHECK(!file_ignored("/build/", "build"));
}

TEST(ignore, star_matches_within_a_component) {
  CHECK(file_ignored("*.o", "a.o"));
  CHECK(file_ignored("*.o", "x/y/a.o"));
  CHECK(file_ignored("*.o", ".o"));
  CHECK(file_ignored("a*c", "abbbc"));
  CHECK(file_ignored("*", "anything"));
  CHECK(!file_ignored("*.o", "a.oo"));
  CHECK(!file_ignored("src/*.o", "src/x/a.o"));
  CHECK(file_ignored("src/*.o", "src/a.o"));
  CHECK(file_ignored("*/a.txt", "src/a.txt"));
}

TEST(ignore, star_backtracks) {
  CHECK(file_ignored("*a*b", "xaxaxb"));
  CHECK(!file_ignored("*a*b", "xaxaxc"));
}

TEST(ignore, question_mark_matches_one_character) {
  CHECK(file_ignored("a?c", "abc"));
  CHECK(!file_ignored("a?c", "ac"));
  CHECK(!file_ignored("a?c", "abbc"));
}

TEST(ignore, long_star_pattern_is_fast) {
  // quadratic at worst, where a naive recursive matcher is exponential
  std::string rule = "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b";
  CHECK(!file_ignored(rule, std::string(2000, 'a')));
}

TEST(ignore, any_rule_can_match) {
  CHECK(file_ignored("x\ny\nz", "y"));
}

TEST(ignore, negation_is_refused) {
  CHECK_THROWS(vcs::IgnoreRules::parse("!keep.txt\n"));
}

TEST(ignore, bare_slash_is_refused) {
  CHECK_THROWS(vcs::IgnoreRules::parse("/\n"));
}
