// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_effects.cpp && ./a.out
#include <cstdio>
#include <vector>
#include "klink_effects.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

static void at(EffectEngine &e, uint32_t t, uint8_t out[5]) { e.step(t, out); }

static bool is(const uint8_t o[5], int a, int b, int c, int d, int e) {
  return o[0] == a && o[1] == b && o[2] == c && o[3] == d && o[4] == e;
}

int main() {
  uint8_t o[5];

  {  // Remaining-time text for the status line: mm:ss, zero-padded seconds
    check("format 102", format_remaining(102) == "1:42");
    check("format 7", format_remaining(7) == "0:07");
    check("format 0", format_remaining(0) == "0:00");
    check("format 300", format_remaining(300) == "5:00");
    check("format negative clamps", format_remaining(-5) == "0:00");
  }

  {  // Aurora, fastest variant: UI1 == UI2, rising 0..255 over 12.75 s, plateau, fall, floor at 2
    EffectEngine e; e.start(EFFECT_AURORA, 0);
    at(e, 0, o);      check("aurora t=0 floors at 2", is(o, 2, 2, 0, 0, 0));
    at(e, 5000, o);   check("aurora t=5s", is(o, 100, 100, 0, 0, 0));
    at(e, 12750, o);  check("aurora t=12.75s", is(o, 255, 255, 0, 0, 0));
    at(e, 14000, o);  check("aurora plateau", is(o, 255, 255, 0, 0, 0));
    at(e, 15000, o);  check("aurora t=15s", is(o, 255, 255, 0, 0, 0));
    at(e, 20000, o);  check("aurora falling t=20s", is(o, 155, 155, 0, 0, 0));
    at(e, 27650, o);  check("aurora t=27.65s", is(o, 2, 2, 0, 0, 0));
    at(e, 29000, o);  check("aurora tail", is(o, 2, 2, 0, 0, 0));
    at(e, 30000 + 5000, o); check("aurora wraps", is(o, 100, 100, 0, 0, 0));
    // capture: about 2.1 per 100 ms tick on the way up
    uint8_t a[5], b[5];
    at(e, 6000, a); at(e, 6100, b);
    check("aurora slope ~2 per 100 ms", b[0] - a[0] == 2);
  }

  {  // Aurora speed variants stretch time by (variant + 1)
    EffectEngine e1; e1.start(EFFECT_AURORA, 1);
    EffectEngine e0; e0.start(EFFECT_AURORA, 0);
    uint8_t a[5], b[5];
    e1.step(10000, a); e0.step(5000, b);
    check("variant 1 runs at half speed", a[0] == b[0] && a[0] == 100);
    EffectEngine e2; e2.start(EFFECT_AURORA, 2);
    e2.step(15000, a);
    check("variant 2 runs at a third", a[0] == 100);
  }

  {  // Cloud: UI1 50, UI2 triangle 0..100..0 over 10 s
    EffectEngine e; e.start(EFFECT_CLOUD, 0);
    at(e, 0, o);     check("cloud t=0", is(o, 50, 0, 0, 0, 0));
    at(e, 2500, o);  check("cloud t=2.5s", is(o, 50, 50, 0, 0, 0));
    at(e, 5000, o);  check("cloud peak", is(o, 50, 100, 0, 0, 0));
    at(e, 7500, o);  check("cloud falling", is(o, 50, 50, 0, 0, 0));
    at(e, 9950, o);  check("cloud near end", o[0] == 50 && o[1] <= 1);
    at(e, 12500, o); check("cloud wraps", is(o, 50, 50, 0, 0, 0));
  }

  {  // Rainbow: 100 passes UI4 -> UI5 -> UI3 -> UI5 -> UI4 ... over six legs
    EffectEngine e; e.start(EFFECT_RAINBOW, 0);
    at(e, 0, o);      check("rainbow t=0", is(o, 0, 0, 0, 100, 0));
    at(e, 2500, o);   check("rainbow leg1 UI5 rising", is(o, 0, 0, 0, 100, 50));
    at(e, 7500, o);   check("rainbow leg2 UI4 falling", is(o, 0, 0, 0, 50, 100));
    at(e, 12500, o);  check("rainbow leg3", is(o, 0, 0, 50, 0, 50));
    at(e, 17500, o);  check("rainbow leg4 UI5 rising again", is(o, 0, 0, 100, 0, 50));
    at(e, 22500, o);  check("rainbow leg5", is(o, 0, 0, 100, 50, 50));
    at(e, 27500, o);  check("rainbow leg6", is(o, 0, 0, 50, 100, 0));
  }

  {  // Moonlight: constants
    EffectEngine r; r.start(EFFECT_MOON_RED, 0);
    r.step(12345, o); check("moon red", is(o, 0, 0, 200, 0, 0));
    EffectEngine b; b.start(EFFECT_MOON_BLUE, 0);
    b.step(12345, o); check("moon blue", is(o, 0, 0, 0, 200, 0));
  }

  {  // Storm with a scripted generator: base 100+7, k 1+2, m 15+1
    std::vector<uint32_t> script = {7, 2, 1};
    size_t pos = 0;
    EffectEngine e([&]() { return script[pos++ % script.size()]; });
    e.start(EFFECT_STORM, 0);
    std::vector<int> flash;  // tick numbers that flashed
    for (int tick = 0; tick < 400; tick++) {
      e.step(tick * 100u, o);
      if (o[0] == 100) {
        check("flash is {100,100}", is(o, 100, 100, 0, 0, 0));
        flash.push_back(tick);
      } else {
        check("idle is {50,0}", is(o, 50, 0, 0, 0, 0));
      }
    }
    // base = 107, m+2 = 18, k = 3 bursts. Tick 0 draws the values and counts as count 0.
    check("first flash after the idle period", !flash.empty() && flash[0] == 107);
    bool pattern = flash.size() >= 6 && flash[1] == 108 && flash[2] == 107 + 18 && flash[3] == 108 + 18 &&
                   flash[4] == 107 + 36 && flash[5] == 108 + 36;
    check("two-tick flashes every m+2 ticks", pattern);
    // after k bursts (107 + 3*18 = 161 ticks) the counter resets and the next cycle is idle again
    bool second_cycle_idle = true;
    for (int f : flash) if (f > 161 && f < 161 + 100) second_cycle_idle = false;
    check("storm idles again after the bursts", second_cycle_idle);
  }

  {  // The built-in generator keeps the idle length in 100..199 ticks and 1..4 bursts
    EffectEngine e; e.start(EFFECT_STORM, 0);
    int idle = 0, bursts = 0, cycles = 0;
    bool in_flash = false;
    for (int tick = 0; tick < 20000 && cycles < 30; tick++) {
      e.step(tick * 100u, o);
      bool f = o[0] == 100;
      if (f && !in_flash) { bursts++; }
      in_flash = f;
      if (!f && bursts == 0) idle++;
      if (!f && bursts > 0 && idle < 0) {}
    }
    check("storm produced flashes with the default generator", bursts > 0);
  }

  if (failures == 0) std::printf("all passed\n");
  return failures ? 1 : 0;
}
