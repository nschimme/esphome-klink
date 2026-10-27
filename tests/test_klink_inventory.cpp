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

  check("info on unknown kid", !inv.set_info(5, "A360X", true, 1, 3, 0));
  check("set info", inv.set_info(18001234, "A360X", true, 1, 3, 7) && inv.find(18001234)->model == "A360X" &&
                        inv.find(18001234)->has_firmware && inv.find(18001234)->fw_patch == 7);
  check("upsert keeps info", inv.upsert(18001234, 2, 2) && inv.find(18001234)->model == "A360X");
  check("new lamp has no info", inv.find(18001235)->model.empty() && !inv.find(18001235)->has_firmware);

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

  // capacity: bus lamps and local records have independent limits
  Inventory full;
  for (uint32_t i = 0; i < MAX_LAMPS; i++) full.upsert(100 + i, 2, 2);
  check("capacity", !full.upsert(999, 2, 2) && full.lamps().size() == MAX_LAMPS);
  check("local fits beside a full bus", full.upsert_local(5000, 1, 1) && full.bus_lamp_count() == MAX_LAMPS &&
                                             full.lamps().size() == MAX_LAMPS + 1);
  check("33rd bus lamp refused with locals", !full.upsert(5001, 2, 2) && full.bus_lamp_count() == MAX_LAMPS);
  std::vector<uint8_t> full_blob = full.serialize();
  check("blob holds only the bus lamps", full_blob.size() == 4u + 6u * MAX_LAMPS && full_blob[1] == MAX_LAMPS);

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
  check("gid allowed for its own type", gid_allowed_for_type(1, 1) && gid_allowed_for_type(2, 2) &&
                                            gid_allowed_for_type(3, 26));
  check("gid rejected for another type", !gid_allowed_for_type(1, 2) && !gid_allowed_for_type(2, 26) &&
                                             !gid_allowed_for_type(3, 1));
  check("custom gid allowed for every type", gid_allowed_for_type(1, 3) && gid_allowed_for_type(2, 25) &&
                                                 gid_allowed_for_type(3, 5));
  check("gid outside the ranges rejected", !gid_allowed_for_type(1, 0) && !gid_allowed_for_type(1, 41) &&
                                                !gid_allowed_for_type(1, 30));

  // assignment error codes and their user-facing text
  check("no assignment error", a.check_assignment_error(1001, 3) == AssignmentError::NONE);
  check("assignment error code matches string", a.check_assignment_error(7, 3) == AssignmentError::UNKNOWN_KID &&
                                                     a.check_assignment(7, 3) == assignment_error_text(AssignmentError::UNKNOWN_KID));
  check("assignment error text", std::string(assignment_error_text(AssignmentError::GROUP_TYPE_CONFLICT)) ==
                                     "group already holds a different lamp type");
  check("no error has no text", std::string(assignment_error_text(AssignmentError::NONE)).empty());

  // local records: the leader's own light, owned by the hub and never on the bus
  Inventory loc;
  check("upsert is not local", loc.upsert(3001, 2, 2) && !loc.find(3001)->local);
  check("upsert_local creates local", loc.upsert_local(3002, 1, 1) && loc.lamps().size() == 2 &&
                                          loc.find(3002)->local);
  check("upsert_local updates in place", loc.upsert_local(3002, 5, 1) && loc.lamps().size() == 2 &&
                                             loc.find(3002)->gid == 5 && loc.find(3002)->local);
  check("upsert_local bad type rejected", !loc.upsert_local(9, 2, 0) && !loc.upsert_local(9, 2, 4));
  check("upsert keeps local", loc.upsert(3002, 5, 1) && loc.find(3002)->local);
  check("bus lamp count ignores local", loc.bus_lamp_count() == 1);
  Inventory full_local;
  for (uint32_t i = 0; i < MAX_LOCAL_LAMPS; i++) full_local.upsert_local(200 + i, 1, 1);
  check("upsert_local capacity", !full_local.upsert_local(999, 1, 1) &&
                                     full_local.lamps().size() == MAX_LOCAL_LAMPS &&
                                     full_local.bus_lamp_count() == 0);
  check("a bus lamp still fits beside full locals", full_local.upsert(3001, 2, 2) &&
                                                        full_local.lamps().size() == MAX_LOCAL_LAMPS + 1);

  // local records are not stored in flash
  std::vector<uint8_t> mixed = loc.serialize();  // one bus lamp (3001) and one local lamp (3002)
  Inventory stored;
  check("local skipped on save", mixed.size() == 10u && mixed[1] == 1 &&
                                     stored.deserialize(mixed) && stored.lamps().size() == 1 &&
                                     stored.find(3001) != nullptr && stored.find(3002) == nullptr &&
                                     !stored.find(3001)->local);
  check("saved blob crc valid", Inventory::crc16(std::vector<uint8_t>(mixed.begin(), mixed.end() - 2)) ==
                                    ((static_cast<uint16_t>(mixed[mixed.size() - 2]) << 8) | mixed.back()));
  Inventory only_local;
  only_local.upsert_local(4001, 1, 1);
  check("only local saves empty blob", only_local.serialize().size() == 4 && only_local.serialize()[1] == 0);
  Inventory from_empty;
  check("empty local blob round trip", from_empty.deserialize(only_local.serialize()) &&
                                           from_empty.lamps().empty() && from_empty.bus_lamp_count() == 0);

  // assignment rules treat a local record like any other lamp
  Inventory mix;
  mix.upsert_local(5001, 3, 1);
  mix.upsert(5002, 4, 2);
  check("local type conflict", mix.check_assignment_error(5001, 4) == AssignmentError::GROUP_TYPE_CONFLICT &&
                                  mix.check_assignment_error(5002, 3) == AssignmentError::GROUP_TYPE_CONFLICT);
  mix.upsert(5003, 3, 1);
  check("local same-type group allowed", mix.check_assignment_error(5001, 3) == AssignmentError::NONE &&
                                            mix.check_assignment_error(5003, 3) == AssignmentError::NONE);
  check("bus lamp count with local group", mix.bus_lamp_count() == 2);

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
