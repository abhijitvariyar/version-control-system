#include "../test_harness.hpp"

#include "vcs/compress/deflate.hpp"
#include "vcs/objects/commit.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/repo/repository.hpp"

namespace fs = std::filesystem;

namespace {

const std::string SIGNATURE = "Ada <ada@example.com> 1700000000 +0000";

void write_file(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  test::write_bytes(path, test::bytes(text));
}

std::string head_file(const test::TempDir &dir) {
  return test::read_text(dir / ".vcs" / "HEAD");
}

// Every regular file under the repo except .vcs, as "path=content" lines. The
// test files all end in a newline, so each content finishes its own line.
std::string snapshot(const test::TempDir &dir) {
  std::vector<std::string> lines;
  for (auto it = fs::recursive_directory_iterator(dir.path());
       it != fs::recursive_directory_iterator(); ++it) {
    if (it->path().filename() == ".vcs") {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file())
      lines.push_back(it->path().lexically_relative(dir.path()).generic_string() +
                      "=" + test::read_text(it->path()));
  }
  std::sort(lines.begin(), lines.end());

  std::string out;
  for (const std::string &line : lines)
    out += line;
  return out;
}

// A repository with two branches:
//   main:    a.txt=main, shared.txt=same, only_main.txt, dir/x.txt=main
//   feature: a.txt=feature, shared.txt=same, only_feature.txt, dir/x.txt=feature
// The checkout is left on main.
struct TwoBranches {
  test::TempDir dir;
  vcs::Repository repo;
  std::string main_commit;
  std::string feature_commit;

  TwoBranches() : repo(vcs::Repository::init(dir.path())) {
    write_file(dir / "a.txt", "main\n");
    write_file(dir / "shared.txt", "same\n");
    write_file(dir / "only_main.txt", "m\n");
    write_file(dir / "dir" / "x.txt", "main\n");
    repo.add({dir.path()});
    main_commit = repo.commit("main", SIGNATURE);

    repo.create_branch("feature");
    repo.checkout("feature");
    write_file(dir / "a.txt", "feature\n");
    fs::remove(dir / "only_main.txt");
    write_file(dir / "only_feature.txt", "f\n");
    write_file(dir / "dir" / "x.txt", "feature\n");
    repo.add({dir.path()});
    feature_commit = repo.commit("feature", SIGNATURE);

    repo.checkout("main");
  }
};

const std::string MAIN_FILES =
    "a.txt=main\ndir/x.txt=main\nonly_main.txt=m\nshared.txt=same\n";
const std::string FEATURE_FILES =
    "a.txt=feature\ndir/x.txt=feature\nonly_feature.txt=f\nshared.txt=same\n";

bool clean(vcs::Repository &repo) {
  vcs::StatusReport report = repo.status();
  return report.staged.empty() && report.unstaged.empty() &&
         report.untracked.empty();
}

// Writes a tree object with one raw entry, for trees no honest commit makes
std::string forged_tree(vcs::Repository &repo, const std::string &mode,
                        const std::string &name, const std::string &hex_hash) {
  vcs::ByteStream content = test::bytes(mode + " " + name);
  content.push_back(0);
  vcs::ByteStream hash = vcs::from_hex(hex_hash);
  content.insert(content.end(), hash.begin(), hash.end());
  return repo.objects().write(vcs::wrap(vcs::ObjectType::Tree, content));
}

std::string forged_commit(vcs::Repository &repo, const std::string &tree_hash) {
  vcs::Commit commit(vcs::from_hex(tree_hash), {}, SIGNATURE, SIGNATURE, "evil\n");
  return repo.objects().write(commit.get_payload());
}

} // namespace

// -------- switching --------

TEST(checkout, switches_files_index_and_head) {
  TwoBranches setup;
  CHECK_EQ(snapshot(setup.dir), MAIN_FILES);

  vcs::CheckoutResult result = setup.repo.checkout("feature");
  CHECK_EQ(result.branch.value_or(""), std::string("feature"));
  CHECK(!result.already_there);

  CHECK_EQ(snapshot(setup.dir), FEATURE_FILES);
  CHECK_EQ(head_file(setup.dir), std::string("ref: refs/heads/feature\n"));
  CHECK_EQ(setup.repo.head_commit().value_or(""), setup.feature_commit);
  CHECK(clean(setup.repo));
}

TEST(checkout, and_back_again) {
  TwoBranches setup;
  setup.repo.checkout("feature");
  setup.repo.checkout("main");
  CHECK_EQ(snapshot(setup.dir), MAIN_FILES);
  CHECK(clean(setup.repo));
}

TEST(checkout, current_branch_is_a_no_op) {
  TwoBranches setup;
  vcs::CheckoutResult result = setup.repo.checkout("main");
  CHECK(result.already_there);
  CHECK_EQ(snapshot(setup.dir), MAIN_FILES);
}

