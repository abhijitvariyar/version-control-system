#include "../test_harness.hpp"

#include "vcs/compress/deflate.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/tree.hpp"
#include "vcs/repo/repository.hpp"
#include "vcs/util/file.hpp"

namespace fs = std::filesystem;

namespace {

const std::string SIGNATURE = "Ada <ada@example.com> 1700000000 +0000";

void write_file(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  test::write_bytes(path, test::bytes(text));
}

vcs::Index index_of(const test::TempDir &dir) {
  return vcs::Index::load(dir / ".vcs" / "index");
}

std::string staged_hash(const test::TempDir &dir, const std::string &path) {
  return index_of(dir).get_entries().at(path);
}

std::vector<std::string> staged(const test::TempDir &dir) {
  return index_of(dir).paths_under("");
}

// The entries of one tree object
std::vector<vcs::TreeNode> tree_entries(vcs::Repository &repo,
                                        const std::string &hash) {
  vcs::ByteStream buffer = repo.objects().read(hash);
  vcs::UnwrappedObject object = vcs::unwrap(buffer);
  CHECK(object.get_object_type() == vcs::ObjectType::Tree);
  return vcs::parse_tree_entries(object.get_content());
}

vcs::ParsedCommit commit_at(vcs::Repository &repo, const std::string &hash) {
  vcs::ByteStream buffer = repo.objects().read(hash);
  vcs::UnwrappedObject object = vcs::unwrap(buffer);
  return vcs::parse_commit(object.get_content());
}

} // namespace

// -------- init and discover --------

TEST(repository, init_creates_layout) {
  test::TempDir dir;
  vcs::Repository::init(dir.path());

  CHECK(fs::is_directory(dir / ".vcs" / "objects"));
  CHECK(fs::is_directory(dir / ".vcs" / "refs" / "heads"));
  CHECK_EQ(test::read_text(dir / ".vcs" / "HEAD"),
           std::string("ref: refs/heads/main\n"));
  CHECK_EQ(test::read_text(dir / ".vcs" / "index"), std::string());
}

TEST(repository, init_creates_missing_directory) {
  test::TempDir dir;
  vcs::Repository::init(dir / "new" / "repo");
  CHECK(fs::exists(dir / "new" / "repo" / ".vcs" / "HEAD"));
}

TEST(repository, init_twice_throws) {
  test::TempDir dir;
  vcs::Repository::init(dir.path());
  CHECK_THROWS(vcs::Repository::init(dir.path()));
}

TEST(repository, discover_from_subdirectory) {
  test::TempDir dir;
  vcs::Repository::init(dir.path());
  fs::create_directories(dir / "a" / "b");

  vcs::Repository repo = vcs::Repository::discover(dir / "a" / "b");
  write_file(dir / "x.txt", "x\n");
  repo.add({dir / "x.txt"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"x.txt"}));
}

TEST(repository, discover_outside_a_repo_throws) {
  test::TempDir dir;
  CHECK_THROWS(vcs::Repository::discover(dir.path()));
}

// -------- add --------

TEST(repository, add_stores_blob_and_stages_it) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "hello.txt", "hello");

  repo.add({dir / "hello.txt"});

  // printf 'blob 5\0hello' | shasum -a 256
  std::string hash =
      "8aec4e4876f854f688d0ebfc8f37598f38e5fd6903cccc850ca36591175aeb60";
  CHECK_EQ(staged_hash(dir, "hello.txt"), hash);
  CHECK(repo.objects().exists(hash));
}

TEST(repository, add_directory_recurses) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "a.txt", "a\n");
  write_file(dir / "src" / "deep" / "b.txt", "b\n");
  write_file(dir / "other.txt", "o\n");

  repo.add({dir / "src"});

  CHECK_EQ(staged(dir),
           (std::vector<std::string>{"src/a.txt", "src/deep/b.txt"}));
}

TEST(repository, add_root_skips_vcs_dir) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");

  repo.add({dir.path()});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"a.txt"}));
}

TEST(repository, add_trailing_slash) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "a.txt", "a\n");

  repo.add({(dir / "src").string() + "/"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"src/a.txt"}));
}

