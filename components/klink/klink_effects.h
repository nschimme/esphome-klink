#pragma once
// The Dongle's lighting effects as pure waveforms. The Dongle computes them itself and streams the
// result as ordinary group writes (command 1F) about ten times a second; no K-Link effect command
// exists on the wire. Recovered from `calcEffect` in Dongle firmware 1.5.13 and checked against bus
// captures of the real Dongle (see docs/klink-protocol.md, "Effects").
//
// An effect ID is `kind + 100 * variant`: kind 0-5 picks the effect and variant 0-2 slows it down by
// (variant + 1), which is what the app's Fast / Moderate / Slow slider sends. The Dongle's FREQ field
// has no effect on the output. No ESPHome dependencies (host tested).
#include <cstdint>
#include <functional>

namespace kessil {
namespace klink {

enum EffectKind : uint8_t {
  EFFECT_STORM = 0,      // lightning: idle, then bursts of flashes
  EFFECT_CLOUD = 1,      // UI2 triangle, UI1 fixed
  EFFECT_RAINBOW = 2,    // sweep over UI3-UI5 (lamps with those channels)
  EFFECT_AURORA = 3,     // UI1 = UI2 slow swell
  EFFECT_MOON_RED = 4,   // constant UI3
  EFFECT_MOON_BLUE = 5,  // constant UI4
};

static const uint32_t EFFECT_TICK_MS = 100;  // the Dongle writes about every 100 ms while an effect runs

class EffectEngine {
 public:
  typedef std::function<uint32_t()> Rng;

  explicit EffectEngine(Rng rng = nullptr) : rng_(rng) {}

  // `variant` 0-2 is the speed (0 fastest). Resets the storm's counter.
  void start(EffectKind kind, uint8_t variant) {
    kind_ = kind;
    a_ = static_cast<uint32_t>(variant > 2 ? 2 : variant) + 1;
    count_ = 0;
  }

  // One step. `elapsed_ms` is the time since the effect started; `out` receives UI1..UI5. Storm also
  // advances an internal counter, so call this once per tick.
  void step(uint32_t elapsed_ms, uint8_t out[5]) {
    int32_t v[5] = {0, 0, 0, 0, 0};
    switch (kind_) {
      case EFFECT_STORM:
        storm_(v);
        break;
      case EFFECT_CLOUD:
        cloud_(elapsed_ms, v);
        break;
      case EFFECT_RAINBOW:
        rainbow_(elapsed_ms, v);
        break;
      case EFFECT_AURORA:
        aurora_(elapsed_ms, v);
        break;
      case EFFECT_MOON_RED:
        v[2] = 200;
        break;
      case EFFECT_MOON_BLUE:
        v[3] = 200;
        break;
    }
    for (int i = 0; i < 5; i++)
      out[i] = static_cast<uint8_t>(v[i] < 0 ? 0 : (v[i] > 255 ? 255 : v[i]));
  }

  uint32_t speed_divisor() const { return a_; }

 private:
  uint32_t next_() { return rng_ ? rng_() : fallback_(); }
  // xorshift32 for when no generator was injected
  uint32_t fallback_() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

  // One unit is 50 ms of effect time, scaled by the speed variant.
  int32_t units_(uint32_t t) const { return static_cast<int32_t>((t / a_) / 50u); }

  // Idle {50,0}. After `base` ticks, `k` bursts of two flash ticks {100,100}, one every m+2 ticks, then
  // the counter resets and new random values are drawn.
  void storm_(int32_t v[5]) {
    v[0] = 50;
    v[1] = 0;
    if (count_ == 0) {
      base_ = next_() % 100 + 100;
      k_ = next_() % 4 + 1;
      m_ = next_() % 6 + 15;
    } else if (count_ >= base_) {
      uint32_t d = count_ - base_;
      if (d % (m_ + 2) < 2) {
        v[0] = 100;
        v[1] = 100;
      }
    }
    uint32_t end = base_ + k_ * (m_ + 2);
    count_ = count_ < end ? count_ + 1 : 0;
  }

  void cloud_(uint32_t elapsed, int32_t v[5]) const {
    uint32_t t = elapsed % (10000u * a_);
    v[0] = 50;
    v[1] = t <= 5000u * a_ ? units_(t) : 200 - units_(t);
  }

  void aurora_(uint32_t elapsed, int32_t v[5]) const {
    uint32_t t = elapsed % (30000u * a_);
    int32_t x;
    if (t <= 12750u * a_)
      x = units_(t);
    else if (t <= 15000u * a_)
      x = 255;
    else if (t <= 27650u * a_)
      x = 555 - units_(t);
    else
      x = 2;
    if (x < 3)
      x = 2;
    v[0] = v[1] = x;
  }

  // Six five-second legs passing 100 around UI3, UI4 and UI5.
  void rainbow_(uint32_t elapsed, int32_t v[5]) const {
    uint32_t t = elapsed % (30000u * a_);
    int32_t u = units_(t);
    const uint32_t leg = 5000u * a_;
    if (t < leg) {
      v[3] = 100;
      v[4] = u;
    } else if (t == leg) {
      v[3] = v[4] = 100;
    } else if (t < 2 * leg) {
      v[3] = 200 - u;
      v[4] = 100;
    } else if (t == 2 * leg) {
      v[4] = 100;
    } else if (t < 3 * leg) {
      v[2] = u - 200;
      v[4] = 300 - u;
    } else if (t == 3 * leg) {
      v[2] = 100;
    } else if (t < 4 * leg) {
      v[2] = 100;
      v[4] = u - 300;
    } else if (t == 4 * leg) {
      v[2] = v[4] = 100;
    } else if (t < 5 * leg) {
      v[2] = 100;
      v[3] = u - 400;
      v[4] = 500 - u;
    } else if (t == 5 * leg) {
      v[2] = v[3] = 100;
    } else {
      v[2] = 600 - u;
      v[3] = 100;
    }
  }

  Rng rng_;
  EffectKind kind_{EFFECT_STORM};
  uint32_t a_{1};
  uint32_t count_{0};
  uint32_t base_{100}, k_{1}, m_{15};
  uint32_t state_{2463534242u};
};

}  // namespace klink
}  // namespace kessil
