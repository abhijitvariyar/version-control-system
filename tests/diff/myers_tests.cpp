#include "../test_harness.hpp"

#include <chrono>
#include <random>

#include "vcs/diff/myers.hpp"

using vcs::DiffOp;
using vcs::DiffOpType;

namespace {

typedef std::vector<std::string> Lines;

// One letter per line, so a script reads as a string: "ABC" -> {"A\n", ...}
Lines lines(const std::string &letters) {
  Lines out;
  for (char c : letters)
    out.push_back(std::string(1, c) + "\n");
  return out;
}

size_t edit_count(const std::vector<DiffOp> &ops) {
  size_t edits = 0;
  for (const DiffOp &op : ops)
    if (op.type != DiffOpType::Equal)
      edits++;
  return edits;
}

// The fewest edits possible, from the longest common subsequence
size_t shortest(const Lines &a, const Lines &b) {
  std::vector<std::vector<size_t>> lcs(a.size() + 1,
                                       std::vector<size_t>(b.size() + 1, 0));
  for (size_t i = 1; i <= a.size(); i++)
    for (size_t j = 1; j <= b.size(); j++)
      lcs[i][j] = a[i - 1] == b[j - 1] ? lcs[i - 1][j - 1] + 1
                                       : std::max(lcs[i - 1][j], lcs[i][j - 1]);
  return a.size() + b.size() - 2 * lcs[a.size()][b.size()];
}

// Applies the script to `a` and checks every op is in step and consistent
bool rebuilds(const Lines &a, const Lines &b, const std::vector<DiffOp> &ops) {
  Lines out;
  size_t i = 0, j = 0;

  for (const DiffOp &op : ops) {
    if (op.old_index != i || op.new_index != j)
      return false;

    if (op.type == DiffOpType::Equal) {
      if (i >= a.size() || j >= b.size() || a[i] != b[j])
        return false;
      out.push_back(a[i]);
      i++;
      j++;
    } else if (op.type == DiffOpType::Delete) {
      if (i >= a.size())
        return false;
      i++;
    } else {
      if (j >= b.size())
        return false;
      out.push_back(b[j]);
      j++;
    }
  }

  return out == b && i == a.size();
}

std::string script(const std::vector<DiffOp> &ops) {
  std::string out;
  for (const DiffOp &op : ops)
    out += op.type == DiffOpType::Equal    ? '='
           : op.type == DiffOpType::Delete ? '-'
                                           : '+';
  return out;
}

} // namespace

// -------- split_lines --------

TEST(myers, split_keeps_newlines) {
  CHECK_EQ(vcs::split_lines("a\nb\n"), (Lines{"a\n", "b\n"}));
}

TEST(myers, split_last_line_without_newline) {
  CHECK_EQ(vcs::split_lines("a\nb"), (Lines{"a\n", "b"}));
}

TEST(myers, split_empty_and_blank_lines) {
  CHECK(vcs::split_lines("").empty());
  CHECK_EQ(vcs::split_lines("\n\n"), (Lines{"\n", "\n"}));
}

TEST(myers, split_keeps_carriage_returns) {
  CHECK_EQ(vcs::split_lines("a\r\nb\r\n"), (Lines{"a\r\n", "b\r\n"}));
}

// -------- diff_lines --------

TEST(myers, both_empty) {
  CHECK(vcs::diff_lines({}, {}).empty());
}

TEST(myers, identical) {
  CHECK_EQ(script(vcs::diff_lines(lines("ABC"), lines("ABC"))), std::string("==="));
}

TEST(myers, from_empty_is_all_inserts) {
  std::vector<DiffOp> ops = vcs::diff_lines({}, lines("AB"));
  CHECK_EQ(script(ops), std::string("++"));
  CHECK(rebuilds({}, lines("AB"), ops));
}

TEST(myers, to_empty_is_all_deletes) {
  CHECK_EQ(script(vcs::diff_lines(lines("AB"), {})), std::string("--"));
}

TEST(myers, total_replacement) {
  std::vector<DiffOp> ops = vcs::diff_lines(lines("ABC"), lines("XYZ"));
  CHECK_EQ(script(ops), std::string("---+++"));
  CHECK(rebuilds(lines("ABC"), lines("XYZ"), ops));
}

TEST(myers, one_line_each) {
  CHECK_EQ(script(vcs::diff_lines(lines("A"), lines("B"))), std::string("-+"));
  CHECK_EQ(script(vcs::diff_lines(lines("A"), lines("A"))), std::string("="));
}

TEST(myers, pure_insert_in_middle) {
  CHECK_EQ(script(vcs::diff_lines(lines("AC"), lines("ABC"))), std::string("=+="));
}

TEST(myers, pure_delete_in_middle) {
  CHECK_EQ(script(vcs::diff_lines(lines("ABC"), lines("AC"))), std::string("=-="));
}

TEST(myers, deletes_come_before_inserts) {
  CHECK_EQ(script(vcs::diff_lines(lines("ABC"), lines("AXC"))), std::string("=-+="));
}

TEST(myers, textbook_example) {
  // Myers' paper: ABCABBA -> CBABAC takes 5 edits
  Lines a = lines("ABCABBA");
  Lines b = lines("CBABAC");
  std::vector<DiffOp> ops = vcs::diff_lines(a, b);
  CHECK_EQ(edit_count(ops), size_t(5));
  CHECK(rebuilds(a, b, ops));
}

TEST(myers, last_line_without_newline_differs) {
  // "b" and "b\n" are different lines, which is how git shows the change
  std::vector<DiffOp> ops = vcs::diff_lines({"a\n", "b"}, {"a\n", "b\n"});
  CHECK_EQ(script(ops), std::string("=-+"));
}

TEST(myers, always_shortest_and_valid) {
  std::mt19937_64 random(42);

  for (int round = 0; round < 3000; round++) {
    // small alphabets force many equal lines and many equally short scripts
    int alphabet = 1 + static_cast<int>(random() % (round % 3 == 0 ? 2 : 6));
    Lines a, b;
    for (size_t i = random() % 25; i > 0; i--)
      a.push_back(std::string(1, char('A' + random() % alphabet)) + "\n");
    for (size_t i = random() % 25; i > 0; i--)
      b.push_back(std::string(1, char('A' + random() % alphabet)) + "\n");

    std::vector<DiffOp> ops = vcs::diff_lines(a, b);

    if (!rebuilds(a, b, ops) || edit_count(ops) != shortest(a, b)) {
      std::string a_text, b_text;
      for (const std::string &line : a)
        a_text += line[0];
      for (const std::string &line : b)
        b_text += line[0];
      test::fail(__FILE__, __LINE__,
                 "bad script for " + a_text + " -> " + b_text + ": " + script(ops));
      return;
    }
  }
}

TEST(myers, large_files_are_fast) {
  Lines big;
  for (int i = 0; i < 50000; i++)
    big.push_back("line " + std::to_string(i) + "\n");

  Lines edited = big;
  for (int i = 0; i < 50; i++)
    edited[static_cast<size_t>(i) * 997] = "changed\n";

  Lines rewritten;
  for (int i = 0; i < 50000; i++)
    rewritten.push_back("other " + std::to_string(i) + "\n");

  auto start = std::chrono::steady_clock::now();
  std::vector<DiffOp> few = vcs::diff_lines(big, edited);
  std::vector<DiffOp> all = vcs::diff_lines(big, rewritten);
  double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();

  CHECK_EQ(edit_count(few), size_t(100));
  CHECK_EQ(edit_count(all), size_t(100000));
  // generous, for sanitizer builds: the naive version takes minutes and GBs
  CHECK(seconds < 10);
}
