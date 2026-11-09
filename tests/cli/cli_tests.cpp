// End-to-end tests: run the built `vcs` binary in a temp directory.

#include "../test_harness.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "vcs/repo/repository.hpp"

namespace fs = std::filesystem;

namespace {

const std::string HELLO_HASH =
    "8aec4e4876f854f688d0ebfc8f37598f38e5fd6903cccc850ca36591175aeb60";

struct RunResult {
  int exit_code;
  std::string out; // stdout only
};

// Runs `vcs <args>` with `cwd` as the working directory
RunResult run_vcs(const fs::path &cwd, const std::string &args) {
  std::string cmd = "cd '" + cwd.string() + "' && '" + std::string(VCS_BIN) +
                    "' " + args + " 2>/dev/null";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    throw std::runtime_error("popen failed");

  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
    out.append(buf, n);

  int status = pclose(pipe);
  int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return {code, out};
}

std::string trim_newline(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

} // namespace

TEST(cli, no_args_fails) {
  test::TempDir dir;
  CHECK(run_vcs(dir.path(), "").exit_code != 0);
}

TEST(cli, unknown_command_fails) {
  test::TempDir dir;
  CHECK(run_vcs(dir.path(), "frobnicate").exit_code != 0);
}

TEST(cli, init_creates_layout) {
  test::TempDir dir;
  RunResult r = run_vcs(dir.path(), "init");
  CHECK_EQ(r.exit_code, 0);

  fs::path vcs = dir / ".vcs";
  CHECK(fs::is_directory(vcs / "objects"));
  CHECK(fs::is_directory(vcs / "refs" / "heads"));
  CHECK(fs::is_regular_file(vcs / "index"));
  CHECK_EQ(test::read_text(vcs / "HEAD"), std::string("ref: refs/heads/main\n"));
}

TEST(cli, init_with_path_argument) {
  test::TempDir dir;
  fs::create_directories(dir / "repo");
  CHECK_EQ(run_vcs(dir.path(), "init repo").exit_code, 0);
  CHECK(fs::is_directory(dir / "repo" / ".vcs" / "objects"));
}

TEST(cli, init_twice_fails) {
  test::TempDir dir;
  CHECK_EQ(run_vcs(dir.path(), "init").exit_code, 0);
  CHECK(run_vcs(dir.path(), "init").exit_code != 0);
}

TEST(cli, hash_object_outside_repo_fails) {
  test::TempDir dir;
  test::write_bytes(dir / "a.txt", test::bytes("hello"));
  CHECK(run_vcs(dir.path(), "hash-object a.txt").exit_code != 0);
}

TEST(cli, hash_object_prints_hash_and_stores) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  test::write_bytes(dir / "a.txt", test::bytes("hello"));

  RunResult r = run_vcs(dir.path(), "hash-object a.txt");
  CHECK_EQ(r.exit_code, 0);
  CHECK_EQ(trim_newline(r.out), HELLO_HASH);
  CHECK(fs::is_regular_file(dir / ".vcs" / "objects" / HELLO_HASH.substr(0, 2) /
                            HELLO_HASH.substr(2)));
}

TEST(cli, hash_object_missing_file_fails) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  CHECK(run_vcs(dir.path(), "hash-object nope.txt").exit_code != 0);
}

TEST(cli, cat_file_prints_blob_content) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  test::write_bytes(dir / "a.txt", test::bytes("hello"));
  run_vcs(dir.path(), "hash-object a.txt");

  RunResult r = run_vcs(dir.path(), "cat-file " + HELLO_HASH);
  CHECK_EQ(r.exit_code, 0);
  CHECK_EQ(r.out, std::string("hello"));
}

TEST(cli, cat_file_missing_object_fails) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  CHECK(run_vcs(dir.path(), "cat-file " + HELLO_HASH).exit_code != 0);
}

TEST(cli, finds_repo_from_subdirectory) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  fs::create_directories(dir / "sub" / "deeper");
  test::write_bytes(dir / "sub" / "deeper" / "a.txt", test::bytes("hello"));

  RunResult r = run_vcs(dir / "sub" / "deeper", "hash-object a.txt");
  CHECK_EQ(r.exit_code, 0);
  CHECK_EQ(trim_newline(r.out), HELLO_HASH);
  CHECK(fs::exists(dir / ".vcs" / "objects" / HELLO_HASH.substr(0, 2)));
}

// -------- add, commit, log --------

namespace {

void write_file(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  test::write_bytes(path, test::bytes(text));
}

// An initialized repository with an identity, ready to commit
void setup_repo(const test::TempDir &dir) {
  run_vcs(dir.path(), "init");
  run_vcs(dir.path(), "config user.name 'Ada Lovelace'");
  run_vcs(dir.path(), "config user.email ada@example.com");
}

// The first word after "commit " on the first line of `vcs log`
std::string head_hash(const fs::path &dir) {
  return run_vcs(dir, "log").out.substr(7, 64);
}

bool contains(const std::string &text, const std::string &part) {
  return text.find(part) != std::string::npos;
}

} // namespace