TEST(checkout, new_branch_keeps_files) {
  TwoBranches setup;
  write_file(setup.dir / "a.txt", "edited but not committed\n");

  setup.repo.create_branch("topic");
  setup.repo.checkout("topic");

  CHECK_EQ(head_file(setup.dir), std::string("ref: refs/heads/topic\n"));
  CHECK_EQ(test::read_text(setup.dir / "a.txt"),
           std::string("edited but not committed\n"));
}

TEST(checkout, commit_hash_detaches_head) {
  TwoBranches setup;
  vcs::CheckoutResult result = setup.repo.checkout(setup.feature_commit);

  CHECK(!result.branch.has_value());
  CHECK_EQ(head_file(setup.dir), setup.feature_commit + "\n");
  CHECK_EQ(snapshot(setup.dir), FEATURE_FILES);
  CHECK(!setup.repo.current_branch().has_value());
}

TEST(checkout, unique_prefix_resolves) {
  TwoBranches setup;
  CHECK_EQ(setup.repo.checkout(setup.feature_commit.substr(0, 10)).commit,
           setup.feature_commit);
}

TEST(checkout, commit_on_detached_head_then_back) {
  TwoBranches setup;
  setup.repo.checkout(setup.main_commit);
  write_file(setup.dir / "a.txt", "detached work\n");
  setup.repo.add({setup.dir / "a.txt"});
  std::string detached = setup.repo.commit("detached", SIGNATURE);

  CHECK_EQ(head_file(setup.dir), detached + "\n");
  // the branch didn't move
  CHECK_EQ(test::read_text(setup.dir / ".vcs" / "refs" / "heads" / "main"),
           setup.main_commit + "\n");

  setup.repo.checkout("main");
  CHECK_EQ(snapshot(setup.dir), MAIN_FILES);
}

TEST(checkout, removes_directories_left_empty) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "keep.txt", "k\n");
  repo.add({dir.path()});
  repo.commit("base", SIGNATURE);

  repo.create_branch("deep");
  repo.checkout("deep");
  write_file(dir / "a" / "b" / "c.txt", "c\n");
  repo.add({dir.path()});
  repo.commit("deep", SIGNATURE);

  repo.checkout("main");
  CHECK(!fs::exists(dir / "a"));
  CHECK(fs::exists(dir / "keep.txt"));
}

TEST(checkout, keeps_directories_holding_other_files) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "keep.txt", "k\n");
  repo.add({dir.path()});
  repo.commit("base", SIGNATURE);

  repo.create_branch("deep");
  repo.checkout("deep");
  write_file(dir / "a" / "c.txt", "c\n");
  repo.add({dir.path()});
  repo.commit("deep", SIGNATURE);

  repo.checkout("main");
  repo.checkout("deep");
  write_file(dir / "a" / "ignored.log", "local\n");
  write_file(dir / ".vcsignore", "*.log\n.vcsignore\n");

  repo.checkout("main");
  CHECK(!fs::exists(dir / "a" / "c.txt"));
  CHECK(fs::exists(dir / "a" / "ignored.log"));
}

TEST(checkout, file_becomes_directory_and_back) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a", "a file\n");
  repo.add({dir.path()});
  repo.commit("file", SIGNATURE);

  repo.create_branch("dir");
  repo.checkout("dir");
  fs::remove(dir / "a");
  write_file(dir / "a" / "b.txt", "inside\n");
  repo.add({dir.path()});
  repo.commit("dir", SIGNATURE);

  repo.checkout("main");
  CHECK_EQ(test::read_text(dir / "a"), std::string("a file\n"));

  repo.checkout("dir");
  CHECK_EQ(test::read_text(dir / "a" / "b.txt"), std::string("inside\n"));
  CHECK(clean(repo));
}

TEST(checkout, from_a_branch_with_no_commits) {
  TwoBranches setup;
  test::write_bytes(setup.dir / ".vcs" / "HEAD",
                    test::bytes("ref: refs/heads/unborn\n"));
  for (const std::string &name : {"a.txt", "shared.txt", "only_main.txt"})
    fs::remove(setup.dir / name);
  fs::remove_all(setup.dir / "dir");
  test::write_bytes(setup.dir / ".vcs" / "index", {});

  setup.repo.checkout("feature");
  CHECK_EQ(snapshot(setup.dir), FEATURE_FILES);
}

// -------- refusing to lose work --------

TEST(checkout, refuses_when_a_changed_file_would_be_overwritten) {
  TwoBranches setup;
  write_file(setup.dir / "a.txt", "precious\n");

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "a.txt"), std::string("precious\n"));
  CHECK_EQ(head_file(setup.dir), std::string("ref: refs/heads/main\n"));
  CHECK_EQ(test::read_text(setup.dir / "only_main.txt"), std::string("m\n"));
}

