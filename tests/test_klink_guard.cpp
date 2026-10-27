// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_guard.cpp && ./a.out
#include <cstdio>
#include "klink_frame.h"
#include "klink_guard.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}

static ChainGuard leader(uint32_t beacon = 1000, uint32_t confirm = 2000, uint32_t holdoff = 3000,
                         uint32_t release = 10000) {
  ChainGuard g(beacon, confirm, holdoff, release);
  g.start(0);
  g.set_want_writes(true);
  return g;
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

  {  // A leader announces itself, then claims after the confirmation window.
    ChainGuard g = leader();
    ChainGuard::Update u = g.update(0);
    check("leader beacons immediately", u.send_beacon && !u.controlling && g.state() == ChainState::CLAIMING);
    u = g.update(1000);
    check("second beacon after the interval", u.send_beacon && !u.controlling);
    u = g.update(2000);
    check("controls after the confirmation window", u.controlling && g.state() == ChainState::CONTROLLING);
    check("a leader keeps its beacon cadence", g.update(3000).send_beacon);
    check("beacons sent counted", g.beacons_sent() >= 3);
  }
  {  // An AP9X or another controller's beacon does not block ownership. Only a real write does.
    ChainGuard g = leader();
    g.update(0);
    for (uint32_t t = 1000; t <= 5000; t += 1000)
      g.on_beacon(t);
    ChainGuard::Update u = g.update(2000);
    check("a foreign beacon alone does not block the claim", u.controlling);
    check("foreign beacons are counted", g.beacons_heard() == 5);
  }
  {  // A foreign group write holds the claim until it has been quiet for the holdoff.
    ChainGuard g = leader();
    g.update(0);
    g.on_foreign_write(1000);
    ChainGuard::Update u = g.update(3000);
    check("claim held while the other controller was writing recently",
          !u.controlling && g.state() == ChainState::CLAIMING);
    u = g.update(2000 + 3000);
    check("claim once the foreign write is older than the holdoff", u.controlling);
  }
  {  // A write while controlling is a conflict; it pauses and then claims again by itself.
    ChainGuard g = leader();
    g.update(0);
    check("controls before the conflict", g.update(2000).controlling);
    g.on_foreign_write(2500);
    check("conflict latched on a foreign write while controlling", g.conflict());
    ChainGuard::Update u = g.update(2500);
    check("conflict sends no beacon and no writes", !u.send_beacon && !u.controlling);
    u = g.update(12000);
    check("conflict holds for its release window", !u.controlling && g.state() == ChainState::CONFLICT);
    u = g.update(12500);
    check("conflict releases into claiming", !u.controlling && g.state() == ChainState::CLAIMING && u.send_beacon);
    u = g.update(14500);
    check("re-claims after the confirmation window", u.controlling);
    check("conflicts counted", g.conflicts() == 1);
  }
  {  // Retry clears a conflict and starts claiming again without waiting for the release window.
    ChainGuard g = leader();
    g.update(0);
    g.update(2000);
    g.on_foreign_write(2500);
    check("conflicted", g.conflict());
    g.retry(3000);
    check("retry claims again", g.state() == ChainState::CLAIMING && g.want_writes());
    check("retry beacons at once", g.update(3000).send_beacon);
  }
  {  // A follower never announces or writes, whatever happens on the bus.
    ChainGuard g(1000, 2000, 3000, 10000);
    g.start(0);
    g.set_role(ChainRole::FOLLOWER);
    g.set_want_writes(true);
    ChainGuard::Update u = g.update(100000);
    check("follower is idle", g.state() == ChainState::IDLE && !u.send_beacon && !u.controlling);
    g.on_foreign_write(100);
    check("follower does not conflict", !g.conflict() && g.state() == ChainState::IDLE);
    g.retry(200);
    check("follower retry does nothing", g.state() == ChainState::IDLE);
    check("follower never sends a beacon", g.beacons_sent() == 0);
  }
  {  // Turning writes off stops the beacon and the control immediately.
    ChainGuard g = leader();
    g.update(0);
    g.update(2000);
    check("controlling before writes off", g.controlling());
    g.set_want_writes(false);
    ChainGuard::Update u = g.update(3000);
    check("writes off goes idle", g.state() == ChainState::IDLE && !u.send_beacon && !u.controlling);
  }
  {  // Claim jitter delays the first write so two equal leaders can separate.
    ChainGuard g = leader();
    g.set_jitter_ms(500);
    g.update(0);
    check("still claiming at the base confirmation", !g.update(2000).controlling);
    check("controls after the jitter", g.update(2500).controlling);
  }
  {  // millis() wrap-around.
    ChainGuard g(1000, 2000, 3000, 10000);
    g.start(0xFFFFF000u);
    g.set_want_writes(true);
    check("wrap: beacons immediately", g.update(0xFFFFF000u + 100).send_beacon);
    check("wrap: controls after the confirmation window", g.update(0xFFFFF000u + 5000u).controlling);
    g.on_foreign_write(0xFFFFF000u + 6000u);
    check("wrap: conflict is detected", g.conflict());
  }
  {  // The guard must be started.
    ChainGuard g;
    g.set_want_writes(true);
    check("unstarted guard sends nothing", !g.update(100000).send_beacon && !g.controlling());
  }
  {  // Reason text for the bus-state entity.
    ChainGuard g = leader();
    g.update(0);
    g.on_foreign_write(100);
    check("reason names the other controller", g.reason(200).find("waiting") != std::string::npos);
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
