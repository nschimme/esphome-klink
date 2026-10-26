// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_bus.cpp && ./a.out
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include "klink_bus.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

// A fake UART with a manual clock and a pluggable "lamp side" that reacts to frames written.
class FakeIo : public Io {
 public:
  uint32_t now = 1000;
  std::deque<uint8_t> rx;
  std::vector<Bytes> sent;
  std::vector<uint32_t> sent_at;
  std::function<void(const Bytes &)> on_write;
  uint32_t now_ms() override { return now; }
  bool read_byte(uint8_t &out) override {
    if (rx.empty()) return false;
    out = rx.front(); rx.pop_front();
    return true;
  }
  void write(const Bytes &f) override {
    sent.push_back(f); sent_at.push_back(now);
    if (on_write) on_write(f);
  }
  void feed(const Bytes &b) { rx.insert(rx.end(), b.begin(), b.end()); }
};

// Advance time in 1 ms steps, calling loop(), up to `ms`.
static void run(FakeIo &io, Bus &bus, uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) { io.now++; bus.loop(); }
}

static const Bytes IDENT_REPLY = {0xA9, 0x06, 0, 0, 0x1C, 0x02, 0x01, 0xDB, 0x5C};  // example reply: GID 2, type 2
static const Bytes MUTE_ACK = {0xA9, 0x05, 0, 0, 0x19, 0xFF, 0xE3, 0x5C};          // example ack

