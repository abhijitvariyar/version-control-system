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
