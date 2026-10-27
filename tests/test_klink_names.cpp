// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_names.cpp && ./a.out
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "klink_names.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  NameTable t;
  check("empty", t.size() == 0 && t.get(1) == nullptr && !t.contains(1));

  check("set/get", t.set(18001234, "Frag tank") && std::string(t.get(18001234)) == "Frag tank" && t.size() == 1);
  check("overwrite", t.set(18001234, "Tuna") && std::string(t.get(18001234)) == "Tuna" && t.size() == 1);
  check("remove", t.set(18001234, "") && t.get(18001234) == nullptr && t.size() == 0);
  check("remove missing ok", t.set(18001234, ""));

  check("too long rejected", !t.set(1, std::string(25, 'x')));
  check("max length accepted", t.set(1, std::string(24, 'x')) && std::string(t.get(1)).size() == 24);
  check("invalid utf8 rejected", !t.set(2, std::string("\xC3\x28", 2)));

  // Round trip.
  t.set(3, "Frag");
  t.set(4, "Reef");  // 4 bytes
  std::vector<uint8_t> blob = t.serialize();
  NameTable back;
  check("round trip", back.deserialize(blob) && back.size() == 3 && std::string(back.get(1)).size() == 24 &&
                          std::string(back.get(3)) == "Frag" && std::string(back.get(4)) == "Reef");

  std::vector<uint8_t> bad = blob; bad[3] ^= 1;
  NameTable keep; keep.set(9, "keep");
  check("bad crc rejected, state kept", !keep.deserialize(bad) && keep.size() == 1 && std::string(keep.get(9)) == "keep");
  bad = blob; bad[0] = 99;
  check("unknown version rejected", !keep.deserialize(bad));
  bad = blob; bad.pop_back();
  check("truncated rejected", !keep.deserialize(bad));
  bad = blob; bad[1] = 33;  // too many entries
  check("too many entries rejected", !keep.deserialize(bad));
  check("garbage rejected", !keep.deserialize(std::vector<uint8_t>({1, 200, 0, 0})));

  NameTable empty; check("empty round trip", keep.deserialize(empty.serialize()) && keep.size() == 0);

  // Capacity: 32 entries, one per kid.
  NameTable full;
  for (uint32_t i = 0; i < MAX_NAMES; i++) full.set(100 + i, "n");
  check("capacity", !full.set(999, "x") && full.size() == MAX_NAMES);
  full.set(100, "");  // freeing a slot lets a new kid in
  check("slot reuse after remove", full.set(999, "x") && full.size() == MAX_NAMES);

  check("utf8 ascii", NameTable::utf8_valid("abc"));
  check("utf8 two byte", NameTable::utf8_valid("\xC3\xA9"));
  check("utf8 three byte", NameTable::utf8_valid("\xE2\x82\xAC"));
  check("utf8 four byte", NameTable::utf8_valid("\xF0\x9F\x98\x80"));
  check("utf8 overlong", !NameTable::utf8_valid("\xC0\xAF"));
  check("utf8 surrogate", !NameTable::utf8_valid("\xED\xA0\x80"));
  check("utf8 too big", !NameTable::utf8_valid("\xF4\x90\x80\x80"));
  check("utf8 truncated", !NameTable::utf8_valid("\xE2\x82"));

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
