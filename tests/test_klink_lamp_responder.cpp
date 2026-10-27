// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_lamp_responder.cpp && ./a.out
// The reply frames are the hardware-observed ones, with a synthetic KID (18001234).
#include <cstdio>
#include "klink_frame.h"
#include "klink_lamp_responder.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}

static const uint32_t KID = 18001234;
static const uint32_t KID2 = 18005678;

static LampResponder lamp() {
  LampResponder l;
  l.set_kid(KID);
  l.set_gid(2);
  l.set_type(2);
  l.set_model("A360X Tuna Sun");
  l.set_version(0, 2, 0);
  return l;
}

int main() {
  // Branch: in range answers with our KID, the high bound is exclusive.
  {
    LampResponder l = lamp();
    check("branch in range", l.handle(branch(18000000, 18002000), 0) ==
                                 Bytes({0xA9, 0x08, 0x00, 0x00, 0x18, 0x01, 0x12, 0xAD, 0x52, 0xCE, 0x5C}));
    check("branch low bound inclusive", l.handle(branch(KID, KID + 1), 0).size() == 11);
    check("branch high bound exclusive", l.handle(branch(KID - 1, KID), 0).empty());
    check("branch out of range", l.handle(branch(18002000, 18003000), 0).empty());
    check("branch while muted", (l.handle(mute(KID), 0) == Bytes({0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C})) &&
                                     l.handle(branch(18000000, 18002000), 0).empty());
  }

  // Mute acknowledges once and silences every query; a wrong KID changes nothing.
  {
    LampResponder l = lamp();
    check("mute another KID is ignored", l.handle(mute(KID + 1), 0).empty());
    check("still unmuted after a wrong mute", l.handle(identity(KID), 0).size() == 9);
    check("mute ack", l.handle(mute(KID), 0) == Bytes({0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C}));
    check("mute ack is repeated", l.handle(mute(KID), 0) == Bytes({0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C}));
    check("muted is silent to identity", l.handle(identity(KID), 0).empty());
    check("muted is silent to status", l.handle(build(CMD_STATUS, be32(KID)), 0).empty());
    check("muted is silent to firmware", l.handle(firmware_query(), 0).empty());
    check("muted is silent to model", l.handle(model_query(), 0).empty());
  }

  // Unmute has no reply and must work while muted.
  {
    LampResponder l = lamp();
    l.handle(mute(KID), 0);
    check("unmute has no reply", l.handle(unmute(KID), 0).empty());
    check("unmuted identity answers",
          l.handle(identity(KID), 0) == Bytes({0xA9, 0x06, 0x00, 0x00, 0x1C, 0x02, 0x01, 0xDB, 0x5C}));
  }

  // Broadcast mute and unmute.
  {
    LampResponder l = lamp();
    check("broadcast mute ack", l.handle(mute(ALL_KIDS), 0) == Bytes({0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C}));
    check("broadcast mute silences", l.handle(identity(KID), 0).empty());
    check("broadcast unmute has no reply", l.handle(unmute(ALL_KIDS), 0).empty());
    check("broadcast unmute restores", l.handle(identity(KID), 0).size() == 9);
  }

  // Identity, firmware, model and status.
  {
    LampResponder l = lamp();
    check("identity GID 2 type 2", l.handle(identity(KID), 0) ==
                                      Bytes({0xA9, 0x06, 0x00, 0x00, 0x1C, 0x02, 0x01, 0xDB, 0x5C}));
    check("identity for another KID is silent", l.handle(identity(KID + 1), 0).empty());
    check("firmware 0.2.0", l.handle(firmware_query(), 0) ==
                                Bytes({0xA9, 0x07, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0xEF, 0x5C}));
    FirmwareVersion fw;
    check("firmware parses", parse_firmware(l.handle(firmware_query(), 0), fw) && fw.major == 0 && fw.minor == 2 &&
                                 fw.patch == 0);
    const std::vector<uint8_t> model_payload = {'A', '3', '6', '0', 'X', ' ', 'T', 'u', 'n', 'a', ' ',
                                                'S', 'u', 'n', 0};
    check("model string", l.handle(model_query(), 0) == build(CMD_MODEL, model_payload));
    std::string model;
    check("model parses", parse_model(l.handle(model_query(), 0), model) && model == "A360X Tuna Sun");
    check("status", l.handle(build(CMD_STATUS, be32(KID)), 0) ==
                        Bytes({0xA9, 0x0B, 0x00, 0x00, 0x35, 0x01, 0x01, 0x01, 0x02, 0x03, 0x05, 0x03, 0xB0, 0x5C}));
    check("status for another KID is silent", l.handle(build(CMD_STATUS, be32(KID + 1)), 0).empty());
  }

  // The version defaults to 0.0.0 when the hub sets none: still a valid 10-byte reply.
  {
    LampResponder l;
    const std::vector<uint8_t> reply = l.handle(firmware_query(), 0);
    check("default firmware 0.0.0",
          reply == Bytes({0xA9, 0x07, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xF1, 0x5C}));
    FirmwareVersion fw;
    check("default firmware parses", parse_firmware(reply, fw) && fw.major == 0 && fw.minor == 0 && fw.patch == 0);
  }

  // Set GID: no reply, stored, and reported by the next identity query.
  {
    LampResponder l = lamp();
    bool gid_changed = false;
    check("set GID has no reply", l.handle(set_gid(KID, 7), 0, &gid_changed).empty() && gid_changed);
    check("set GID for another KID changes nothing", l.handle(set_gid(KID + 1, 8), 0).empty() && l.gid() == 7);
    gid_changed = true;
    check("set GID rejects another type's default",
          l.handle(set_gid(KID, 1), 0, &gid_changed).empty() && !gid_changed && l.gid() == 7);
    gid_changed = true;
    check("set GID rejects an out-of-range group",
          l.handle(set_gid(KID, 30), 0, &gid_changed).empty() && !gid_changed && l.gid() == 7);
    check("identity reports the new GID",
          l.handle(identity(KID), 0) == Bytes({0xA9, 0x06, 0x00, 0x00, 0x1C, 0x07, 0x01, 0xD6, 0x5C}));
  }

  // 1B is not gated by mute: the leader unmutes first, but a muted lamp still takes it.
  {
    LampResponder l = lamp();
    l.handle(mute(KID), 0);
    bool gid_changed = false;
    check("muted lamp takes a valid set GID",
          l.handle(set_gid(KID, 5), 0, &gid_changed).empty() && gid_changed && l.gid() == 5);
    gid_changed = true;
    check("muted lamp ignores an invalid set GID",
          l.handle(set_gid(KID, 1), 0, &gid_changed).empty() && !gid_changed && l.gid() == 5);
  }

  // Find needs a recent controller probe and flashes only the addressed lamp.
  {
    LampResponder l = lamp();
    bool flash = true;
    l.handle(find(KID), 0, nullptr, &flash);
    check("bare find does nothing", !flash);
    check("probe has no reply", l.handle(controller_probe(), 1000).empty());
    l.handle(find(KID), 1100, nullptr, &flash);
    check("find after a probe flashes", flash);
    l.handle(find(KID + 1), 1150, nullptr, &flash);
    check("find for another KID does nothing", !flash);
    l.handle(find(KID), 2500, nullptr, &flash);
    check("find outside the probe window does nothing", !flash);
    l.handle(controller_probe(), 2600);
    l.handle(find(KID), 3000, nullptr, &flash);
    check("a fresh probe re-arms find", flash);
  }

  // Anything else is ignored, including a request that is not a complete frame.
  {
    LampResponder l = lamp();
    check("unknown command", l.handle(build(0x77, {}), 0).empty());
    check("short frame", l.handle(Bytes({0xA9, 0x04, 0x00, 0x00, 0x08}), 0).empty());
    check("wrong marker", l.handle(Bytes({0x00, 0x04, 0x00, 0x00, 0x08, 0xF4, 0x5C}), 0).empty());
  }

  // A set of two lamps: a width-20,000 scan finds them one at a time.
  const Bytes MUTE_ACK = {0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C};
  const Bytes WIDE = branch(18000000, 18010000);
  auto two_lamps = []() {
    LampResponder a;
    a.set_kid(KID);
    a.set_gid(2);
    a.set_type(2);
    a.set_model("A360X Tuna Sun");
    LampResponder b;
    b.set_kid(KID2);
    b.set_gid(3);
    b.set_type(1);
    b.set_model("A360X Tuna Blue");
    LampSet set;
    set.add(a);
    set.add(b);
    set.set_version(0, 2, 0);
    return set;
  };
  {
    LampSet set = two_lamps();
    LampResult r = set.handle(WIDE, 0);
    check("set branch answers the lowest KID", r.reply == build(CMD_BRANCH, be32(KID)) && r.gid_changed == -1 &&
                                                 r.find == -1);
    check("set mutes the lowest KID", set.handle(mute(KID), 0).reply == MUTE_ACK);
    check("set branch answers the other KID",
          set.handle(WIDE, 0).reply == build(CMD_BRANCH, be32(KID2)));
    check("set mutes the other KID", set.handle(mute(KID2), 0).reply == MUTE_ACK);
    check("set branch is silent when both are muted", set.handle(WIDE, 0).reply.empty());
  }

  // Broadcast mute is acknowledged once and silences both; broadcast unmute brings both back.
  {
    LampSet set = two_lamps();
    check("set broadcast mute acked once", set.handle(mute(ALL_KIDS), 0).reply == MUTE_ACK);
    check("set broadcast mute silences branch", set.handle(WIDE, 0).reply.empty());
    check("set broadcast mute silences identity", set.handle(identity(KID), 0).reply.empty() &&
                                                      set.handle(identity(KID2), 0).reply.empty());
    check("set broadcast unmute has no reply", set.handle(unmute(ALL_KIDS), 0).reply.empty());
    check("set broadcast unmute restores both", set.handle(identity(KID), 0).reply.size() == 9 &&
                                                    set.handle(identity(KID2), 0).reply.size() == 9);
  }

  // Identity and status per KID.
  {
    LampSet set = two_lamps();
    check("set identity of the first",
          set.handle(identity(KID), 0).reply == Bytes({0xA9, 0x06, 0x00, 0x00, 0x1C, 0x02, 0x01, 0xDB, 0x5C}));
    check("set identity of the second",
          set.handle(identity(KID2), 0).reply == Bytes({0xA9, 0x06, 0x00, 0x00, 0x1C, 0x03, 0x00, 0xDB, 0x5C}));
    check("set identity of an unknown KID", set.handle(identity(KID + 1), 0).reply.empty());
    check("set status of the second", set.handle(build(CMD_STATUS, be32(KID2)), 0).reply ==
                                          Bytes({0xA9, 0x0B, 0x00, 0x00, 0x35, 0x01, 0x01, 0x01, 0x02, 0x03, 0x05, 0x03,
                                                 0xB0, 0x5C}));
  }

  // 08 and 20 are broadcast: they answer once, for the lowest unmuted lamp. The version is shared.
  {
    LampSet set = two_lamps();
    set.handle(mute(KID), 0);
    check("set firmware uses the shared version", set.handle(firmware_query(), 0).reply ==
                                                      Bytes({0xA9, 0x07, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0xEF, 0x5C}));
    std::string model;
    check("set model is the unmuted lamp's",
          parse_model(set.handle(model_query(), 0).reply, model) && model == "A360X Tuna Blue");
    set.handle(mute(KID2), 0);
    check("set firmware silent when all muted", set.handle(firmware_query(), 0).reply.empty());
    check("set model silent when all muted", set.handle(model_query(), 0).reply.empty());
  }

  // Set GID moves one lamp and reports its index; it is not gated by mute.
  {
    LampSet set = two_lamps();
    LampResult r = set.handle(set_gid(KID2, 9), 0);
    check("set GID has no reply", r.reply.empty() && r.gid_changed == 1 && r.find == -1);
    check("set GID moved only that lamp", set.lamps()[0].gid() == 2 && set.lamps()[1].gid() == 9);
    Identity id;
    check("set GID shows in identity", parse_identity(set.handle(identity(KID2), 0).reply, id) && id.gid == 9);
    check("set GID leaves the other lamp", parse_identity(set.handle(identity(KID), 0).reply, id) && id.gid == 2);
    LampResult bad = set.handle(set_gid(KID2, 2), 0);
    check("set GID rejects another type's default in a set",
          bad.reply.empty() && bad.gid_changed == -1 && set.lamps()[1].gid() == 9);
  }

  // A muted lamp in a set still takes 1B, and an invalid one is ignored.
  {
    LampSet set = two_lamps();
    set.handle(mute(KID2), 0);
    LampResult r = set.handle(set_gid(KID2, 4), 0);
    check("set muted lamp takes a valid set GID", r.gid_changed == 1 && set.lamps()[1].gid() == 4);
    r = set.handle(set_gid(KID2, 2), 0);
    check("set muted lamp ignores an invalid set GID", r.gid_changed == -1 && set.lamps()[1].gid() == 4);
  }

  // Find needs a recent probe and reports the matching lamp's index.
  {
    LampSet set = two_lamps();
    check("set bare find does nothing", set.handle(find(KID2), 0).find == -1);
    check("set probe has no reply", set.handle(controller_probe(), 1000).reply.empty());
    check("set find after a probe", set.handle(find(KID2), 1100).find == 1);
    check("set find of the other lamp", set.handle(find(KID), 1150).find == 0);
    check("set find of an unknown KID", set.handle(find(KID + 1), 1200).find == -1);
    check("set find outside the window", set.handle(find(KID2), 2500).find == -1);
    set.handle(controller_probe(), 2600);
    check("set fresh probe re-arms find", set.handle(find(KID2), 3000).find == 1);
  }

  // An empty set never replies, and a malformed frame is ignored.
  {
    LampSet set;
    check("empty set branch", set.handle(WIDE, 0).reply.empty());
    check("empty set mute", set.handle(mute(ALL_KIDS), 0).reply.empty());
    check("empty set firmware", set.handle(firmware_query(), 0).reply.empty());
    check("empty set model", set.handle(model_query(), 0).reply.empty());
    LampSet full = two_lamps();
    check("set malformed frame", full.handle(Bytes({0xA9, 0x04, 0x00, 0x00, 0x08}), 0).reply.empty());
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
