#include "../test_harness.hpp"

#include "vcs/objects/commit.hpp"
#include "vcs/objects/object.hpp"

using vcs::ByteStream;

namespace {

const std::string AUTHOR = "Ada <ada@example.com> 1700000000 +0000";
const std::string COMMITTER = "Bob <bob@example.com> 1700000001 +0000";

ByteStream hash_of(uint8_t fill) { return ByteStream(32, fill); }

std::string commit_content(vcs::Commit &commit) {
  vcs::UnwrappedObject obj = vcs::unwrap(commit.get_payload());
  CHECK_EQ(obj.get_object_type(), vcs::ObjectType::Commit);
  return test::as_string(obj.get_content());
}

} // namespace

TEST(commit, root_commit_no_parents) {
  vcs::Commit commit(hash_of(0x11), {}, AUTHOR, COMMITTER, "initial\n");

  CHECK_EQ(commit_content(commit),
           "tree " + vcs::to_hex(hash_of(0x11)) + "\n" +
               "author " + AUTHOR + "\n" +
               "committer " + COMMITTER + "\n" +
               "\n" +
               "initial\n");
}

TEST(commit, one_parent) {
  vcs::Commit commit(hash_of(0x11), {hash_of(0x22)}, AUTHOR, COMMITTER, "msg");

  CHECK_EQ(commit_content(commit),
           "tree " + vcs::to_hex(hash_of(0x11)) + "\n" +
               "parent " + vcs::to_hex(hash_of(0x22)) + "\n" +
               "author " + AUTHOR + "\n" +
               "committer " + COMMITTER + "\n\nmsg");
}

TEST(commit, two_parents_keep_order) {
  vcs::Commit commit(hash_of(0x11), {hash_of(0x22), hash_of(0x33)}, AUTHOR,
                     COMMITTER, "merge");

  CHECK_EQ(commit_content(commit),
           "tree " + vcs::to_hex(hash_of(0x11)) + "\n" +
               "parent " + vcs::to_hex(hash_of(0x22)) + "\n" +
               "parent " + vcs::to_hex(hash_of(0x33)) + "\n" +
               "author " + AUTHOR + "\n" +
               "committer " + COMMITTER + "\n\nmerge");
}

TEST(commit, header_size_matches_content) {
  vcs::Commit commit(hash_of(0x11), {}, AUTHOR, COMMITTER, "x");
  const ByteStream &payload = commit.get_payload();
  std::string expected_prefix = "commit " + std::to_string(commit_content(commit).size());

  CHECK_EQ(test::as_string(ByteStream(payload.begin(),
                                      payload.begin() + expected_prefix.size())),
           expected_prefix);
  CHECK_EQ(payload[expected_prefix.size()], uint8_t(0));
}

TEST(commit, multiline_message_preserved) {
  std::string message = "subject\n\nbody line 1\nbody line 2\n";
  vcs::Commit commit(hash_of(0x11), {}, AUTHOR, COMMITTER, message);

  std::string content = commit_content(commit);
  CHECK(content.size() >= message.size());
  CHECK_EQ(content.substr(content.size() - message.size()), message);
}

TEST(commit, different_parents_different_payload) {
  vcs::Commit a(hash_of(0x11), {hash_of(0x22)}, AUTHOR, COMMITTER, "m");
  vcs::Commit b(hash_of(0x11), {hash_of(0x23)}, AUTHOR, COMMITTER, "m");
  CHECK(a.get_payload() != b.get_payload());
}

TEST(commit, three_parents_throws) {
  CHECK_THROWS(vcs::Commit(hash_of(0x11),
                           {hash_of(1), hash_of(2), hash_of(3)}, AUTHOR,
                           COMMITTER, "m"));
}

TEST(commit, bad_tree_hash_length_throws) {
  CHECK_THROWS(vcs::Commit(ByteStream(31, 0x11), {}, AUTHOR, COMMITTER, "m"));
  CHECK_THROWS(vcs::Commit(ByteStream(64, 0x11), {}, AUTHOR, COMMITTER, "m"));
}

TEST(commit, bad_parent_hash_length_throws) {
  CHECK_THROWS(vcs::Commit(hash_of(0x11), {ByteStream(20, 0x22)}, AUTHOR,
                           COMMITTER, "m"));
}

// -------- parse_commit --------

namespace {

vcs::ByteStream content_of(vcs::Commit &commit) {
  vcs::UnwrappedObject object = vcs::unwrap(commit.get_payload());
  return object.get_content();
}

} // namespace

TEST(commit, parse_roundtrip) {
  vcs::ByteStream tree(32, 0x11);
  vcs::ByteStream parent_a(32, 0x22);
  vcs::ByteStream parent_b(32, 0x33);
  vcs::Commit commit(tree, {parent_a, parent_b}, "Ada <a@b> 1 +0000",
                     "Bob <c@d> 2 +0100", "subject\n\nbody\n");

  vcs::ParsedCommit parsed = vcs::parse_commit(content_of(commit));
  CHECK_EQ(parsed.tree_hash, vcs::to_hex(tree));
  CHECK_EQ(parsed.parent_hashes,
           (std::vector<std::string>{vcs::to_hex(parent_a), vcs::to_hex(parent_b)}));
  CHECK_EQ(parsed.author, std::string("Ada <a@b> 1 +0000"));
  CHECK_EQ(parsed.committer, std::string("Bob <c@d> 2 +0100"));
  CHECK_EQ(parsed.message, std::string("subject\n\nbody\n"));
}

TEST(commit, parse_no_parents) {
  vcs::Commit commit(vcs::ByteStream(32, 0x11), {}, "a", "b", "m\n");
  CHECK(vcs::parse_commit(content_of(commit)).parent_hashes.empty());
}

TEST(commit, parse_rejects_malformed) {
  for (std::string text :
       {"", "tree abc\n", "tree abc\nauthor a\ncommitter b\n", "garbage\n\nmsg",
        "unknown value\n\nmsg", "author a\ncommitter b\n\nmsg",
        "tree abc\ncommitter b\n\nmsg", "tree abc\nauthor a\n\nmsg"}) {
    CHECK_THROWS(vcs::parse_commit(test::bytes(text)));
  }
}
