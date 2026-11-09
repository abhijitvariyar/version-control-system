#include "../test_harness.hpp"

#include "vcs/repo/refs.hpp"

namespace fs = std::filesystem;

namespace {

const std::string HASH_A(64, 'a');
const std::string HASH_B(64, 'b');

void write_text(const fs::path &path, const std::string &text) {
  test::write_bytes(path, test::bytes(text));
}

std::string head_of(const vcs::Refs &refs) { return refs.head_commit().value(); }

void make_vcs_dir(const test::TempDir &dir) {
  fs::create_directories(dir / "refs" / "heads");
  write_text(dir / "HEAD", "ref: refs/heads/main\n");
}

} // namespace

TEST(refs, unborn_branch_has_no_commit) {
  test::TempDir dir;
  make_vcs_dir(dir);
  CHECK(!vcs::Refs(dir.path()).head_commit().has_value());
}

TEST(refs, update_writes_branch_file) {
  test::TempDir dir;
  make_vcs_dir(dir);

  vcs::Refs refs(dir.path());
  refs.update_head(HASH_A);

  CHECK_EQ(test::read_text(dir / "refs" / "heads" / "main"), HASH_A + "\n");
  CHECK_EQ(test::read_text(dir / "HEAD"), std::string("ref: refs/heads/main\n"));
  CHECK_EQ(head_of(refs), HASH_A);
}

TEST(refs, update_overwrites) {
  test::TempDir dir;
  make_vcs_dir(dir);

  vcs::Refs refs(dir.path());
  refs.update_head(HASH_A);
  refs.update_head(HASH_B);
  CHECK_EQ(head_of(refs), HASH_B);
}

TEST(refs, nested_branch_name) {
  test::TempDir dir;
  make_vcs_dir(dir);
  write_text(dir / "HEAD", "ref: refs/heads/feature/x\n");

  vcs::Refs refs(dir.path());
  refs.update_head(HASH_A);
  CHECK(fs::exists(dir / "refs" / "heads" / "feature" / "x"));
  CHECK_EQ(head_of(refs), HASH_A);
}

TEST(refs, detached_head) {
  test::TempDir dir;
  make_vcs_dir(dir);
  write_text(dir / "HEAD", HASH_A + "\n");

  vcs::Refs refs(dir.path());
  CHECK_EQ(head_of(refs), HASH_A);

  refs.update_head(HASH_B);
  CHECK_EQ(test::read_text(dir / "HEAD"), HASH_B + "\n");
  CHECK(!fs::exists(dir / "refs" / "heads" / "main"));
}

TEST(refs, update_rejects_bad_hash) {
  test::TempDir dir;
  make_vcs_dir(dir);
  CHECK_THROWS(vcs::Refs(dir.path()).update_head("abc"));
}

TEST(refs, corrupt_branch_file_throws) {
  test::TempDir dir;
  make_vcs_dir(dir);
  write_text(dir / "refs" / "heads" / "main", "not a hash\n");
  CHECK_THROWS(vcs::Refs(dir.path()).head_commit());
}

TEST(refs, corrupt_head_throws) {
  test::TempDir dir;
  make_vcs_dir(dir);
  write_text(dir / "HEAD", "garbage\n");
  CHECK_THROWS(vcs::Refs(dir.path()).head_commit());
}

TEST(refs, head_ref_cannot_leave_refs_heads) {
  for (std::string head : {"ref: refs/heads/../../../evil\n", "ref: /etc/passwd\n",
                           "ref: objects/aa\n"}) {
    test::TempDir dir;
    make_vcs_dir(dir);
    write_text(dir / "HEAD", head);
    CHECK_THROWS(vcs::Refs(dir.path()).update_head(HASH_A));
  }
}

// -------- branches --------

TEST(refs, valid_branch_names) {
  for (std::string name : {"main", "feature/x", "fix-123", "a.b", "v1.0/rc_2", "UPPER"})
    CHECK_NOTHROW(vcs::validate_branch_name(name));
}

TEST(refs, invalid_branch_names) {
  for (std::string name :
       {"", "-b", "HEAD", "a..b", "../x", "/a", "a/", "a//b", ".hidden", "a/.b",
        "a.lock", "a/b.lock", "end.", "has space", "tab\there", "a~b", "a^b",
        "a:b", "a?b", "a*b", "a[b", "a\\b", "a@{b", "new\nline"})
    CHECK_THROWS(vcs::validate_branch_name(name));
}

TEST(refs, create_and_list_branches) {
  test::TempDir dir;
  make_vcs_dir(dir);
  vcs::Refs refs(dir.path());

  CHECK(refs.branches().empty());
  refs.create_branch("main", HASH_A);
  refs.create_branch("feature/x", HASH_B);
  refs.create_branch("b", HASH_A);

  CHECK_EQ(refs.branches(), (std::vector<std::string>{"b", "feature/x", "main"}));
  CHECK_EQ(refs.branch_commit("feature/x").value_or(""), HASH_B);
  CHECK(!refs.branch_commit("missing").has_value());
  CHECK(!refs.branch_commit("../../HEAD").has_value());
}

TEST(refs, create_existing_branch_throws) {
  test::TempDir dir;
  make_vcs_dir(dir);
  vcs::Refs refs(dir.path());
  refs.create_branch("x", HASH_A);
  CHECK_THROWS(refs.create_branch("x", HASH_B));
  CHECK_EQ(refs.branch_commit("x").value_or(""), HASH_A);
}

TEST(refs, branch_names_cannot_clash_as_file_and_directory) {
  test::TempDir dir;
  make_vcs_dir(dir);
  vcs::Refs refs(dir.path());

  refs.create_branch("a", HASH_A);
  CHECK_THROWS(refs.create_branch("a/b", HASH_A));

  refs.create_branch("c/d", HASH_A);
  CHECK_THROWS(refs.create_branch("c", HASH_A));
}

TEST(refs, create_rejects_invalid_input) {
  test::TempDir dir;
  make_vcs_dir(dir);
  vcs::Refs refs(dir.path());
  CHECK_THROWS(refs.create_branch("../evil", HASH_A));
  CHECK_THROWS(refs.create_branch("ok", "not a hash"));
  CHECK(!fs::exists(dir / "evil"));
}

TEST(refs, point_head_and_detach) {
  test::TempDir dir;
  make_vcs_dir(dir);
  vcs::Refs refs(dir.path());
  refs.create_branch("feature", HASH_B);

  refs.point_head_at_branch("feature");
  CHECK_EQ(test::read_text(dir / "HEAD"), std::string("ref: refs/heads/feature\n"));
  CHECK_EQ(refs.current_branch().value_or(""), std::string("feature"));
  CHECK_EQ(head_of(refs), HASH_B);

  refs.detach_head(HASH_A);
  CHECK_EQ(test::read_text(dir / "HEAD"), HASH_A + "\n");
  CHECK(!refs.current_branch().has_value());

  CHECK_THROWS(refs.point_head_at_branch("bad name"));
  CHECK_THROWS(refs.detach_head("abc"));
}

TEST(refs, head_with_invalid_branch_name_throws) {
  test::TempDir dir;
  make_vcs_dir(dir);
  write_text(dir / "HEAD", "ref: refs/heads/a..b\n");
  CHECK_THROWS(vcs::Refs(dir.path()).head_commit());
}
