// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_version.cpp && ./a.out
// The project version a follower puts in its `08` reply.
#include <cstdio>

#include "klink_version.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}

static bool is(const char *text, uint8_t major, uint8_t minor, uint8_t patch) {
  uint8_t a = 0, b = 0, c = 0;
  parse_version_string(text, a, b, c);
  return a == major && b == minor && c == patch;
}

int main() {
  check("plain 0.2.0", is("0.2.0", 0, 2, 0));
  check("v prefix", is("v0.2.0", 0, 2, 0));
  check("ESPHome v2 example", is("v2", 2, 0, 0));
  check("two fields", is("1.2", 1, 2, 0));
  check("one field", is("3", 3, 0, 0));
  check("suffix ignored", is("1.2.3-dev", 1, 2, 3));
  check("spaces ignored", is(" 0.2.0", 0, 2, 0));
  check("field capped at 255", is("2026.10.0", 255, 10, 0));
  check("empty", is("", 0, 0, 0));
  check("no digits", is("dev", 0, 0, 0));
  {
    uint8_t a = 9, b = 9, c = 9;
    parse_version_string(nullptr, a, b, c);
    check("null clears", a == 0 && b == 0 && c == 0);
  }
  return failures == 0 ? 0 : 1;
}