TEST(cli, config_get_and_set) {
  test::TempDir dir;
  setup_repo(dir);
  CHECK_EQ(trim_newline(run_vcs(dir.path(), "config user.name").out),
           std::string("Ada Lovelace"));
  CHECK(run_vcs(dir.path(), "config user.missing").exit_code != 0);
}

TEST(cli, add_commit_log_loop) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "one\n");
  CHECK_EQ(run_vcs(dir.path(), "add a.txt").exit_code, 0);

  RunResult commit = run_vcs(dir.path(), "commit -m 'first commit'");
  CHECK_EQ(commit.exit_code, 0);
  CHECK(contains(commit.out, "first commit"));

  write_file(dir / "a.txt", "two\n");
  run_vcs(dir.path(), "add .");
  CHECK_EQ(run_vcs(dir.path(), "commit -m 'second commit'").exit_code, 0);

  RunResult log = run_vcs(dir.path(), "log");
  CHECK_EQ(log.exit_code, 0);
  CHECK(contains(log.out, "Author: Ada Lovelace <ada@example.com>"));
  CHECK(contains(log.out, "    first commit\n"));
  CHECK(log.out.find("second commit") < log.out.find("first commit"));
}

TEST(cli, log_shows_signature_date_in_its_own_zone) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");

  vcs::Repository repo(dir.path());
  repo.add({dir / "a.txt"});
  repo.commit("msg", "Ada <a@b> 1700000000 +0530");

  // date -u -r 1700000000 is Tue Nov 14 22:13:20 2023; +0530 is Wed 03:43:20
  CHECK(contains(run_vcs(dir.path(), "log").out,
                 "Date:   Wed Nov 15 03:43:20 2023 +0530"));
}

TEST(cli, log_indents_every_message_line) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");
  run_vcs(dir.path(), "commit -m 'subject\n\nbody'");

  CHECK(contains(run_vcs(dir.path(), "log").out, "    subject\n\n    body\n"));
}

TEST(cli, commit_time_is_now) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");

  long long before = std::time(nullptr);
  run_vcs(dir.path(), "commit -m msg");
  long long after = std::time(nullptr);

  std::string raw = run_vcs(dir.path(), "cat-file " + head_hash(dir.path())).out;
  size_t start = raw.find("> ") + 2;
  long long stamp = std::stoll(raw.substr(start));
  CHECK(stamp >= before && stamp <= after);
}

TEST(cli, add_from_subdirectory_uses_repo_relative_paths) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "src" / "a.txt", "a\n");

  CHECK_EQ(run_vcs(dir / "src", "add a.txt").exit_code, 0);
  CHECK(contains(test::read_text(dir / ".vcs" / "index"), " src/a.txt\n"));
}

TEST(cli, add_file_with_space_in_name) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "my file.txt", "a\n");
  CHECK_EQ(run_vcs(dir.path(), "add 'my file.txt'").exit_code, 0);
  CHECK_EQ(run_vcs(dir.path(), "commit -m msg").exit_code, 0);
}

TEST(cli, cat_file_prints_commit_and_tree) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "src" / "a.txt", "a\n");
  write_file(dir / "b.txt", "b\n");
  run_vcs(dir.path(), "add .");
  run_vcs(dir.path(), "commit -m msg");

  std::string commit = run_vcs(dir.path(), "cat-file " + head_hash(dir.path())).out;
  CHECK(commit.starts_with("tree "));
  CHECK(contains(commit, "author Ada Lovelace <ada@example.com> "));
  CHECK(commit.ends_with("\n\nmsg\n"));

  std::string tree = run_vcs(dir.path(), "cat-file " + commit.substr(5, 64)).out;
  CHECK(contains(tree, "100644 blob "));
  CHECK(contains(tree, "\tb.txt\n"));
  CHECK(contains(tree, "40000 tree "));
  CHECK(contains(tree, "\tsrc\n"));
}

TEST(cli, commit_without_identity_fails) {
  test::TempDir dir;
  run_vcs(dir.path(), "init");
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");
  CHECK(run_vcs(dir.path(), "commit -m msg").exit_code != 0);
  CHECK(!fs::exists(dir / ".vcs" / "refs" / "heads" / "main"));
}

TEST(cli, usage_errors_fail) {
  test::TempDir dir;
  setup_repo(dir);
  CHECK(run_vcs(dir.path(), "add").exit_code != 0);
  CHECK(run_vcs(dir.path(), "commit").exit_code != 0);
  CHECK(run_vcs(dir.path(), "commit -x msg").exit_code != 0);
  CHECK(run_vcs(dir.path(), "commit -m").exit_code != 0);
  CHECK(run_vcs(dir.path(), "log extra").exit_code != 0);
  CHECK(run_vcs(dir.path(), "config").exit_code != 0);
}