TEST(repository, add_path_with_dots_is_normalized) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "a.txt", "a\n");

  repo.add({dir / "src" / "." / ".." / "src" / "a.txt"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"src/a.txt"}));
}

TEST(repository, add_changed_file_updates_hash) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "one\n");
  repo.add({dir / "a.txt"});
  std::string first = staged_hash(dir, "a.txt");

  write_file(dir / "a.txt", "two\n");
  repo.add({dir / "a.txt"});
  CHECK(staged_hash(dir, "a.txt") != first);
}

TEST(repository, add_deleted_file_stages_deletion) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});

  fs::remove(dir / "a.txt");
  repo.add({dir / "a.txt"});
  CHECK(staged(dir).empty());
}

TEST(repository, add_directory_stages_deletions_inside_it) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "keep.txt", "k\n");
  write_file(dir / "src" / "gone.txt", "g\n");
  write_file(dir / "other.txt", "o\n");
  repo.add({dir.path()});

  fs::remove(dir / "src" / "gone.txt");
  repo.add({dir / "src"});
  CHECK_EQ(staged(dir),
           (std::vector<std::string>{"other.txt", "src/keep.txt"}));
}

TEST(repository, add_file_replaced_by_directory) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a", "file\n");
  repo.add({dir / "a"});

  fs::remove(dir / "a");
  write_file(dir / "a" / "b.txt", "b\n");
  repo.add({dir / "a"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"a/b.txt"}));
  CHECK_NOTHROW(repo.commit("works", SIGNATURE));
}

TEST(repository, add_missing_path_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  CHECK_THROWS(repo.add({dir / "nope.txt"}));
}

TEST(repository, add_outside_repo_throws) {
  test::TempDir dir;
  test::TempDir other;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(other / "x.txt", "x\n");

  CHECK_THROWS(repo.add({other / "x.txt"}));
  CHECK_THROWS(repo.add({dir / ".."}));
}

TEST(repository, add_inside_vcs_dir_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  CHECK_THROWS(repo.add({dir / ".vcs" / "HEAD"}));
  CHECK_THROWS(repo.add({dir / ".vcs"}));
}

TEST(repository, add_is_all_or_nothing) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");

  CHECK_THROWS(repo.add({dir / "a.txt", dir / "nope.txt"}));
  CHECK(staged(dir).empty());
}

TEST(repository, add_symlink_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  fs::create_symlink(dir / "a.txt", dir / "link");
  CHECK_THROWS(repo.add({dir / "link"}));
}

TEST(repository, add_directory_skips_symlinks) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  fs::create_symlink(dir / "a.txt", dir / "file_link");
  fs::create_directory_symlink(dir.path(), dir / "loop");

  repo.add({dir.path()});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"a.txt"}));
}

// -------- .vcsignore --------

TEST(repository, add_skips_ignored_files_and_directories) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "bin/\n*.o\n");
  write_file(dir / "bin" / "app", "binary\n");
  write_file(dir / "src" / "bin" / "nested", "binary\n");
  write_file(dir / "src" / "a.cpp", "a\n");
  write_file(dir / "src" / "a.o", "object\n");

  repo.add({dir.path()});
  CHECK_EQ(staged(dir), (std::vector<std::string>{".vcsignore", "src/a.cpp"}));
}

TEST(repository, ignored_directory_is_not_walked) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "locked/\n");
  write_file(dir / "locked" / "a.txt", "a\n");
  fs::permissions(dir / "locked", fs::perms::none);

  CHECK_NOTHROW(repo.add({dir.path()}));
  fs::permissions(dir / "locked", fs::perms::owner_all);
  CHECK_EQ(staged(dir), (std::vector<std::string>{".vcsignore"}));
}

TEST(repository, naming_an_ignored_file_adds_it) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "*.log\n");
  write_file(dir / "keep.log", "x\n");

  repo.add({dir / "keep.log"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"keep.log"}));
}

TEST(repository, tracked_file_stays_tracked_once_ignored) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "out" / "a.log", "one\n");
  repo.add({dir.path()});
  std::string first = staged_hash(dir, "out/a.log");

  write_file(dir / ".vcsignore", "*.log\nout/\n");
  repo.add({dir.path()});
  CHECK_EQ(staged_hash(dir, "out/a.log"), first);

  // and later changes to it are still picked up
  write_file(dir / "out" / "a.log", "two\n");
  repo.add({dir.path()});
  CHECK(staged_hash(dir, "out/a.log") != first);
}

