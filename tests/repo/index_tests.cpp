#include "../test_harness.hpp"

#include "vcs/repo/index.hpp"

namespace {

const std::string HASH_A(64, 'a');
const std::string HASH_B(64, 'b');

std::string line(const std::string &hash, const std::string &path) {
  return "100644 " + hash + " " + path + "\n";
}

size_t count(const vcs::Index &index) { return index.get_entries().size(); }

bool has(const vcs::Index &index, const std::string &path) {
  return index.get_entries().contains(path);
}

} // namespace

TEST(index, load_empty_file) {
  test::TempDir dir;
  test::write_bytes(dir / "index", {});
  CHECK_EQ(count(vcs::Index::load(dir / "index")), size_t(0));
}

TEST(index, load_missing_file_throws) {
  test::TempDir dir;
  CHECK_THROWS(vcs::Index::load(dir / "index"));
}

TEST(index, save_is_sorted_text) {
  test::TempDir dir;
  vcs::Index index;
  index.add("z.txt", HASH_A);
  index.add("a/b.txt", HASH_B);
  index.save(dir / "index");

  CHECK_EQ(test::read_text(dir / "index"),
           line(HASH_B, "a/b.txt") + line(HASH_A, "z.txt"));
}

TEST(index, save_load_roundtrip) {
  test::TempDir dir;
  vcs::Index index;
  index.add("a.txt", HASH_A);
  index.add("dir/with space/b c.txt", HASH_B);
  index.save(dir / "index");

  vcs::Index loaded = vcs::Index::load(dir / "index");
  CHECK(loaded.get_entries() == index.get_entries());
}

TEST(index, save_leaves_no_temp_file) {
  test::TempDir dir;
  vcs::Index().save(dir / "index");
  CHECK(!std::filesystem::exists(dir / "index.tmp"));
}

TEST(index, add_same_path_updates_hash) {
  vcs::Index index;
  index.add("a.txt", HASH_A);
  index.add("a.txt", HASH_B);
  CHECK_EQ(count(index), size_t(1));
  CHECK_EQ(index.get_entries().at("a.txt"), HASH_B);
}

TEST(index, file_replaced_by_directory) {
  vcs::Index index;
  index.add("a", HASH_A);
  index.add("a/b/c.txt", HASH_B);
  CHECK(!has(index, "a"));
  CHECK(has(index, "a/b/c.txt"));
}

TEST(index, directory_replaced_by_file) {
  vcs::Index index;
  index.add("a/b.txt", HASH_A);
  index.add("a/c.txt", HASH_A);
  index.add("a", HASH_B);
  CHECK_EQ(count(index), size_t(1));
  CHECK(has(index, "a"));
}

TEST(index, similar_prefix_is_not_a_conflict) {
  vcs::Index index;
  index.add("src.txt", HASH_A);
  index.add("src/a.txt", HASH_B);
  index.add("src", HASH_A);
  CHECK(has(index, "src.txt"));
  CHECK(!has(index, "src/a.txt"));
}

TEST(index, remove_path_and_children) {
  vcs::Index index;
  index.add("a/b.txt", HASH_A);
  index.add("a/c/d.txt", HASH_A);
  index.add("a.txt", HASH_A);

  CHECK_EQ(index.remove("a"), size_t(2));
  CHECK_EQ(count(index), size_t(1));
  CHECK(has(index, "a.txt"));
}

TEST(index, remove_missing_returns_zero) {
  vcs::Index index;
  CHECK_EQ(index.remove("nope"), size_t(0));
}

TEST(index, paths_under) {
  vcs::Index index;
  index.add("src/a.txt", HASH_A);
  index.add("src/b/c.txt", HASH_A);
  index.add("src.txt", HASH_A);

  CHECK_EQ(index.paths_under("src"),
           (std::vector<std::string>{"src/a.txt", "src/b/c.txt"}));
  CHECK_EQ(index.paths_under("").size(), size_t(3));
}

TEST(index, rejects_unsafe_paths) {
  for (std::string path :
       {"", "/etc/passwd", "../x", "a/../../x", "a/./b", "a//b", "a/", ".vcs/HEAD",
        "a/.vcs/b", "a\nb"}) {
    vcs::Index index;
    CHECK_THROWS(index.add(path, HASH_A));
  }
}

TEST(index, rejects_bad_hash) {
  vcs::Index index;
  CHECK_THROWS(index.add("a", "abc"));
  CHECK_THROWS(index.add("a", std::string(64, 'Z')));
  CHECK_EQ(count(index), size_t(0));
}

TEST(index, load_rejects_malformed_lines) {
  std::vector<std::string> contents = {
      "garbage\n", "100644 " + HASH_A + "\n", "100755 " + HASH_A + " a\n",
      "100644 abc a\n", "100644 " + HASH_A + " ../../evil\n"};

  for (const std::string &content : contents) {
    test::TempDir dir;
    test::write_bytes(dir / "index", test::bytes(content));
    CHECK_THROWS(vcs::Index::load(dir / "index"));
  }
}
