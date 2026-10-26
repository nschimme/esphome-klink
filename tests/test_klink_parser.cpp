// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_parser.cpp && ./a.out
// Uses example frames (GID 2 group writes and the controller probe).
#include <cstdio>
#include "klink_frame.h"

using namespace kessil::klink;
typedef std::vector<uint8_t> Bytes;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

// Feed a whole byte string; return the frames found (draining poll() after each byte).
static std::vector<Bytes> run(FrameParser &p, const Bytes &in) {
  std::vector<Bytes> out;
  Bytes f;
  for (size_t i = 0; i < in.size(); i++) {
    bool got = p.feed(in[i], f);
    while (got) { out.push_back(f); got = p.poll(f); }
  }
  return out;
}

int main() {
  const Bytes probe = {0xA9, 0x04, 0x00, 0x00, 0x34, 0xC8, 0x5C};                                // example
  const Bytes g66 = {0xA9, 0x0B, 0x00, 0x00, 0x1F, 0x66, 0x33, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3B, 0x5C};  // example
  const Bytes g67 = {0xA9, 0x0B, 0x00, 0x00, 0x1F, 0x67, 0x33, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3A, 0x5C};  // example
  const Bytes ident = {0xA9, 0x06, 0x00, 0x00, 0x1C, 0x02, 0x01, 0xDB, 0x5C};                    // example reply

  check("controller_probe builds the example frame", controller_probe() == probe);
  uint8_t ch[5] = {0x66, 0x33, 0, 0, 0};
  check("group_channels reproduces the example frame", group_channels(2, ch) == g66);

  {  // an alternating stream, byte at a time
    FrameParser p;
    Bytes stream;
    for (int i = 0; i < 3; i++) {
      stream.insert(stream.end(), g66.begin(), g66.end());
      stream.insert(stream.end(), probe.begin(), probe.end());
    }
    std::vector<Bytes> f = run(p, stream);
    check("six frames from the stream", f.size() == 6 && f[0] == g66 && f[1] == probe && p.bad_frames() == 0 && p.dropped_bytes() == 0);
  }
  {  // noise before, between, and after
    FrameParser p;
    Bytes in = {0x00, 0xFF, 0x13};
    in.insert(in.end(), probe.begin(), probe.end());
    in.push_back(0x55);
    in.insert(in.end(), g67.begin(), g67.end());
    in.push_back(0xA8);
    std::vector<Bytes> f = run(p, in);
    check("noise is skipped", f.size() == 2 && f[0] == probe && f[1] == g67 && p.dropped_bytes() == 5);
  }
  {  // a false start marker swallowing the frame behind it
    FrameParser p;
    Bytes in = {0xA9, 0x04};            // looks like the start of a 7-byte frame
    in.insert(in.end(), ident.begin(), ident.end());  // the frame begins inside the false one
    std::vector<Bytes> f = run(p, in);
    check("frame found inside a false start", f.size() == 1 && f[0] == ident);
  }
  {  // bad checksum is rejected and counted, the next frame is still found
    FrameParser p;
    Bytes bad = g66; bad[5] ^= 0x01;
    Bytes in = bad;
    in.insert(in.end(), probe.begin(), probe.end());
    std::vector<Bytes> f = run(p, in);
    check("bad checksum rejected", f.size() == 1 && f[0] == probe && p.bad_frames() >= 1);
  }
  {  // missing stop byte
    FrameParser p;
    Bytes bad = probe; bad[6] = 0x00;
    Bytes in = bad;
    in.insert(in.end(), g67.begin(), g67.end());
    std::vector<Bytes> f = run(p, in);
    check("bad terminator rejected", f.size() == 1 && f[0] == g67);
  }
  {  // implausible length bytes do not stall the parser
    FrameParser p;
    Bytes in = {0xA9, 0xFF, 0x00, 0x00, 0x00};
    in.insert(in.end(), probe.begin(), probe.end());
    std::vector<Bytes> f = run(p, in);
    check("huge length ignored", f.size() == 1 && f[0] == probe);
    Bytes small = {0xA9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    FrameParser q;
    check("zero length ignored", run(q, small).empty());
  }
  {  // a frame split across many feeds
    FrameParser p;
    Bytes f1;
    bool got = false;
    for (size_t i = 0; i < g66.size() - 1; i++) got = p.feed(g66[i], f1) || got;
    check("incomplete frame returns nothing", !got);
    check("completes on the last byte", p.feed(g66.back(), f1) && f1 == g66);
  }
  {  // back-to-back frames with no gap, including the mute reply seen on hardware
    FrameParser p;
    const Bytes mute_ack = {0xA9, 0x05, 0x00, 0x00, 0x19, 0xFF, 0xE3, 0x5C};
    Bytes in = mute_ack;
    in.insert(in.end(), ident.begin(), ident.end());
    in.insert(in.end(), probe.begin(), probe.end());
    std::vector<Bytes> f = run(p, in);
    check("three adjacent frames", f.size() == 3 && f[0] == mute_ack && f[1] == ident && f[2] == probe);
  }

  check("describe probe", describe_frame(probe) == "controller probe (34)");
  check("describe group write", describe_frame(g66) == "group write GID 2: 102 51 0 0 0");
  check("describe identity reply", describe_frame(ident) == "identity reply: GID 2, type 2");
  check("describe mute all", describe_frame(mute(ALL_KIDS)) == "mute all lamps");
  check("describe KID query", describe_frame(identity(18001234)) == "identity query KID 18001234");
  check("describe unknown", describe_frame(build(0x77, Bytes())) == "command 77");

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