TEST(cli, log_with_no_commits_fails) {
  test::TempDir dir;
  setup_repo(dir);
  CHECK(run_vcs(dir.path(), "log").exit_code != 0);
}

TEST(cli, commands_fail_outside_a_repo) {
  test::TempDir dir;
  CHECK(run_vcs(dir.path(), "add x").exit_code != 0);
  CHECK(run_vcs(dir.path(), "commit -m x").exit_code != 0);
  CHECK(run_vcs(dir.path(), "log").exit_code != 0);
  CHECK(run_vcs(dir.path(), "config user.name x").exit_code != 0);
}

TEST(cli, add_dot_respects_vcsignore) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / ".vcsignore", "# build output\nbin/\n");
  write_file(dir / "bin" / "app", "x\n");
  write_file(dir / "a.txt", "a\n");

  CHECK_EQ(run_vcs(dir.path(), "add .").exit_code, 0);
  std::string index = test::read_text(dir / ".vcs" / "index");
  CHECK(contains(index, " a.txt\n"));
  CHECK(!contains(index, "bin"));
}

TEST(cli, cat_file_reports_a_corrupt_object) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "hello");
  std::string hash = trim_newline(run_vcs(dir.path(), "hash-object a.txt").out);

  // same size, so only the hash can catch it (stored mode keeps bytes as is)
  fs::path path = dir / ".vcs" / "objects" / hash.substr(0, 2) / hash.substr(2);
  std::string raw = test::read_text(path);
  raw.back() = 'X';
  test::write_bytes(path, test::bytes(raw));

  CHECK(run_vcs(dir.path(), "cat-file " + hash).exit_code != 0);
}

// -------- status and diff --------

TEST(cli, status_clean) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");
  run_vcs(dir.path(), "commit -m msg");

  RunResult r = run_vcs(dir.path(), "status");
  CHECK_EQ(r.exit_code, 0);
  CHECK_EQ(r.out, std::string("On branch main\n\n"
                              "nothing to commit, working tree clean\n"));
}

TEST(cli, status_all_sections) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "edit.txt", "1\n");
  write_file(dir / "gone.txt", "g\n");
  run_vcs(dir.path(), "add .");
  run_vcs(dir.path(), "commit -m msg");

  write_file(dir / "staged.txt", "s\n");
  run_vcs(dir.path(), "add staged.txt");
  write_file(dir / "edit.txt", "2\n");
  fs::remove(dir / "gone.txt");
  write_file(dir / "new" / "x.txt", "x\n");

  CHECK_EQ(run_vcs(dir.path(), "status").out,
           std::string("On branch main\n\n"
                       "Changes to be committed:\n\tnew file:   staged.txt\n\n"
                       "Changes not staged for commit:\n"
                       "\tmodified:   edit.txt\n\tdeleted:    gone.txt\n\n"
                       "Untracked files:\n\tnew/\n\n"));
}

TEST(cli, status_before_first_commit) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");

  CHECK_EQ(run_vcs(dir.path(), "status").out,
           std::string("On branch main\n\nNo commits yet\n\n"
                       "Changes to be committed:\n\tnew file:   a.txt\n\n"));
}

TEST(cli, diff_shows_unstaged_changes) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "1\n2\n3\n");
  run_vcs(dir.path(), "add a.txt");
  write_file(dir / "a.txt", "1\nTWO\n3\n");

  RunResult r = run_vcs(dir.path(), "diff");
  CHECK_EQ(r.exit_code, 0);
  CHECK_EQ(r.out, std::string("diff --git a/a.txt b/a.txt\n--- a/a.txt\n+++ b/a.txt\n"
                              "@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n 3\n"));
}

TEST(cli, diff_staged) {
  test::TempDir dir;
  setup_repo(dir);
  write_file(dir / "a.txt", "a\n");
  run_vcs(dir.path(), "add a.txt");

  // nothing unstaged, and against no commit the whole file is new
  CHECK_EQ(run_vcs(dir.path(), "diff").out, std::string());
  std::string expected = "diff --git a/a.txt b/a.txt\nnew file mode 100644\n"
                         "--- /dev/null\n+++ b/a.txt\n@@ -0,0 +1 @@\n+a\n";
  CHECK_EQ(run_vcs(dir.path(), "diff --staged").out, expected);
  CHECK_EQ(run_vcs(dir.path(), "diff --cached").out, expected);
}

TEST(cli, status_and_diff_usage_errors) {
  test::TempDir dir;
  setup_repo(dir);
  CHECK(run_vcs(dir.path(), "status extra").exit_code != 0);
  CHECK(run_vcs(dir.path(), "diff --nope").exit_code != 0);
  CHECK(run_vcs(dir.path(), "diff a b").exit_code != 0);
}

TEST(cli, status_and_diff_fail_outside_a_repo) {
  test::TempDir dir;
  CHECK(run_vcs(dir.path(), "status").exit_code != 0);
  CHECK(run_vcs(dir.path(), "diff").exit_code != 0);
}