TEST(repository, deleting_a_tracked_ignored_file_stages_the_deletion) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.log", "x\n");
  repo.add({dir.path()});

  write_file(dir / ".vcsignore", "*.log\n");
  fs::remove(dir / "a.log");
  repo.add({dir.path()});
  CHECK_EQ(staged(dir), (std::vector<std::string>{".vcsignore"}));
}

TEST(repository, ignore_rules_use_repo_paths_from_a_subdirectory) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "/src/gen.txt\n");
  write_file(dir / "src" / "gen.txt", "g\n");
  write_file(dir / "src" / "a.txt", "a\n");

  repo.add({dir / "src"});
  CHECK_EQ(staged(dir), (std::vector<std::string>{"src/a.txt"}));
}

TEST(repository, bad_ignore_file_fails_add) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "!keep.txt\n");
  write_file(dir / "a.txt", "a\n");
  CHECK_THROWS(repo.add({dir.path()}));
  CHECK(staged(dir).empty());
}

// -------- commit --------

TEST(repository, commit_matches_real_git) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "README.md", "hello\n");
  write_file(dir / "src" / "main.cpp", "int main() {}\n");
  write_file(dir / "src" / "deep" / "a.txt", "x\n");
  write_file(dir / "src.txt", "lib\n");
  repo.add({dir.path()});

  // Expected values come from the same files in a real repository:
  //   git init --object-format=sha256; git add .
  //   GIT_AUTHOR_DATE='1700000000 +0000' (and GIT_COMMITTER_DATE, name, email)
  //   git commit -m 'first commit'; git rev-parse HEAD HEAD^{tree}
  std::string first = repo.commit("first commit", SIGNATURE);
  CHECK_EQ(first, std::string("5a2a255726114e4ad14ef23814884740"
                              "d5f95932d8078bfc216a162bfc0e60bb"));
  CHECK_EQ(commit_at(repo, first).tree_hash,
           std::string("e8a69f1c7ae2212f2348f0af86f9736c"
                       "8b163f5300b6d76bbdb1703b567b01d4"));

  // the same, after appending "more\n" to README.md, with date
  // '1700000100 +0530'
  write_file(dir / "README.md", "hello\nmore\n");
  repo.add({dir / "README.md"});
  std::string second =
      repo.commit("second commit", "Ada <ada@example.com> 1700000100 +0530");
  CHECK_EQ(second, std::string("c9108f468d1a1b9b7c6554455b856d89"
                               "36b1ebc34ddb1c1e8f06ca1c7bd67052"));
}

TEST(repository, commit_writes_every_tree_level) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a" / "b" / "c.txt", "c\n");
  repo.add({dir.path()});

  vcs::ParsedCommit commit = commit_at(repo, repo.commit("msg", SIGNATURE));

  std::vector<vcs::TreeNode> root = tree_entries(repo, commit.tree_hash);
  CHECK_EQ(root.size(), size_t(1));
  CHECK_EQ(root.at(0).name, std::string("a"));

  std::vector<vcs::TreeNode> a = tree_entries(repo, vcs::to_hex(root.at(0).hash));
  CHECK_EQ(a.at(0).name, std::string("b"));

  std::vector<vcs::TreeNode> b = tree_entries(repo, vcs::to_hex(a.at(0).hash));
  CHECK_EQ(b.at(0).name, std::string("c.txt"));
  CHECK(repo.objects().exists(vcs::to_hex(b.at(0).hash)));
}

TEST(repository, commit_records_fields) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});

  vcs::ParsedCommit commit = commit_at(repo, repo.commit("msg", SIGNATURE));
  CHECK_EQ(commit.author, SIGNATURE);
  CHECK_EQ(commit.committer, SIGNATURE);
  CHECK_EQ(commit.message, std::string("msg\n"));
  CHECK(commit.parent_hashes.empty());
}

TEST(repository, commit_keeps_existing_trailing_newline) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});

  vcs::ParsedCommit commit = commit_at(repo, repo.commit("msg\n", SIGNATURE));
  CHECK_EQ(commit.message, std::string("msg\n"));
}

