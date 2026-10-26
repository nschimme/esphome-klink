// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_frame.cpp && ./a.out
// Example frames, with a synthetic KID (18001234).
#include <cstdio>
#include "klink_frame.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  check("unmute", unmute(18001234) == Bytes({0xA9, 0x08, 0, 0, 0x1A, 0x01, 0x12, 0xAD, 0x52, 0xCC, 0x5C}));
  check("identity", identity(18001234) == Bytes({0xA9, 0x08, 0, 0, 0x1C, 0x01, 0x12, 0xAD, 0x52, 0xCA, 0x5C}));
  check("mute", mute(18001234) == Bytes({0xA9, 0x08, 0, 0, 0x19, 0x01, 0x12, 0xAD, 0x52, 0xCD, 0x5C}));
  check("mute all", mute(ALL_KIDS) == Bytes({0xA9, 0x08, 0, 0, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0x5C}));
  check("unmute all", unmute(ALL_KIDS) == Bytes({0xA9, 0x08, 0, 0, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF, 0xE2, 0x5C}));
  const uint8_t dim[5] = {40, 40, 0, 0, 0}, full[5] = {127, 127, 0, 0, 0};
  check("group 40", group_channels(2, dim) == Bytes({0xA9, 0x0B, 0, 0, 0x1F, 0x28, 0x28, 0, 0, 0, 0x02, 0, 0x84, 0x5C}));
  check("group 127", group_channels(2, full) == Bytes({0xA9, 0x0B, 0, 0, 0x1F, 0x7F, 0x7F, 0, 0, 0, 0x02, 0, 0xD6, 0x5C}));

  Identity id;
  Bytes ok = {0xA9, 0x06, 0, 0, 0x1C, 0x02, 0x01, 0xDB, 0x5C};  // example reply: GID 2, type 2
  check("parse identity", parse_identity(ok, id) && id.gid == 2 && id.type == 2);
  Bytes bad_sum = ok; bad_sum[7] ^= 1;
  check("bad checksum", !parse_identity(bad_sum, id));
  check("wrong command", !valid_reply(ok, CMD_GET_GID_TYPE + 1));
  check("FAILED text", !valid_reply(Bytes({'F', 'A', 'I', 'L', 'E', 'D'}), CMD_GET_GID_TYPE));
  check("truncated", !valid_reply(Bytes(ok.begin(), ok.end() - 1), CMD_GET_GID_TYPE));
  check("mute ack is not an identity", !parse_identity(Bytes({0xA9, 0x05, 0, 0, 0x19, 0xFF, 0xE3, 0x5C}), id));

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
