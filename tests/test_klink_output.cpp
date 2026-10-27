// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_output.cpp -o /tmp/t && /tmp/t
#include <cmath>
#include <cstdio>

#include "klink_output.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}
static bool close_to(float a, float b, float eps) { return std::fabs(a - b) < eps; }

// t = all zeros with channel c set to v
static void one(float t[NUM_CHANNELS], size_t c, float v) {
  for (size_t i = 0; i < NUM_CHANNELS; i++)
    t[i] = 0.0f;
  t[c] = v;
}

// Fresh stage: first set_target lands on `from` instantly, the second starts the fade to `to`.
static void begin_fade(OutputStage &s, size_t c, float from, float to) {
  float t[NUM_CHANNELS];
  one(t, c, from);
  s.set_target(t, true);
  one(t, c, to);
  s.set_target(t, true);
}

int main() {
  // --- CIE 1931 lightness ---
  check("cie endpoints", close_to(perceptual_to_linear(0.0f), 0.0f, 1e-6f) &&
                             close_to(perceptual_to_linear(1.0f), 1.0f, 1e-6f) &&
                             close_to(linear_to_perceptual(0.0f), 0.0f, 1e-6f) &&
                             close_to(linear_to_perceptual(1.0f), 1.0f, 1e-6f));
  bool monotonic = true;
  float prev = -1.0f;
  for (int i = 0; i <= 1000; i++) {
    float y = perceptual_to_linear(static_cast<float>(i) / 1000.0f);
    if (y < prev)
      monotonic = false;
    prev = y;
  }
  check("cie monotonic", monotonic);
  float max_err = 0.0f;
  for (int i = 0; i <= 1000; i++) {
    float l = static_cast<float>(i) / 1000.0f;
    float e = std::fabs(linear_to_perceptual(perceptual_to_linear(l)) - l);
    if (e > max_err)
      max_err = e;
  }
  check("cie round trip", max_err < 1e-4f);
  check("cie L=50", close_to(perceptual_to_linear(0.5f), 0.1842f, 1e-3f));
  check("cie L=8", close_to(perceptual_to_linear(0.08f), 0.08f / 9.033f, 1e-4f));

  uint8_t out[NUM_CHANNELS];
  float t[NUM_CHANNELS];

  // --- no fade configured ---
  {
    OutputStage s;
    OutputConfig cfg;  // fade_ms 0
    s.configure(cfg);
    one(t, 0, 200.0f);
    t[1] = 100.0f;
    t[2] = 50.0f;
    t[3] = 25.0f;
    t[4] = 12.0f;
    s.set_target(t, true);
    bool busy = s.step(100, out);
    check("no fade is immediate", !busy && s.settled());
    check("no fade bytes", out[0] == 200 && out[1] == 100 && out[2] == 50 && out[3] == 25 && out[4] == 12);
  }

  // --- linear fade 0 -> 200 over 2000 ms ---
  {
    OutputStage s;
    OutputConfig cfg;
    cfg.fade_ms = 2000;
    s.configure(cfg);
    begin_fade(s, 0, 0.0f, 200.0f);
    bool mono = true, busy_until_end = true;
    int prev_byte = -1, at_1000 = -1;
    for (int i = 1; i <= 20; i++) {
      bool busy = s.step(100, out);
      if (out[0] < prev_byte)
        mono = false;
      prev_byte = out[0];
      if (i == 10)
        at_1000 = out[0];
      if (i < 20 && !busy)
        busy_until_end = false;
      if (i == 20)
        check("linear ends settled", !busy && s.settled());
    }
    check("linear monotonic", mono);
    check("linear halfway", at_1000 == 100);
    check("linear stays busy", busy_until_end);
    check("linear ends on target", out[0] == 200);
  }

  // --- perceptual fade 0 -> 255 over 2000 ms ---
  {
    OutputStage s;
    OutputConfig cfg;
    cfg.fade_ms = 2000;
    cfg.fade_style = FADE_PERCEPTUAL;
    s.configure(cfg);
    begin_fade(s, 0, 0.0f, 255.0f);
    int at_1000 = -1;
    for (int i = 1; i <= 10; i++) {
      s.step(100, out);
      if (i == 10)
        at_1000 = out[0];
    }
    int expect = static_cast<int>(255.0f * perceptual_to_linear(0.5f) + 0.5f);
    check("perceptual halfway", at_1000 == expect);
    for (int i = 11; i <= 20; i++)
      s.step(100, out);
    check("perceptual ends on target", out[0] == 255 && s.settled());
  }

  // --- eased fade 0 -> 255: slower start than linear, same end ---
  {
    OutputStage lin, eased;
    OutputConfig cfg;
    cfg.fade_ms = 2000;
    cfg.fade_style = FADE_LINEAR;
    lin.configure(cfg);
    begin_fade(lin, 0, 0.0f, 255.0f);
    int lin25 = -1;
    for (int i = 1; i <= 5; i++) {
      lin.step(100, out);
      if (i == 5)
        lin25 = out[0];
    }

    cfg.fade_style = FADE_EASED;
    eased.configure(cfg);
    begin_fade(eased, 0, 0.0f, 255.0f);
    int eased25 = -1;
    for (int i = 1; i <= 20; i++) {
      eased.step(100, out);
      if (i == 5)
        eased25 = out[0];
    }
    check("eased slower start", eased25 < lin25);
    check("eased ends on target", out[0] == 255 && eased.settled());
  }

  // --- no overshoot in either direction, every style ---
  {
    const FadeStyle styles[3] = {FADE_LINEAR, FADE_PERCEPTUAL, FADE_EASED};
    bool ok = true;
    for (int k = 0; k < 3; k++) {
      const float pairs[2][2] = {{0.0f, 200.0f}, {200.0f, 10.0f}};
      for (int d = 0; d < 2; d++) {
        OutputStage s;
        OutputConfig cfg;
        cfg.fade_ms = 2000;
        cfg.fade_style = styles[k];
        s.configure(cfg);
        begin_fade(s, 0, pairs[d][0], pairs[d][1]);
        int lo = static_cast<int>(pairs[d][0] < pairs[d][1] ? pairs[d][0] : pairs[d][1]);
        int hi = static_cast<int>(pairs[d][0] < pairs[d][1] ? pairs[d][1] : pairs[d][0]);
        for (int i = 1; i <= 20; i++) {
          s.step(100, out);
          if (out[0] < lo || out[0] > hi)
            ok = false;
        }
        if (out[0] != hi && out[0] != lo)
          ok = false;
      }
    }
    check("fade never overshoots", ok);
  }

  // --- channel 1 is never perceptual ---
  {
    OutputStage s;
    OutputConfig cfg;
    cfg.fade_ms = 2000;
    cfg.fade_style = FADE_PERCEPTUAL;
    s.configure(cfg);
    begin_fade(s, 1, 0.0f, 200.0f);
    for (int i = 1; i <= 10; i++)
      s.step(100, out);
    check("color axis is linear", out[1] == 100);
  }

  // --- non-jump target changes ---
  {
    OutputStage s;
    OutputConfig cfg;
    cfg.fade_ms = 1000;
    s.configure(cfg);
    one(t, 0, 0.0f);
    s.set_target(t, true);
    one(t, 0, 120.0f);
    s.set_target(t, false);  // idle: immediate
    bool busy = s.step(100, out);
    check("idle non-jump is immediate", out[0] == 120 && !busy && s.settled());
  }
  {
    OutputStage s;
    OutputConfig cfg;
    cfg.fade_ms = 1000;
    s.configure(cfg);
    begin_fade(s, 0, 0.0f, 200.0f);
    for (int i = 1; i <= 5; i++)
      s.step(100, out);  // halfway, fade keeps its start and progress
    one(t, 0, 100.0f);
    s.set_target(t, false);  // only the end target changes
    bool busy = true;
    for (int i = 6; i <= 10; i++)
      busy = s.step(100, out);
    check("retarget during fade", !busy && s.settled() && out[0] == 100);
  }

  // --- channel clamps ---
  {
    OutputStage s;
    OutputConfig cfg;
    s.configure(cfg);
    one(t, 0, 300.0f);
    s.set_target(t, true);
    s.step(100, out);
    check("clamp UI1", out[0] == 255);

    one(t, 2, 150.0f);
    s.set_target(t, true);
    s.step(100, out);
    check("clamp UI3", out[2] == 100);
  }

  // reset() takes a value without fading, and a later jump fades from it.
  {
    OutputStage st;
    OutputConfig cfg;
    cfg.fade_ms = 1000;
    st.configure(cfg);
    float a[5] = {0, 0, 0, 0, 0}, b[5] = {100, 0, 0, 0, 0}, c[5] = {200, 0, 0, 0, 0};
    uint8_t o[5];
    st.set_target(a, true);
    st.reset(b);
    st.step(0, o);
    check("reset is immediate", o[0] == 100 && st.settled());
    st.set_target(c, true);
    st.step(500, o);
    check("jump after reset fades from it", o[0] == 150);
  }

  // fade_to: a soft start of its own length, whatever the configured fade time.
  {
    OutputStage st;
    OutputConfig cfg;
    cfg.fade_style = FADE_LINEAR;
    cfg.fade_ms = 0;
    st.configure(cfg);
    float zero[5] = {0, 0, 0, 0, 0}, goal[5] = {200, 0, 0, 0, 0};
    uint8_t o[5];
    st.reset(zero);
    st.fade_to(goal, 4000);
    st.step(1000, o);
    check("fade_to quarter way", o[0] == 50 && !st.settled());
    st.step(3000, o);
    check("fade_to lands exactly", o[0] == 200 && st.settled());
    st.fade_to(zero, 0);
    st.step(0, o);
    check("fade_to with 0 ms is immediate", o[0] == 0 && st.settled());
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