int main() {
  {  // our own fire-and-forget frame read back (direct drive) is not "another controller"
    FakeIo io; Bus bus(io); int heard = 0;
    bus.set_frame_callback([&](const Bytes &) { heard++; });
    Bytes gw = group_channels(2, (const uint8_t[5]){138, 81, 0, 0, 0});
    bus.enqueue(gw, 0, 0, nullptr);
    run(io, bus, 1);
    io.feed(gw);  // the echo
    run(io, bus, 5);
    check("own echo of a fire-and-forget frame is swallowed", heard == 0);
    io.feed(gw);  // a second, identical frame is a different talker
    run(io, bus, 5);
    check("only one echo is swallowed", heard == 1);
  }
  {  // fire and forget
    FakeIo io; Bus bus(io); int calls = 0; bool ok = false;
    bus.enqueue(unmute(1), 0, 0, [&](bool o, const Bytes &r) { calls++; ok = o && r.empty(); });
    bus.loop();
    check("fire-and-forget sends and completes", calls == 1 && ok && io.sent.size() == 1 && !bus.busy());
  }
  {  // request/response in two chunks
    FakeIo io; Bus bus(io); int calls = 0; bool ok = false; Bytes got;
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 150, [&](bool o, const Bytes &r) { calls++; ok = o; got = r; });
    bus.loop();
    io.feed(Bytes(IDENT_REPLY.begin(), IDENT_REPLY.begin() + 4)); run(io, bus, 5);
    check("partial reply does not complete", calls == 0 && bus.busy());
    io.feed(Bytes(IDENT_REPLY.begin() + 4, IDENT_REPLY.end())); run(io, bus, 2);
    check("reply completes once", calls == 1 && ok && got == IDENT_REPLY && !bus.busy());
  }
  {  // timeout
    FakeIo io; Bus bus(io); int calls = 0; bool ok = true; size_t n = 99;
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 150, [&](bool o, const Bytes &r) { calls++; ok = o; n = r.size(); });
    bus.loop(); run(io, bus, 149);
    check("not timed out early", calls == 0);
    run(io, bus, 2);
    check("times out with empty reply", calls == 1 && !ok && n == 0 && bus.timeouts() == 1);
  }
  {  // echo of our own frame, garbage before the marker, then the reply
    FakeIo io; Bus bus(io); bool ok = false; Bytes got;
    Bytes req = identity(7);
    bus.enqueue(req, CMD_GET_GID_TYPE, 150, [&](bool o, const Bytes &r) { ok = o; got = r; });
    bus.loop();
    Bytes in = {0x00, 0x13};
    in.insert(in.end(), req.begin(), req.end());
    in.insert(in.end(), IDENT_REPLY.begin(), IDENT_REPLY.end());
    io.feed(in); run(io, bus, 3);
    check("echo and noise are skipped", ok && got == IDENT_REPLY);
  }
  {  // a reply to a different command is not accepted as ours
    FakeIo io; Bus bus(io); bool ok = true;
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 50, [&](bool o, const Bytes &) { ok = o; });
    bus.loop(); io.feed(MUTE_ACK); run(io, bus, 60);
    check("wrong-command reply ignored, then times out", !ok);
  }
  {  // gap, FIFO order, one in flight, stale bytes dropped
    FakeIo io; Bus bus(io); bus.set_min_gap_ms(20);
    std::vector<int> order;
    bus.enqueue(unmute(1), 0, 0, [&](bool, const Bytes &) { order.push_back(1); });
    bus.enqueue(unmute(2), 0, 0, [&](bool, const Bytes &) { order.push_back(2); });
    bus.enqueue(identity(3), CMD_GET_GID_TYPE, 150, [&](bool, const Bytes &) { order.push_back(3); });
    io.feed(Bytes{0xA9, 0x01, 0x02});  // stale junk
    bus.loop(); run(io, bus, 300);  // request 3 gets no reply and completes at its 150 ms timeout
    check("FIFO order", order == std::vector<int>({1, 2, 3}));
    check("min gap between frames", io.sent.size() == 3 && io.sent_at[1] - io.sent_at[0] >= 20 && io.sent_at[2] - io.sent_at[1] >= 20);
    check("stale bytes were dropped", io.rx.empty());
  }
  {  // per-loop byte budget
    FakeIo io; Bus bus(io); bus.set_byte_budget(16);
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 500, nullptr);
    bus.loop();
    io.feed(Bytes(200, 0x00));  // noise
    bus.loop();
    check("one loop reads at most the budget", io.rx.size() == 184);
  }
  {  // identity job: unmute, 1C, mute; mute even when 1C fails
    FakeIo io; Bus bus(io); bool ok = false; Identity id = {0, 0}; bool called = false;
    io.on_write = [&](const Bytes &f) {
      if (f[4] == CMD_GET_GID_TYPE) io.feed(IDENT_REPLY);
      if (f[4] == CMD_MUTE) io.feed(MUTE_ACK);
    };
    read_identity_async(bus, 18001234, [&](bool o, Identity i) { called = true; ok = o; id = i; });
    run(io, bus, 400);
    check("identity job result", called && ok && id.gid == 2 && id.type == 2);
    check("identity job frame order", io.sent.size() == 3 && io.sent[0] == unmute(18001234) && io.sent[1] == identity(18001234) && io.sent[2] == mute(18001234));

    FakeIo io2; Bus bus2(io2); bool called2 = false, ok2 = true;
    read_identity_async(bus2, 18001234, [&](bool o, Identity) { called2 = true; ok2 = o; });
    run(io2, bus2, 1000);
    check("lamp silent: failure reported", called2 && !ok2);
    check("lamp silent: still re-muted", io2.sent.size() == 3 && io2.sent.back() == mute(18001234));
  }
  {  // a complete frame that fails its checksum is a collision, not silence
    FakeIo io; Bus bus(io); bool called = false, ok = true; Bytes got;
    Bytes bad = build(CMD_BRANCH, be32(18001234u)); bad[bad.size() - 2] ^= 0x55;  // wrong checksum, right length
    io.on_write = [&](const Bytes &) { io.feed(bad); };
    bus.enqueue(branch(100, 200), CMD_BRANCH, 150, [&](bool o, const Bytes &r) { called = true; ok = o; got = r; });
    run(io, bus, 400);
    check("invalid frame on timeout is reported as non-empty", called && !ok && !got.empty());
  }
  {  // scan against simulated lamps with mute state; several lamps answer one of three ways
    const uint32_t A = 18001234u, B = 18001235u, C = 25000000u;
    for (int mode = 0; mode < 3; mode++) {
      FakeIo io; Bus bus(io);
      std::map<uint32_t, bool> unmuted = {{A, false}, {B, false}, {C, false}};  // all start muted
      io.on_write = [&](const Bytes &f) {
        uint32_t kid = (uint32_t(f[5]) << 24) | (uint32_t(f[6]) << 16) | (uint32_t(f[7]) << 8) | f[8];
        if (f[4] == CMD_UNMUTE || f[4] == CMD_MUTE) {
          bool on = f[4] == CMD_UNMUTE;
          if (kid == ALL_KIDS) { for (auto &l : unmuted) l.second = on; }
          else if (unmuted.count(kid)) { unmuted[kid] = on; if (!on) io.feed(MUTE_ACK); }
          return;
        }
        if (f[4] != CMD_BRANCH) return;
        uint32_t hi = (uint32_t(f[9]) << 24) | (uint32_t(f[10]) << 16) | (uint32_t(f[11]) << 8) | f[12];
        std::vector<uint32_t> in;
        for (auto &l : unmuted) if (l.second && l.first >= kid && l.first < hi) in.push_back(l.first);
        if (in.size() == 1) io.feed(build(CMD_BRANCH, be32(in[0])));
        else if (in.size() > 1) {
          if (mode == 0) io.feed(Bytes{0xA9, 0xFF, 0x3C});                       // short fragment
          else if (mode == 1) { Bytes g = build(CMD_BRANCH, be32(in[0])); g[g.size() - 2] ^= 0x55; io.feed(g); }  // invalid frame
          else io.feed(build(CMD_BRANCH, be32(in[0])));                             // one wins cleanly (capture effect)
        }
      };
      bus.enqueue(unmute(ALL_KIDS), 0, 0, nullptr);
      Scanner scan(bus); bool done = false, complete = false; std::vector<uint32_t> kids;
      scan.start(18000000u, 36000001u, [&](bool c, const std::vector<uint32_t> &k) { done = true; complete = c; kids = k; });
      uint32_t guard = 0;
      while (!done && guard++ < 4000000) { io.now++; bus.loop(); }
      std::sort(kids.begin(), kids.end());
      char name[96];
      std::snprintf(name, sizeof name, "mode %d: scan completes", mode);
      check(name, done && complete);
      std::snprintf(name, sizeof name, "mode %d: scan finds exactly the lamps", mode);
      check(name, kids == std::vector<uint32_t>({A, B, C}));
      std::snprintf(name, sizeof name, "mode %d: found lamps are left muted", mode);
      check(name, !unmuted[A] && !unmuted[B] && !unmuted[C]);
      std::snprintf(name, sizeof name, "mode %d: scan is not running afterwards", mode);
      check(name, !scan.running());
    }
  }
  {  // scan: no lamps at all
    FakeIo io; Bus bus(io); Scanner scan(bus); bool done = false; size_t n = 99; bool complete = false;
    scan.start(18000000u, 36000001u, [&](bool c, const std::vector<uint32_t> &k) { done = true; complete = c; n = k.size(); });
    uint32_t guard = 0;
    while (!done && guard++ < 100000) { io.now++; bus.loop(); }
    check("empty bus: completes with no lamps after one retry", done && complete && n == 0 && scan.queries() == 2 && scan.silent_ranges() == 1);
  }
  {  // scan: unresolvable collision on a single KID aborts instead of looping
    FakeIo io; Bus bus(io);
    io.on_write = [&](const Bytes &) { io.feed(Bytes{0xA9, 0xFF, 0x3C}); };
    Scanner scan(bus); bool done = false, complete = true;
    scan.start(100, 102, [&](bool c, const std::vector<uint32_t> &) { done = true; complete = c; });
    uint32_t guard = 0;
    while (!done && guard++ < 100000) { io.now++; bus.loop(); }
    check("persistent collision aborts as incomplete", done && !complete);
  }
  {  // idle monitor: another controller's alternating 34 / 1F traffic is reported as frames
    FakeIo io; Bus bus(io);
    std::vector<Bytes> seen;
    bus.set_frame_callback([&](const Bytes &f) { seen.push_back(f); });
    const Bytes probe = {0xA9, 0x04, 0x00, 0x00, 0x34, 0xC8, 0x5C};
    const Bytes g = {0xA9, 0x0B, 0x00, 0x00, 0x1F, 0x66, 0x33, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3B, 0x5C};
    Bytes stream; for (int i = 0; i < 3; i++) { stream.insert(stream.end(), g.begin(), g.end()); stream.insert(stream.end(), probe.begin(), probe.end()); }
    io.feed(Bytes{0x00, 0x55});  // noise first
    io.feed(stream);
    run(io, bus, 20);
    check("monitor reports six frames", seen.size() == 6 && seen[0] == g && seen[1] == probe && bus.monitor().frames() == 6);
  }
  {  // a request in flight: our reply completes it, another controller's frame goes to the monitor
    FakeIo io; Bus bus(io);
    std::vector<Bytes> seen; bool ok = false; Bytes got;
    bus.set_frame_callback([&](const Bytes &f) { seen.push_back(f); });
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 150, [&](bool o, const Bytes &r) { ok = o; got = r; });
    bus.loop();
    const Bytes probe = {0xA9, 0x04, 0x00, 0x00, 0x34, 0xC8, 0x5C};
    io.feed(probe); io.feed(IDENT_REPLY);
    run(io, bus, 5);
    check("reply completes while foreign frame is reported", ok && got == IDENT_REPLY && seen.size() == 1 && seen[0] == probe);
  }
  {  // without a callback nothing is consumed while idle (stale bytes are dropped at the next send)
    FakeIo io; Bus bus(io);
    io.feed(Bytes{1, 2, 3}); run(io, bus, 5);
    check("no callback: idle leaves bytes alone", io.rx.size() == 3);
  }
  {  // clock wrap-around
    FakeIo io; io.now = 0xFFFFFFF0u; Bus bus(io); bool calls = false, ok = true;
    bus.enqueue(identity(7), CMD_GET_GID_TYPE, 150, [&](bool o, const Bytes &) { calls = true; ok = o; });
    bus.loop(); run(io, bus, 160);
    check("timeout works across millis() wrap", calls && !ok);
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
