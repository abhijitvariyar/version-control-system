#include "../test_harness.hpp"

#include <string>

#include "vcs/hash/sha256.hpp"

namespace {

std::string sha(const vcs::ByteStream &data) {
  return vcs::SHA256().scramble(data);
}

std::string sha(std::string_view s) { return sha(test::bytes(s)); }

} // namespace

// -------- NIST FIPS 180-2 vectors --------

TEST(sha256, empty_string) {
  CHECK_EQ(sha(""),
           std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(sha256, abc) {
  CHECK_EQ(sha("abc"),
           std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(sha256, nist_448_bit) {
  CHECK_EQ(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
           std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256, nist_896_bit) {
  CHECK_EQ(sha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
               "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
           std::string("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
}

TEST(sha256, one_million_a) {
  CHECK_EQ(sha(std::string(1000000, 'a')),
           std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

// -------- padding boundaries (expected values from `shasum -a 256`) --------
// 55 bytes: largest message whose padding fits in one block
// 56..63:   length field spills into a second block
// 64:       exactly one full block of message

TEST(sha256, padding_55) {
  CHECK_EQ(sha(std::string(55, 'a')),
           std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
}

TEST(sha256, padding_56) {
  CHECK_EQ(sha(std::string(56, 'a')),
           std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
}

TEST(sha256, padding_63) {
  CHECK_EQ(sha(std::string(63, 'a')),
           std::string("7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"));
}

TEST(sha256, padding_64) {
  CHECK_EQ(sha(std::string(64, 'a')),
           std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
}

TEST(sha256, padding_65) {
  CHECK_EQ(sha(std::string(65, 'a')),
           std::string("635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"));
}

TEST(sha256, padding_119) {
  CHECK_EQ(sha(std::string(119, 'a')),
           std::string("31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"));
}

TEST(sha256, padding_120) {
  CHECK_EQ(sha(std::string(120, 'a')),
           std::string("2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"));
}

TEST(sha256, padding_128) {
  CHECK_EQ(sha(std::string(128, 'a')),
           std::string("6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"));
}

// -------- byte handling --------

TEST(sha256, all_byte_values) {
  // catches sign-extension bugs on bytes >= 0x80
  vcs::ByteStream data;
  for (int i = 0; i < 256; i++)
    data.push_back(static_cast<uint8_t>(i));

  CHECK_EQ(sha(data),
           std::string("40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"));
}

TEST(sha256, output_is_lowercase_hex) {
  std::string digest = sha("abc");
  CHECK_EQ(digest.size(), size_t(64));
  for (char c : digest)
    CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

TEST(sha256, instance_is_reusable) {
  vcs::SHA256 hasher;
  std::string first = hasher.scramble(test::bytes("abc"));
  std::string other = hasher.scramble(test::bytes("something else"));
  std::string second = hasher.scramble(test::bytes("abc"));

  CHECK_EQ(first, second);
  CHECK(first != other);
}
