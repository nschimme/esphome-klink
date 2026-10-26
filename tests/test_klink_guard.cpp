// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_guard.cpp && ./a.out
#include <cstdio>
#include "klink_frame.h"
#include "klink_guard.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  // Classification of example frames: another controller's requests versus lamp replies.
  const Bytes probe = {0xA9, 0x04, 0x00, 0x00, 0x34, 0xC8, 0x5C};
  const Bytes group = {0xA9, 0x0B, 0x00, 0x00, 0x1F, 0x66, 0x33, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3B, 0x5C};
  const Bytes ident_reply = {0xA9, 0x06, 0x00, 0x00, 0x1C, 0x02, 0x01, 0xDB, 0x5C};
  const Bytes mute_ack = {0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C};
  check("probe is a controller frame", looks_like_controller_frame(probe));
  check("group write is a controller frame", looks_like_controller_frame(group));
  check("unmute/mute/identity requests are controller frames",
        looks_like_controller_frame(unmute(18001234)) && looks_like_controller_frame(mute(ALL_KIDS)) &&
            looks_like_controller_frame(identity(18001234)));
  check("branch query is a controller frame", looks_like_controller_frame(branch(18000000, 36000001)));
  check("identity reply is NOT a controller frame", !looks_like_controller_frame(ident_reply));
  check("mute ack is NOT a controller frame", !looks_like_controller_frame(mute_ack));
  check("unknown command is not classified", !looks_like_controller_frame(build(0x77, Bytes())));

  const uint32_t Q = 15000;
  {  // quiet bus: writes may be enabled only after the listening period
    BusGuard g(Q); std::string why;
    g.start(1000);
    check("refused while still listening", !g.request_transmit(5000, why) && why.find("still listening") != std::string::npos);
    check("allowed after the quiet period", g.request_transmit(1000 + Q, why) && g.transmitting());
  }
  {  // another controller's once-per-second traffic blocks enabling writes
    BusGuard g(Q); std::string why;
    g.start(0);
    for (uint32_t t = 1000; t <= 30000; t += 1000) g.on_foreign_frame(t);
    check("refused while another controller is active", !g.request_transmit(30500, why) && why.find("another controller") != std::string::npos);
    check("allowed once it has been silent for the quiet period", g.request_transmit(30000 + Q, why));
  }
  {  // a controller shows up while we are transmitting: stop at once and latch
    BusGuard g(Q); std::string why;
    g.start(0);
    check("start transmitting", g.request_transmit(Q, why));
    check("foreign frame while transmitting demands a stop", g.on_foreign_frame(Q + 100) && !g.transmitting() && g.conflict_latched());
    check("cannot resume until the bus is quiet", !g.request_transmit(Q + 200, why));
    check("resumes after the quiet period and clears the latch", g.request_transmit(Q + 100 + Q, why) && !g.conflict_latched());
  }
  {  // foreign frames while not transmitting do not latch
    BusGuard g(Q);
    g.start(0);
    check("no stop requested when idle", !g.on_foreign_frame(2000) && !g.conflict_latched() && g.foreign_frames() == 1);
  }
  {  // millis() wrap-around
    BusGuard g(Q); std::string why;
    g.start(0xFFFFF000u);
    check("wrap: still listening just after start", !g.request_transmit(0xFFFFF000u + 1000, why));
    check("wrap: allowed after wrapping past zero", g.request_transmit(0xFFFFF000u + Q + 5, why));
  }
  {  // the guard must be started
    BusGuard g(Q); std::string why;
    check("unstarted guard refuses", !g.request_transmit(100000, why));
  }
  {  // other_controller_recent
    BusGuard g(Q);
    g.start(0);
    g.on_foreign_frame(4000);
    check("recent while within the period", g.other_controller_recent(10000) && !g.other_controller_recent(4000 + Q));
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