TEST(repository, commit_moves_branch_and_links_parent) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "1\n");
  repo.add({dir / "a.txt"});
  std::string first = repo.commit("one", SIGNATURE);

  CHECK_EQ(test::read_text(dir / ".vcs" / "refs" / "heads" / "main"),
           first + "\n");

  write_file(dir / "a.txt", "2\n");
  repo.add({dir / "a.txt"});
  std::string second = repo.commit("two", SIGNATURE);

  CHECK_EQ(test::read_text(dir / ".vcs" / "refs" / "heads" / "main"),
           second + "\n");
  CHECK_EQ(commit_at(repo, second).parent_hashes,
           (std::vector<std::string>{first}));
}

TEST(repository, commit_empty_index_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  CHECK_THROWS(repo.commit("msg", SIGNATURE));
  CHECK(!fs::exists(dir / ".vcs" / "refs" / "heads" / "main"));
}

TEST(repository, commit_without_changes_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});
  std::string first = repo.commit("one", SIGNATURE);

  CHECK_THROWS(repo.commit("two", SIGNATURE));
  CHECK_EQ(test::read_text(dir / ".vcs" / "refs" / "heads" / "main"),
           first + "\n");
}

TEST(repository, commit_empty_message_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});
  CHECK_THROWS(repo.commit("", SIGNATURE));
}

TEST(repository, commit_deleting_every_file) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});
  repo.commit("add", SIGNATURE);

  fs::remove(dir / "a.txt");
  repo.add({dir / "a.txt"});
  vcs::ParsedCommit commit = commit_at(repo, repo.commit("remove", SIGNATURE));

  CHECK(tree_entries(repo, commit.tree_hash).empty());
}

TEST(repository, commit_on_detached_head) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "1\n");
  repo.add({dir / "a.txt"});
  std::string first = repo.commit("one", SIGNATURE);

  test::write_bytes(dir / ".vcs" / "HEAD", test::bytes(first + "\n"));
  write_file(dir / "a.txt", "2\n");
  repo.add({dir / "a.txt"});
  std::string second = repo.commit("two", SIGNATURE);

  CHECK_EQ(test::read_text(dir / ".vcs" / "HEAD"), second + "\n");
  CHECK_EQ(test::read_text(dir / ".vcs" / "refs" / "heads" / "main"),
           first + "\n");
}

// -------- log --------

TEST(repository, log_before_first_commit_is_empty) {
  test::TempDir dir;
  CHECK(vcs::Repository::init(dir.path()).log().empty());
}

TEST(repository, log_is_newest_first) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());

  std::vector<std::string> hashes;
  for (int i = 0; i < 3; i++) {
    write_file(dir / "a.txt", std::to_string(i));
    repo.add({dir / "a.txt"});
    hashes.push_back(repo.commit("commit " + std::to_string(i), SIGNATURE));
  }

  std::vector<vcs::LogEntry> log = repo.log();
  CHECK_EQ(log.size(), size_t(3));
  CHECK_EQ(log.at(0).hash, hashes[2]);
  CHECK_EQ(log.at(1).hash, hashes[1]);
  CHECK_EQ(log.at(2).hash, hashes[0]);
  CHECK_EQ(log.at(2).commit.message, std::string("commit 0\n"));
}

TEST(repository, log_on_a_damaged_commit_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});
  std::string hash = repo.commit("real message", SIGNATURE);

  // swap the commit for a well-formed one that says something else
  vcs::ByteStream forged = vcs::wrap(
      vcs::ObjectType::Commit,
      test::bytes("tree " + std::string(64, 'a') + "\nauthor x\ncommitter x\n\nfake\n"));
  fs::path path = dir / ".vcs" / "objects" / hash.substr(0, 2) / hash.substr(2);
  test::write_bytes(path, vcs::compress(forged));

  CHECK_THROWS(repo.log());
}

TEST(repository, log_on_a_non_commit_ref_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});

  std::string blob = staged_hash(dir, "a.txt");
  test::write_bytes(dir / ".vcs" / "refs" / "heads" / "main",
                    test::bytes(blob + "\n"));
  CHECK_THROWS(repo.log());
}

