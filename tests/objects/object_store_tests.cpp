#include "../test_harness.hpp"

#include <cctype>

#include "vcs/hash/sha256.hpp"
#include "vcs/objects/object.hpp"
#include "vcs/objects/object_store.hpp"

#include "vcs/compress/deflate.hpp"

namespace fs = std::filesystem;

namespace {

vcs::ByteStream hello_blob() {
  return vcs::wrap(vcs::ObjectType::Blob, test::bytes("hello"));
}

const std::string HELLO_HASH =
    "8aec4e4876f854f688d0ebfc8f37598f38e5fd6903cccc850ca36591175aeb60";

// Lists every regular file under dir, relative to it
std::vector<std::string> files_under(const fs::path &dir) {
  std::vector<std::string> out;
  for (const auto &entry : fs::recursive_directory_iterator(dir))
    if (entry.is_regular_file())
      out.push_back(fs::relative(entry.path(), dir).string());
  return out;
}

} // namespace

TEST(object_store, write_returns_sha256) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  CHECK_EQ(store.write(hello_blob()), HELLO_HASH);
}

TEST(object_store, write_creates_sharded_path) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  CHECK(fs::is_regular_file(dir / HELLO_HASH.substr(0, 2) / HELLO_HASH.substr(2)));
}

TEST(object_store, file_is_compressed_buffer) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  fs::path path = dir / HELLO_HASH.substr(0, 2) / HELLO_HASH.substr(2);
  vcs::ByteStream on_disk = test::bytes(test::read_text(path));
  CHECK(on_disk != hello_blob());
  CHECK_EQ(on_disk, vcs::compress(hello_blob()));
}

TEST(object_store, hash_is_of_uncompressed_buffer) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  vcs::ByteStream blob =
      vcs::wrap(vcs::ObjectType::Blob, vcs::ByteStream(5000, 'q'));
  CHECK_EQ(store.write(blob), vcs::SHA256().scramble(blob));
}

TEST(object_store, read_decompresses) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  vcs::ByteStream blob =
      vcs::wrap(vcs::ObjectType::Blob, vcs::ByteStream(5000, 'q'));
  CHECK(store.read(store.write(blob)) == blob);
}

TEST(object_store, repetitive_blob_smaller_on_disk) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  vcs::ByteStream blob =
      vcs::wrap(vcs::ObjectType::Blob, vcs::ByteStream(100 * 1024, 'q'));
  std::string hash = store.write(blob);

  fs::path path = dir / hash.substr(0, 2) / hash.substr(2);
  CHECK(fs::file_size(path) < blob.size());
}

TEST(object_store, small_object_overhead_is_bounded) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  // too small to compress, so it is stored behind the 13-byte header
  fs::path path = dir / HELLO_HASH.substr(0, 2) / HELLO_HASH.substr(2);
  CHECK_EQ(fs::file_size(path), hello_blob().size() + 13);
}

TEST(object_store, wrong_object_under_a_hash_throws) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  // a valid compressed file, but of different content than its hash says
  fs::path path = dir / HELLO_HASH.substr(0, 2) / HELLO_HASH.substr(2);
  test::write_bytes(path, vcs::compress(test::bytes("blob 3\0bye")));
  CHECK_THROWS(store.read(HELLO_HASH));
}

TEST(object_store, no_flipped_bit_returns_wrong_bytes) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  std::string text;
  for (int i = 0; i < 100; i++)
    text += "the quick brown fox " + std::to_string(i) + "\n";
  vcs::ByteStream blob = vcs::wrap(vcs::ObjectType::Blob, test::bytes(text));
  std::string hash = store.write(blob);

  fs::path path = dir / hash.substr(0, 2) / hash.substr(2);
  vcs::ByteStream on_disk = test::bytes(test::read_text(path));

  for (size_t bit = 0; bit < on_disk.size() * 8; bit++) {
    vcs::ByteStream damaged = on_disk;
    damaged[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
    test::write_bytes(path, damaged);

    // a flip in unused padding decodes to the original, which is fine
    try {
      if (store.read(hash) != blob) {
        test::fail(__FILE__, __LINE__,
                   "wrong bytes returned after flipping bit " +
                       std::to_string(bit));
        return;
      }
    } catch (const std::exception &) {
    }
  }
}

TEST(object_store, corrupt_file_throws_on_read) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  fs::path path = dir / HELLO_HASH.substr(0, 2) / HELLO_HASH.substr(2);
  test::write_bytes(path, test::bytes("not a compressed object"));
  CHECK_THROWS(store.read(HELLO_HASH));
}

TEST(object_store, read_returns_written_bytes) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  vcs::ByteStream data;
  for (int i = 0; i < 1000; i++)
    data.push_back(static_cast<uint8_t>(i));

  std::string hash = store.write(data);
  CHECK_EQ(store.read(hash), data);
}

TEST(object_store, exists_before_and_after_write) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  CHECK(!store.exists(HELLO_HASH));
  store.write(hello_blob());
  CHECK(store.exists(HELLO_HASH));
}

TEST(object_store, write_is_idempotent) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  std::string first = store.write(hello_blob());
  std::string second = store.write(hello_blob());

  CHECK_EQ(first, second);
  CHECK_EQ(files_under(dir.path()).size(), size_t(1));
}

TEST(object_store, distinct_objects_distinct_files) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  std::string a = store.write(test::bytes("a"));
  std::string b = store.write(test::bytes("b"));

  CHECK(a != b);
  CHECK_EQ(store.read(a), test::bytes("a"));
  CHECK_EQ(store.read(b), test::bytes("b"));
}

TEST(object_store, empty_object) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  std::string hash = store.write({});
  CHECK_EQ(hash, vcs::SHA256().scramble({}));
  CHECK(store.read(hash).empty());
}

TEST(object_store, no_tmp_file_left_behind) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  for (const std::string &file : files_under(dir.path()))
    CHECK(file.find(".tmp") == std::string::npos);
}

TEST(object_store, read_missing_throws) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  CHECK_THROWS(store.read(HELLO_HASH));
}

TEST(object_store, wrong_length_hash_throws) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());

  CHECK_THROWS(store.read("abc"));
  CHECK_THROWS(store.read(HELLO_HASH + "0"));
  CHECK_THROWS(store.exists(""));
}

// -------- hash validation --------
// path_for only checks the length, so a crafted 64-char "hash" can point
// outside objects/.

TEST(object_store, rejects_non_hex_hash) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  CHECK_THROWS(store.exists(std::string(64, 'z')));
}

TEST(object_store, rejects_uppercase_hash) {
  test::TempDir dir;
  vcs::ObjectStore store(dir.path());
  store.write(hello_blob());

  std::string upper = HELLO_HASH;
  for (char &c : upper)
    c = static_cast<char>(std::toupper(c));
  CHECK_THROWS(store.read(upper));
}

TEST(object_store, rejects_path_traversal_hash) {
  test::TempDir dir;
  fs::create_directories(dir / "objects");
  vcs::ObjectStore store(dir / "objects");

  // shard ".." + 62 chars resolves to objects/../<62 chars>, outside the store
  std::string evil = ".." + std::string(62, 'a');
  test::write_bytes(dir / std::string(62, 'a'), test::bytes("secret"));
  // exists() never decompresses, so it can't pass by accident on bad content
  CHECK_THROWS(store.exists(evil));
  CHECK_THROWS(store.read(evil));
}
