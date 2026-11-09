#include "../test_harness.hpp"

#include "vcs/diff/myers.hpp"
#include "vcs/diff/unified.hpp"

// Expected hunks come from git on the same two files:
//   git diff --no-index old new
// with the header lines above the first @@ removed. git also appends the
// nearest line above a hunk after its @@ (`@@ -2,0 +3 @@ b`); vcs leaves that
// out.

namespace {

std::string hunks(const std::string &old_text, const std::string &new_text,
                  size_t context = 3) {
  return vcs::unified_hunks(vcs::split_lines(old_text),
                            vcs::split_lines(new_text), context);
}

const std::string TEN = "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n";

} // namespace

TEST(unified, no_changes_is_empty) {
  CHECK_EQ(hunks(TEN, TEN), std::string());
  CHECK_EQ(hunks("", ""), std::string());
}

TEST(unified, change_in_the_middle) {
  CHECK_EQ(hunks(TEN, "1\n2\n3\n4\nFIVE\n6\n7\n8\n9\n10\n"),
           std::string("@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+FIVE\n 6\n 7\n 8\n"));
}

TEST(unified, changes_far_apart_get_separate_hunks) {
  std::string old_text = "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n";
  std::string new_text =
      "ONE\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\nSIXTEEN\n";
  CHECK_EQ(hunks(old_text, new_text),
           std::string("@@ -1,4 +1,4 @@\n-1\n+ONE\n 2\n 3\n 4\n"
                       "@@ -13,4 +13,4 @@\n 13\n 14\n 15\n-16\n+SIXTEEN\n"));
}

TEST(unified, gap_of_six_shares_a_hunk) {
  CHECK_EQ(hunks(TEN, "X\n2\n3\n4\n5\n6\n7\nY\n9\n10\n"),
           std::string("@@ -1,10 +1,10 @@\n-1\n+X\n 2\n 3\n 4\n 5\n 6\n 7\n"
                       "-8\n+Y\n 9\n 10\n"));
}

TEST(unified, gap_of_seven_splits) {
  CHECK_EQ(hunks(TEN, "X\n2\n3\n4\n5\n6\n7\n8\nY\n10\n"),
           std::string("@@ -1,4 +1,4 @@\n-1\n+X\n 2\n 3\n 4\n"
                       "@@ -6,5 +6,5 @@\n 6\n 7\n 8\n-9\n+Y\n 10\n"));
}

TEST(unified, insert_at_start) {
  CHECK_EQ(hunks("a\nb\nc\nd\ne\n", "new\na\nb\nc\nd\ne\n"),
           std::string("@@ -1,3 +1,4 @@\n+new\n a\n b\n c\n"));
}

TEST(unified, append_at_end) {
  CHECK_EQ(hunks("a\nb\n", "a\nb\nc\n"),
           std::string("@@ -1,2 +1,3 @@\n a\n b\n+c\n"));
}

TEST(unified, zero_count_starts_at_line_before) {
  // git diff --no-index -U0
  CHECK_EQ(hunks("a\nb\nc\n", "a\nb\nNEW\nc\n", 0),
           std::string("@@ -2,0 +3 @@\n+NEW\n"));
  CHECK_EQ(hunks("a\nb\nc\n", "a\nc\n", 0), std::string("@@ -2 +1,0 @@\n-b\n"));
}

TEST(unified, whole_file_added_and_removed) {
  CHECK_EQ(hunks("", "a\nb\n"), std::string("@@ -0,0 +1,2 @@\n+a\n+b\n"));
  CHECK_EQ(hunks("a\nb\n", ""), std::string("@@ -1,2 +0,0 @@\n-a\n-b\n"));
}

TEST(unified, single_lines) {
  CHECK_EQ(hunks("x\n", "y\n"), std::string("@@ -1 +1 @@\n-x\n+y\n"));
}

TEST(unified, newline_added_at_end) {
  CHECK_EQ(hunks("a\nb", "a\nb\n"),
           std::string("@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n"));
}

TEST(unified, neither_side_ends_in_newline) {
  CHECK_EQ(hunks("a\nb", "a\nc"),
           std::string("@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n"
                       "+c\n\\ No newline at end of file\n"));
}

// -------- is_binary --------

TEST(unified, binary_means_a_nul_byte) {
  CHECK(!vcs::is_binary(test::bytes("plain text\n")));
  CHECK(!vcs::is_binary({}));
  CHECK(vcs::is_binary(test::bytes_n("a\0b")));
}

TEST(unified, nul_after_8000_bytes_is_text) {
  vcs::ByteStream content(9000, 'a');
  content[8500] = 0;
  CHECK(!vcs::is_binary(content));
  content[7999] = 0;
  CHECK(vcs::is_binary(content));
}

// -------- file_diff --------

TEST(unified, file_modified) {
  CHECK_EQ(vcs::file_diff("src/a.txt", test::bytes("x\n"), test::bytes("y\n")),
           std::string("diff --git a/src/a.txt b/src/a.txt\n"
                       "--- a/src/a.txt\n+++ b/src/a.txt\n"
                       "@@ -1 +1 @@\n-x\n+y\n"));
}

TEST(unified, file_added) {
  CHECK_EQ(vcs::file_diff("a.txt", std::nullopt, test::bytes("x\n")),
           std::string("diff --git a/a.txt b/a.txt\nnew file mode 100644\n"
                       "--- /dev/null\n+++ b/a.txt\n@@ -0,0 +1 @@\n+x\n"));
}

TEST(unified, file_deleted) {
  CHECK_EQ(vcs::file_diff("a.txt", test::bytes("x\n"), std::nullopt),
           std::string("diff --git a/a.txt b/a.txt\ndeleted file mode 100644\n"
                       "--- a/a.txt\n+++ /dev/null\n@@ -1 +0,0 @@\n-x\n"));
}

TEST(unified, empty_file_added_has_no_hunks) {
  CHECK_EQ(vcs::file_diff("e", std::nullopt, vcs::ByteStream{}),
           std::string("diff --git a/e b/e\nnew file mode 100644\n"));
}

TEST(unified, binary_file) {
  CHECK_EQ(vcs::file_diff("img", test::bytes_n("a\0"), test::bytes_n("b\0")),
           std::string("diff --git a/img b/img\nBinary files a/img and b/img differ\n"));
  CHECK_EQ(vcs::file_diff("img", std::nullopt, test::bytes_n("b\0")),
           std::string("diff --git a/img b/img\nnew file mode 100644\n"
                       "Binary files /dev/null and b/img differ\n"));
}

TEST(unified, same_content_is_empty) {
  CHECK_EQ(vcs::file_diff("a", test::bytes("x\n"), test::bytes("x\n")),
           std::string());
}
