#include "../test_harness.hpp"

#include "vcs/objects/object.hpp"

using vcs::ObjectType;

// -------- type names --------

TEST(object, type_to_string) {
  CHECK_EQ(vcs::to_string(ObjectType::Blob), std::string("blob"));
  CHECK_EQ(vcs::to_string(ObjectType::Tree), std::string("tree"));
  CHECK_EQ(vcs::to_string(ObjectType::Commit), std::string("commit"));
}

TEST(object, type_from_string) {
  CHECK_EQ(vcs::object_type_from_string("blob"), ObjectType::Blob);
  CHECK_EQ(vcs::object_type_from_string("tree"), ObjectType::Tree);
  CHECK_EQ(vcs::object_type_from_string("commit"), ObjectType::Commit);
}

TEST(object, type_from_string_rejects_unknown) {
  CHECK_THROWS(vcs::object_type_from_string("tag"));
  CHECK_THROWS(vcs::object_type_from_string("Blob"));
  CHECK_THROWS(vcs::object_type_from_string(""));
}

// -------- wrap --------

TEST(object, wrap_blob_header) {
  CHECK_EQ(vcs::wrap(ObjectType::Blob, test::bytes("hello")),
           test::bytes_n("blob 5\0hello"));
}

TEST(object, wrap_tree_header) {
  CHECK_EQ(vcs::wrap(ObjectType::Tree, test::bytes("xyz")),
           test::bytes_n("tree 3\0xyz"));
}

TEST(object, wrap_commit_header) {
  CHECK_EQ(vcs::wrap(ObjectType::Commit, test::bytes("msg")),
           test::bytes_n("commit 3\0msg"));
}

TEST(object, wrap_empty_content) {
  CHECK_EQ(vcs::wrap(ObjectType::Blob, {}), test::bytes_n("blob 0\0"));
}

TEST(object, wrap_multi_digit_size) {
  vcs::ByteStream content(1234, 'x');
  vcs::ByteStream wrapped = vcs::wrap(ObjectType::Blob, content);

  vcs::ByteStream header = test::bytes_n("blob 1234\0");
  CHECK_EQ(wrapped.size(), header.size() + content.size());
  CHECK_EQ(vcs::ByteStream(wrapped.begin(), wrapped.begin() + header.size()),
           header);
}

// -------- unwrap round-trips --------

TEST(object, roundtrip_each_type) {
  for (ObjectType type :
       {ObjectType::Blob, ObjectType::Tree, ObjectType::Commit}) {
    vcs::ByteStream content = test::bytes("some content");
    vcs::UnwrappedObject obj = vcs::unwrap(vcs::wrap(type, content));

    CHECK_EQ(obj.get_object_type(), type);
    CHECK_EQ(obj.get_content_size(), content.size());
    CHECK_EQ(obj.get_content(), content);
  }
}

TEST(object, roundtrip_empty) {
  vcs::UnwrappedObject obj = vcs::unwrap(vcs::wrap(ObjectType::Blob, {}));
  CHECK_EQ(obj.get_content_size(), size_t(0));
  CHECK(obj.get_content().empty());
}

TEST(object, roundtrip_embedded_nul_and_spaces) {
  // content containing the header's own delimiters must not confuse unwrap
  vcs::ByteStream content = test::bytes_n("a \0b\0 c\0\0");
  vcs::UnwrappedObject obj = vcs::unwrap(vcs::wrap(ObjectType::Blob, content));
  CHECK_EQ(obj.get_content(), content);
}

TEST(object, roundtrip_all_byte_values) {
  vcs::ByteStream content;
  for (int i = 0; i < 256; i++)
    content.push_back(static_cast<uint8_t>(i));

  vcs::UnwrappedObject obj = vcs::unwrap(vcs::wrap(ObjectType::Blob, content));
  CHECK_EQ(obj.get_content(), content);
}

TEST(object, roundtrip_one_megabyte) {
  vcs::ByteStream content(1 << 20);
  for (size_t i = 0; i < content.size(); i++)
    content[i] = static_cast<uint8_t>(i * 31 + 7);

  vcs::UnwrappedObject obj = vcs::unwrap(vcs::wrap(ObjectType::Blob, content));
  CHECK_EQ(obj.get_content_size(), content.size());
  CHECK(obj.get_content() == content);
}

// -------- unwrap rejects well-formed-but-wrong headers --------

TEST(object, unwrap_unknown_type) {
  CHECK_THROWS(vcs::unwrap(test::bytes_n("tag 5\0hello")));
}

TEST(object, unwrap_size_too_large) {
  CHECK_THROWS(vcs::unwrap(test::bytes_n("blob 6\0hello")));
}

TEST(object, unwrap_size_too_small) {
  CHECK_THROWS(vcs::unwrap(test::bytes_n("blob 4\0hello")));
}

// -------- unwrap on malformed buffers --------
// Separate suite: without bounds checks these read past the end of the buffer,
// which aborts the process under ASan. Keeping them apart lets the `object`
// suite still report.

TEST(object_malformed, empty_buffer) {
  CHECK_THROWS(vcs::unwrap(vcs::ByteStream{}));
}

TEST(object_malformed, missing_space) {
  CHECK_THROWS(vcs::unwrap(test::bytes("blob")));
}

TEST(object_malformed, missing_nul) {
  CHECK_THROWS(vcs::unwrap(test::bytes("blob 5")));
}

TEST(object_malformed, non_digit_size) {
  // 'H' - '0' == 24, so naive digit parsing reads this size as 24 and the
  // check against the 24 trailing bytes passes
  vcs::ByteStream buffer = test::bytes_n("blob 0H\0");
  buffer.insert(buffer.end(), 24, 'x');
  CHECK_THROWS(vcs::unwrap(buffer));
}

TEST(object_malformed, empty_size) {
  CHECK_THROWS(vcs::unwrap(test::bytes_n("blob \0")));
}