TEST(checkout, refuses_when_a_staged_file_would_be_overwritten) {
  TwoBranches setup;
  write_file(setup.dir / "dir" / "x.txt", "staged\n");
  setup.repo.add({setup.dir / "dir" / "x.txt"});

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "dir" / "x.txt"), std::string("staged\n"));
}

TEST(checkout, refuses_when_a_changed_file_would_be_deleted) {
  TwoBranches setup;
  write_file(setup.dir / "only_main.txt", "edited\n");

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "only_main.txt"), std::string("edited\n"));
}

TEST(checkout, refuses_when_a_staged_new_file_is_in_the_way) {
  TwoBranches setup;
  write_file(setup.dir / "only_feature.txt", "mine\n");
  setup.repo.add({setup.dir / "only_feature.txt"});

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "only_feature.txt"), std::string("mine\n"));
}

TEST(checkout, refuses_to_overwrite_an_untracked_file) {
  TwoBranches setup;
  write_file(setup.dir / "only_feature.txt", "untracked\n");

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "only_feature.txt"),
           std::string("untracked\n"));
  CHECK_EQ(test::read_text(setup.dir / "a.txt"), std::string("main\n"));
}

TEST(checkout, refuses_to_overwrite_an_ignored_file) {
  TwoBranches setup;
  write_file(setup.dir / ".vcsignore", "only_feature.txt\n");
  write_file(setup.dir / "only_feature.txt", "ignored but mine\n");

  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "only_feature.txt"),
           std::string("ignored but mine\n"));
}

TEST(checkout, refuses_when_an_untracked_file_blocks_a_directory) {
  TwoBranches setup;
  // feature has dir/x.txt; on another branch, "dir" holds untracked files
  setup.repo.create_branch("nodir");
  setup.repo.checkout("nodir");
  fs::remove_all(setup.dir / "dir");
  setup.repo.add({setup.dir / "dir"});
  setup.repo.commit("no dir", SIGNATURE);

  write_file(setup.dir / "dir", "an untracked file named dir\n");
  CHECK_THROWS(setup.repo.checkout("feature"));
  CHECK_EQ(test::read_text(setup.dir / "dir"),
           std::string("an untracked file named dir\n"));
}

TEST(checkout, refuses_when_untracked_files_fill_a_directory_to_replace) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a" / "tracked.txt", "t\n");
  repo.add({dir.path()});
  repo.commit("dir", SIGNATURE);

  repo.create_branch("file");
  repo.checkout("file");
  fs::remove_all(dir / "a");
  write_file(dir / "a", "now a file\n");
  repo.add({dir.path()});
  repo.commit("file", SIGNATURE);

  repo.checkout("main");
  write_file(dir / "a" / "untracked.txt", "mine\n");

  CHECK_THROWS(repo.checkout("file"));
  CHECK_EQ(test::read_text(dir / "a" / "untracked.txt"), std::string("mine\n"));
  CHECK_EQ(test::read_text(dir / "a" / "tracked.txt"), std::string("t\n"));
}

TEST(checkout, keeps_local_changes_to_unaffected_files) {
  TwoBranches setup;
  write_file(setup.dir / "shared.txt", "edited locally\n");
  write_file(setup.dir / "notes.txt", "untracked notes\n");
  write_file(setup.dir / "staged.txt", "s\n");
  setup.repo.add({setup.dir / "staged.txt"});

  setup.repo.checkout("feature");

  CHECK_EQ(test::read_text(setup.dir / "shared.txt"), std::string("edited locally\n"));
  CHECK_EQ(test::read_text(setup.dir / "notes.txt"), std::string("untracked notes\n"));
  CHECK_EQ(test::read_text(setup.dir / "a.txt"), std::string("feature\n"));

  vcs::StatusReport report = setup.repo.status();
  CHECK_EQ(report.staged.size(), size_t(1));
  CHECK_EQ(report.staged.at(0).path, std::string("staged.txt"));
  CHECK_EQ(report.unstaged.size(), size_t(1));
  CHECK_EQ(report.unstaged.at(0).path, std::string("shared.txt"));
}

TEST(checkout, refusal_lists_every_problem_file) {
  TwoBranches setup;
  write_file(setup.dir / "a.txt", "1\n");
  write_file(setup.dir / "dir" / "x.txt", "2\n");

  std::string message;
  try {
    setup.repo.checkout("feature");
  } catch (const std::exception &e) {
    message = e.what();
  }
  CHECK(message.find("a.txt") != std::string::npos);
  CHECK(message.find("dir/x.txt") != std::string::npos);
}

// -------- bad targets --------