// -------- config --------

TEST(repository, config_set_and_get) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());

  CHECK(!repo.get_config("user.name").has_value());
  repo.set_config("user.name", "Ada Lovelace");
  repo.set_config("user.email", "a=b@example.com");
  repo.set_config("user.name", "Ada");

  std::string name = repo.get_config("user.name").value();
  std::string email = repo.get_config("user.email").value();
  CHECK_EQ(name, std::string("Ada"));
  CHECK_EQ(email, std::string("a=b@example.com"));
}

TEST(repository, config_rejects_bad_input) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());

  CHECK_THROWS(repo.set_config("", "x"));
  CHECK_THROWS(repo.set_config("a=b", "x"));
  CHECK_THROWS(repo.set_config("a\nb", "x"));
  CHECK_THROWS(repo.set_config("user.name", "Ada\nparent evil"));
}

// -------- status --------

namespace {

std::string changes_of(const std::vector<vcs::TreeChange> &changes) {
  std::string out;
  for (const vcs::TreeChange &change : changes) {
    out += change.type == vcs::ChangeType::Added     ? "A "
           : change.type == vcs::ChangeType::Deleted ? "D "
                                                     : "M ";
    out += change.path + "\n";
  }
  return out;
}

std::string text_of(const std::optional<vcs::ByteStream> &content) {
  return content ? std::string(content->begin(), content->end()) : "<none>";
}

} // namespace

TEST(repository, status_of_fresh_repo) {
  test::TempDir dir;
  vcs::StatusReport report = vcs::Repository::init(dir.path()).status();

  CHECK_EQ(report.branch.value_or(""), std::string("main"));
  CHECK(!report.has_commits);
  CHECK(report.staged.empty());
  CHECK(report.unstaged.empty());
  CHECK(report.untracked.empty());
}

TEST(repository, status_before_first_commit_stages_everything_as_new) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  write_file(dir / "src" / "b.txt", "b\n");
  repo.add({dir.path()});

  CHECK_EQ(changes_of(repo.status().staged), std::string("A a.txt\nA src/b.txt\n"));
}

TEST(repository, status_clean_after_commit) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  vcs::StatusReport report = repo.status();
  CHECK(report.has_commits);
  CHECK(report.staged.empty());
  CHECK(report.unstaged.empty());
  CHECK(report.untracked.empty());
}

TEST(repository, status_staged_changes_against_head) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "keep.txt", "k\n");
  write_file(dir / "edit.txt", "1\n");
  write_file(dir / "gone.txt", "g\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  write_file(dir / "edit.txt", "2\n");
  write_file(dir / "new.txt", "n\n");
  fs::remove(dir / "gone.txt");
  repo.add({dir.path()});

  vcs::StatusReport report = repo.status();
  CHECK_EQ(changes_of(report.staged),
           std::string("M edit.txt\nD gone.txt\nA new.txt\n"));
  CHECK(report.unstaged.empty());
}

TEST(repository, status_unstaged_changes_against_index) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "edit.txt", "1\n");
  write_file(dir / "gone.txt", "g\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  write_file(dir / "edit.txt", "2\n");
  fs::remove(dir / "gone.txt");

  vcs::StatusReport report = repo.status();
  CHECK(report.staged.empty());
  CHECK_EQ(changes_of(report.unstaged), std::string("M edit.txt\nD gone.txt\n"));
}

TEST(repository, status_staged_then_edited_again_shows_both) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "1\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  write_file(dir / "a.txt", "2\n");
  repo.add({dir / "a.txt"});
  write_file(dir / "a.txt", "3\n");

  vcs::StatusReport report = repo.status();
  CHECK_EQ(changes_of(report.staged), std::string("M a.txt\n"));
  CHECK_EQ(changes_of(report.unstaged), std::string("M a.txt\n"));
}

TEST(repository, status_lists_untracked_files) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "tracked.txt", "t\n");
  repo.add({dir / "tracked.txt"});
  write_file(dir / "b.txt", "b\n");
  write_file(dir / "a.txt", "a\n");

  CHECK_EQ(repo.status().untracked, (std::vector<std::string>{"a.txt", "b.txt"}));
}

