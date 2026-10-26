// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_inventory.cpp && ./a.out
#include <cstdio>
#include "klink_inventory.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  Inventory inv;
  check("empty find", inv.find(1) == nullptr);
  check("upsert", inv.upsert(18001234, 2, 2) && inv.upsert(18001235, 2, 2) && inv.lamps().size() == 2);
  check("upsert updates", inv.upsert(18001235, 5, 2) && inv.lamps().size() == 2 && inv.find(18001235)->gid == 5);
  check("bad type rejected", !inv.upsert(9, 2, 0) && !inv.upsert(9, 2, 4));
  check("remove", inv.remove(18001235) && !inv.remove(18001235) && inv.lamps().size() == 1);
  inv.upsert(18001235, 2, 2);

  // round trip
  std::vector<uint8_t> blob = inv.serialize();
  Inventory back;
  check("round trip", back.deserialize(blob) && back.lamps().size() == 2 && back.find(18001234)->gid == 2 &&
                          back.find(18001235)->type == 2);
  std::vector<uint8_t> bad = blob; bad[3] ^= 1;
  Inventory untouched; untouched.upsert(1, 1, 1);
  check("bad crc rejected, state kept", !untouched.deserialize(bad) && untouched.lamps().size() == 1);
  bad = blob; bad[0] = 99;
  check("unknown version rejected", !untouched.deserialize(bad));
  bad = blob; bad.pop_back();
  check("truncated rejected", !untouched.deserialize(bad));
  check("garbage rejected", !untouched.deserialize(std::vector<uint8_t>({1, 200, 0, 0})));
  Inventory empty; check("empty round trip", untouched.deserialize(empty.serialize()) && untouched.lamps().empty());
  check("known crc", Inventory::crc16(std::vector<uint8_t>({'1', '2', '3', '4', '5', '6', '7', '8', '9'})) == 0x29B1);

  // capacity
  Inventory full;
  for (uint32_t i = 0; i < MAX_LAMPS; i++) full.upsert(100 + i, 2, 2);
  check("capacity", !full.upsert(999, 2, 2) && full.lamps().size() == MAX_LAMPS);

  // assignment rules (two type-2 Tuna Suns, one type-3 lamp)
  Inventory a;
  a.upsert(1001, 2, 2); a.upsert(1002, 2, 2); a.upsert(2001, 26, 3);
  check("move to free custom group", a.check_assignment(1001, 3).empty());
  check("release to own default", a.check_assignment(1001, 2).empty());
  check("unknown kid", !a.check_assignment(7, 3).empty());
  check("gid 0 rejected", !a.check_assignment(1001, 0).empty());
  check("gid 41 rejected", !a.check_assignment(1001, 41).empty());
  check("other type's default rejected", !a.check_assignment(1001, 26).empty() && !a.check_assignment(1001, 1).empty());
  check("26-40 non-default rejected", !a.check_assignment(1001, 30).empty());
  a.upsert(1002, 4, 2);
  check("same-type group allowed", a.check_assignment(1001, 4).empty());
  a.upsert(2001, 4, 3);  // pretend a type-3 lamp sits in group 4
  check("mixed types in one group rejected", !a.check_assignment(1001, 4).empty());
  check("default gids", default_gid(1) == 1 && default_gid(2) == 2 && default_gid(3) == 26 && default_gid(9) == 0);

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
