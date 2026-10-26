// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_http.cpp && ./a.out
#include <cstdio>
#include <string>
#include "klink_http.h"

using namespace esphome::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  uint32_t v = 0;

  check("zero", parse_uint("0", 100, v) && v == 0);
  check("max", parse_uint("100", 100, v) && v == 100);
  check("leading zeros", parse_uint("007", 100, v) && v == 7);
  check("empty rejected", !parse_uint("", 100, v));
  check("non-digit rejected", !parse_uint("12a", 100, v));
  check("sign rejected", !parse_uint("-1", 100, v) && !parse_uint("+1", 100, v));
  check("space rejected", !parse_uint(" 1", 100, v) && !parse_uint("1 ", 100, v));
  check("overflow rejected", !parse_uint("101", 100, v) && !parse_uint("9999999999", 100, v));
  check("uint32 max", parse_uint("4294967295", 4294967295u, v) && v == 4294967295u);
  check("uint32 overflow", !parse_uint("4294967296", 4294967295u, v));
  check("max zero", parse_uint("0", 0, v) && v == 0 && !parse_uint("1", 0, v));

  check("plain", json_escape("abc") == "abc");
  check("quote", json_escape("a\"b") == "a\\\"b");
  check("backslash", json_escape("a\\b") == "a\\\\b");
  check("newline", json_escape("a\nb") == "a\\nb");
  check("tab", json_escape("a\tb") == "a\\tb");
  check("control", json_escape(std::string("\x01", 1)) == "\\u0001");
  check("utf8 passthrough", json_escape("\xC3\xA9") == "\xC3\xA9");

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