TEST(repository, status_collapses_untracked_directories) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "tracked.txt", "t\n");
  repo.add({dir / "src" / "tracked.txt"});

  write_file(dir / "src" / "new.txt", "n\n");
  write_file(dir / "src" / "fresh" / "a.txt", "a\n");
  write_file(dir / "src" / "fresh" / "deep" / "b.txt", "b\n");
  write_file(dir / "docs" / "c.txt", "c\n");
  fs::create_directories(dir / "empty_dir");

  CHECK_EQ(repo.status().untracked,
           (std::vector<std::string>{"docs/", "src/fresh/", "src/new.txt"}));
}

TEST(repository, status_respects_vcsignore) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / ".vcsignore", "bin/\n*.o\n");
  write_file(dir / "bin" / "app", "x\n");
  write_file(dir / "a.o", "x\n");
  write_file(dir / "a.c", "x\n");

  CHECK_EQ(repo.status().untracked,
           (std::vector<std::string>{".vcsignore", "a.c"}));
}

TEST(repository, status_tracked_file_replaced_by_directory) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a", "file\n");
  repo.add({dir / "a"});

  fs::remove(dir / "a");
  write_file(dir / "a" / "b.txt", "b\n");

  vcs::StatusReport report = repo.status();
  CHECK_EQ(changes_of(report.unstaged), std::string("D a\n"));
  CHECK_EQ(report.untracked, (std::vector<std::string>{"a/"}));
}

TEST(repository, status_does_not_write_objects) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "src" / "a.txt", "a\n");
  repo.add({dir.path()});

  size_t before = 0;
  for (auto it = fs::recursive_directory_iterator(dir / ".vcs" / "objects");
       it != fs::recursive_directory_iterator(); ++it)
    before++;

  repo.status();

  size_t after = 0;
  for (auto it = fs::recursive_directory_iterator(dir / ".vcs" / "objects");
       it != fs::recursive_directory_iterator(); ++it)
    after++;

  CHECK_EQ(after, before);
}

TEST(repository, status_on_detached_head) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir.path()});
  std::string hash = repo.commit("msg", SIGNATURE);
  test::write_bytes(dir / ".vcs" / "HEAD", test::bytes(hash + "\n"));

  vcs::StatusReport report = repo.status();
  CHECK(!report.branch.has_value());
  CHECK(report.has_commits);
  CHECK(report.staged.empty());
}

// -------- diff --------

TEST(repository, diff_unstaged_contents) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "edit.txt", "old\n");
  write_file(dir / "gone.txt", "g\n");
  repo.add({dir.path()});

  write_file(dir / "edit.txt", "new\n");
  fs::remove(dir / "gone.txt");
  write_file(dir / "untracked.txt", "u\n");

  std::vector<vcs::FileChange> files = repo.diff(false);
  CHECK_EQ(files.size(), size_t(2));
  CHECK_EQ(files.at(0).path, std::string("edit.txt"));
  CHECK_EQ(text_of(files.at(0).old_content), std::string("old\n"));
  CHECK_EQ(text_of(files.at(0).new_content), std::string("new\n"));
  CHECK_EQ(files.at(1).path, std::string("gone.txt"));
  CHECK_EQ(text_of(files.at(1).new_content), std::string("<none>"));
}

TEST(repository, diff_staged_contents) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "one\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  write_file(dir / "a.txt", "two\n");
  write_file(dir / "b.txt", "b\n");
  repo.add({dir.path()});
  write_file(dir / "a.txt", "three\n"); // unstaged, so not in --staged

  std::vector<vcs::FileChange> files = repo.diff(true);
  CHECK_EQ(files.size(), size_t(2));
  CHECK_EQ(text_of(files.at(0).old_content), std::string("one\n"));
  CHECK_EQ(text_of(files.at(0).new_content), std::string("two\n"));
  CHECK_EQ(text_of(files.at(1).old_content), std::string("<none>"));
  CHECK_EQ(text_of(files.at(1).new_content), std::string("b\n"));
}

TEST(repository, diff_with_no_changes_is_empty) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir.path()});
  repo.commit("msg", SIGNATURE);

  CHECK(repo.diff(false).empty());
  CHECK(repo.diff(true).empty());
}
