#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "vcs/util/bytes.hpp"

namespace test {

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> tests;
  return tests;
}

// Failures recorded by the currently running test
inline std::vector<std::string> &current_failures() {
  static std::vector<std::string> failures;
  return failures;
}

struct Registrar {
  Registrar(std::string suite, std::string name, std::function<void()> fn) {
    registry().push_back({std::move(suite), std::move(name), std::move(fn)});
  }
};

inline void fail(const char *file, int line, const std::string &msg) {
  std::ostringstream out;
  out << file << ":" << line << ": " << msg;
  current_failures().push_back(out.str());
}

// -------- value printing for failure messages --------
inline std::string describe(const vcs::ByteStream &bytes) {
  if (bytes.size() > 64)
    return "<" + std::to_string(bytes.size()) + " bytes> " +
           vcs::to_hex(std::span(bytes.data(), 64)) + "...";
  return "<" + std::to_string(bytes.size()) + " bytes> " + vcs::to_hex(bytes);
}

inline std::string describe(const std::string &s) { return "\"" + s + "\""; }
inline std::string describe(const char *s) { return describe(std::string(s)); }
inline std::string describe(bool b) { return b ? "true" : "false"; }

template <typename T> std::string describe(const T &value);

template <typename T> std::string describe(const std::vector<T> &values) {
  std::string out = "{";
  for (size_t i = 0; i < values.size(); i++) {
    if (i == 32) {
      out += ", ...";
      break;
    }
    out += (i ? ", " : "") + describe(values[i]);
  }
  return out + "}";
}

template <typename T> std::string describe(const T &value) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return std::to_string(value); // not as a char
  } else if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<std::underlying_type_t<T>>(value));
  } else {
    std::ostringstream out;
    out << value;
    return out.str();
  }
}

// -------- helpers --------

// ByteStream from a string literal; use the sized overload for embedded '\0'
inline vcs::ByteStream bytes(std::string_view s) {
  return vcs::ByteStream(s.begin(), s.end());
}

template <size_t N> vcs::ByteStream bytes_n(const char (&s)[N]) {
  // N includes the literal's trailing '\0', which is dropped
  return vcs::ByteStream(s, s + N - 1);
}

inline std::string as_string(const vcs::ByteStream &b) {
  return std::string(b.begin(), b.end());
}

inline std::string read_text(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

inline void write_bytes(const std::filesystem::path &path,
                        const vcs::ByteStream &data) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

// Unique directory under the system temp dir, removed on destruction
class TempDir {
private:
  std::filesystem::path dir;

public:
  TempDir() {
    static std::atomic<int> counter{0};
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    dir = std::filesystem::temp_directory_path() /
          ("vcs_test_" + std::to_string(stamp) + "_" +
           std::to_string(counter++));
    std::filesystem::create_directories(dir);
    // resolve symlinks (macOS /var -> /private/var) so path comparisons hold
    dir = std::filesystem::canonical(dir);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  const std::filesystem::path &path() const { return dir; }
  std::filesystem::path operator/(const std::filesystem::path &p) const {
    return dir / p;
  }
};

} // namespace test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST(suite, name)                                                      \
  static void test_##suite##_##name();                                         \
  static test::Registrar TEST_CONCAT(registrar_##suite##_##name, __LINE__)(    \
      #suite, #name, test_##suite##_##name);                                   \
  static void test_##suite##_##name()

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr))                                                               \
      test::fail(__FILE__, __LINE__, "CHECK(" #expr ") failed");               \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    const auto &check_a_ = (a);                                                \
    const auto &check_b_ = (b);                                                \
    if (!(check_a_ == check_b_))                                               \
      test::fail(__FILE__, __LINE__,                                           \
                 "CHECK_EQ(" #a ", " #b ") failed\n    left:  " +              \
                     test::describe(check_a_) +                                \
                     "\n    right: " + test::describe(check_b_));              \
  } while (0)

#define CHECK_THROWS(expr)                                                     \
  do {                                                                         \
    bool threw_ = false;                                                       \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (...) {                                                            \
      threw_ = true;                                                           \
    }                                                                          \
    if (!threw_)                                                               \
      test::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ") did not throw"); \
  } while (0)

#define CHECK_NOTHROW(expr)                                                    \
  do {                                                                         \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (const std::exception &e) {                                        \
      test::fail(__FILE__, __LINE__,                                           \
                 std::string("CHECK_NOTHROW(" #expr ") threw: ") + e.what());  \
    }                                                                          \
  } while (0)
