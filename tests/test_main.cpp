#include <exception>
#include <iostream>
#include <string>

#include "test_harness.hpp"

// Usage: vcs_tests [suite | suite.name]
// Runs every registered test, only those in the given suite, or a single test.
int main(int argc, char **argv) {
  std::string filter = argc > 1 ? argv[1] : "";

  int passed = 0;
  int failed = 0;

  for (const test::TestCase &tc : test::registry()) {
    if (!filter.empty() && tc.suite != filter &&
        tc.suite + "." + tc.name != filter)
      continue;

    test::current_failures().clear();

    try {
      tc.fn();
    } catch (const std::exception &e) {
      test::current_failures().push_back(std::string("uncaught exception: ") +
                                         e.what());
    } catch (...) {
      test::current_failures().push_back("uncaught non-std exception");
    }

    std::string label = tc.suite + "." + tc.name;
    if (test::current_failures().empty()) {
      passed++;
      std::cout << "[ PASS ] " << label << "\n";
    } else {
      failed++;
      std::cout << "[ FAIL ] " << label << "\n";
      for (const std::string &msg : test::current_failures())
        std::cout << "    " << msg << "\n";
    }
  }

  if (passed + failed == 0) {
    std::cout << "no tests matched '" << filter << "'\n";
    return 1;
  }

  std::cout << "\n" << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}