TEST(checkout, unknown_target_throws) {
  TwoBranches setup;
  CHECK_THROWS(setup.repo.checkout("nope"));
  CHECK_THROWS(setup.repo.checkout("abc"));          // too short to be a prefix
  CHECK_THROWS(setup.repo.checkout(std::string(64, 'f')));
  CHECK_EQ(snapshot(setup.dir), MAIN_FILES);
}

TEST(checkout, prefix_of_a_non_commit_throws) {
  TwoBranches setup;
  // printf 'blob 5\0main\n' | shasum -a 256 is a blob, not a commit
  std::string blob =
      setup.repo.objects().write(vcs::wrap(vcs::ObjectType::Blob, test::bytes("main\n")));
  CHECK_THROWS(setup.repo.checkout(blob));
}

TEST(checkout, ambiguous_prefix_throws) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());

  // make commits until two share a 4-character prefix
  std::map<std::string, std::string> seen;
  std::string a, b;
  for (int i = 0; a.empty(); i++) {
    write_file(dir / "f.txt", std::to_string(i));
    repo.add({dir / "f.txt"});
    std::string hash = repo.commit("c" + std::to_string(i), SIGNATURE);
    auto [it, fresh] = seen.try_emplace(hash.substr(0, 4), hash);
    if (!fresh) {
      a = it->second;
      b = hash;
    }
  }

  CHECK_THROWS(repo.checkout(a.substr(0, 4)));
  CHECK_EQ(repo.checkout(a).commit, a);
}

// -------- trees that try to escape the repository --------

TEST(checkout, refuses_tree_entry_with_dot_dot) {
  test::TempDir outer;
  fs::path root = outer / "repo";
  vcs::Repository repo = vcs::Repository::init(root);
  write_file(root / "a.txt", "a\n");
  repo.add({root / "a.txt"});
  repo.commit("base", SIGNATURE);

  std::string blob =
      repo.objects().write(vcs::wrap(vcs::ObjectType::Blob, test::bytes("pwned\n")));
  std::string evil = forged_commit(repo, forged_tree(repo, "100644", "..", blob));

  CHECK_THROWS(repo.checkout(evil));
  CHECK(!fs::exists(outer / "pwned"));
  CHECK_EQ(test::read_text(root / "a.txt"), std::string("a\n"));
  CHECK(repo.current_branch().has_value());
}

TEST(checkout, refuses_nested_escape_through_a_subtree) {
  test::TempDir outer;
  fs::path root = outer / "repo";
  vcs::Repository repo = vcs::Repository::init(root);
  write_file(root / "a.txt", "a\n");
  repo.add({root / "a.txt"});
  repo.commit("base", SIGNATURE);

  std::string blob =
      repo.objects().write(vcs::wrap(vcs::ObjectType::Blob, test::bytes("pwned\n")));
  std::string inner = forged_tree(repo, "100644", "evil.txt", blob);
  std::string up = forged_tree(repo, "40000", "..", inner);
  std::string evil = forged_commit(repo, forged_tree(repo, "40000", "sub", up));

  CHECK_THROWS(repo.checkout(evil));
  CHECK(!fs::exists(outer / "evil.txt"));
  CHECK(!fs::exists(root / "evil.txt"));
}

TEST(checkout, refuses_tree_that_writes_into_vcs_dir) {
  test::TempDir dir;
  vcs::Repository repo = vcs::Repository::init(dir.path());
  write_file(dir / "a.txt", "a\n");
  repo.add({dir / "a.txt"});
  repo.commit("base", SIGNATURE);
  std::string head_before = head_file(dir);

  std::string blob = repo.objects().write(
      vcs::wrap(vcs::ObjectType::Blob, test::bytes("ref: refs/heads/evil\n")));
  std::string vcs_tree = forged_tree(repo, "100644", "HEAD", blob);
  std::string evil = forged_commit(repo, forged_tree(repo, "40000", ".vcs", vcs_tree));

  CHECK_THROWS(repo.checkout(evil));
  CHECK_EQ(head_file(dir), head_before);
}

TEST(checkout, refuses_to_write_through_an_untracked_symlinked_directory) {
  test::TempDir outer;
  fs::path root = outer / "repo";
  vcs::Repository repo = vcs::Repository::init(root);
  write_file(root / "a.txt", "a\n");
  repo.add({root / "a.txt"});
  repo.commit("base", SIGNATURE);

  repo.create_branch("with_sub");
  repo.checkout("with_sub");
  write_file(root / "sub" / "x.txt", "x\n");
  repo.add({root / "sub"});
  repo.commit("sub", SIGNATURE);
  repo.checkout("main");

  fs::create_directories(outer / "elsewhere");
  fs::create_directory_symlink(outer / "elsewhere", root / "sub");

  CHECK_THROWS(repo.checkout("with_sub"));
  CHECK(!fs::exists(outer / "elsewhere" / "x.txt"));
}
