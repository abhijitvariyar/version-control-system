#include "../test_harness.hpp"

#include "vcs/hash/sha256.hpp"
#include "vcs/objects/blob.hpp"
#include "vcs/objects/object.hpp"

TEST(blob, payload_has_header) {
  test::TempDir dir;
  test::write_bytes(dir / "a.txt", test::bytes("hello"));

  vcs::Blob blob(dir / "a.txt");
  CHECK_EQ(blob.get_blob(), test::bytes_n("blob 5\0hello"));
}

TEST(blob, hash_matches_shasum) {
  // printf 'blob 5\0hello' | shasum -a 256
  test::TempDir dir;
  test::write_bytes(dir / "a.txt", test::bytes("hello"));

  vcs::Blob blob(dir / "a.txt");
  CHECK_EQ(vcs::SHA256().scramble(blob.get_blob()),
           std::string("8aec4e4876f854f688d0ebfc8f37598f38e5fd6903cccc850ca36591175aeb60"));
}

TEST(blob, empty_file) {
  // printf 'blob 0\0' | shasum -a 256
  test::TempDir dir;
  test::write_bytes(dir / "empty", {});

  vcs::Blob blob(dir / "empty");
  CHECK_EQ(blob.get_blob(), test::bytes_n("blob 0\0"));
  CHECK_EQ(vcs::SHA256().scramble(blob.get_blob()),
           std::string("473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813"));
}

TEST(blob, binary_file_roundtrip) {
  test::TempDir dir;
  vcs::ByteStream content;
  for (int i = 0; i < 256; i++)
    content.push_back(static_cast<uint8_t>(i));
  test::write_bytes(dir / "bin", content);

  vcs::Blob blob(dir / "bin");
  vcs::UnwrappedObject obj = vcs::unwrap(blob.get_blob());

  CHECK_EQ(obj.get_object_type(), vcs::ObjectType::Blob);
  CHECK_EQ(obj.get_content(), content);
}

TEST(blob, same_content_same_payload) {
  test::TempDir dir;
  test::write_bytes(dir / "one", test::bytes("same"));
  test::write_bytes(dir / "two", test::bytes("same"));

  CHECK_EQ(vcs::Blob(dir / "one").get_blob(), vcs::Blob(dir / "two").get_blob());
}

TEST(blob, missing_file_throws) {
  test::TempDir dir;
  CHECK_THROWS(vcs::Blob(dir / "does_not_exist"));
}
