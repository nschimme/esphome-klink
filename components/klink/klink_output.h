#pragma once
// Output stage: turns requested channel targets into the bytes sent to the light, optionally
// fading between them (linear, CIE perceptual or eased).
// Works on raw channel units (0..CHANNEL_MAX[c]) as floats. No ESPHome dependencies (host tested).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "klink_schedule.h"

namespace kessil {
namespace klink {

// CIE 1931 lightness (0..1) to relative luminance (0..1). L = l*100; below 8 the curve is linear.
inline float perceptual_to_linear(float l) {
  l = std::min(1.0f, std::max(0.0f, l));
  float L = l * 100.0f;
  if (L <= 8.0f)
    return L / 903.3f;
  float t = (L + 16.0f) / 116.0f;
  return t * t * t;
}

// Inverse of perceptual_to_linear: relative luminance to CIE 1931 lightness, both 0..1.
inline float linear_to_perceptual(float y) {
  y = std::min(1.0f, std::max(0.0f, y));
  float L;
  if (y <= 216.0f / 24389.0f)  // 216/24389 is the linear cut-off, 0.008856
    L = 903.3f * y;
  else
    L = 116.0f * std::pow(y, 1.0f / 3.0f) - 16.0f;
  return L / 100.0f;
}

// Fades are written 25 times a second so that a slow fade moves the lamp by one step at a time (at 10 frames a
// second a 3 s fade of a 26% level jumps 2-3 steps per frame).
static const uint32_t FADE_TICK_MS = 40;

enum FadeStyle { FADE_LINEAR = 0, FADE_PERCEPTUAL = 1, FADE_EASED = 2 };

struct OutputConfig {
  FadeStyle fade_style = FADE_LINEAR;
  uint32_t fade_ms = 0;  // 0 means jumps are immediate
};

class OutputStage {
 public:
  OutputStage() : style_(FADE_LINEAR), fade_ms_(0), run_ms_(0), ready_(false), fading_(false), elapsed_(0) {
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      current_[c] = target_[c] = from_[c] = 0.0f;
  }

  void configure(const OutputConfig &config) {
    style_ = config.fade_style;
    fade_ms_ = config.fade_ms;
  }

  // `target` is in raw channel units; it is not clamped here, the byte output is.
  void set_target(const float target[NUM_CHANNELS], bool jump) {
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      target_[c] = target[c];
    if (!ready_) {  // first call ever: start at the target, never fade in
      for (size_t c = 0; c < NUM_CHANNELS; c++)
        current_[c] = target_[c];
      ready_ = true;
      fading_ = false;
      return;
    }
    if (jump) {
      if (fade_ms_ > 0) {  // (re)start the fade from where the output is right now
        run_ms_ = fade_ms_;
        for (size_t c = 0; c < NUM_CHANNELS; c++)
          from_[c] = current_[c];
        elapsed_ = 0;
        fading_ = true;
      } else {  // no fade configured: jump straight there
        for (size_t c = 0; c < NUM_CHANNELS; c++)
          current_[c] = target_[c];
        fading_ = false;
      }
    } else if (!fading_) {  // smooth target while idle: already there
      for (size_t c = 0; c < NUM_CHANNELS; c++)
        current_[c] = target_[c];
    }
    // While fading and !jump only the end target changes; from_ and elapsed_ stay.
  }

  // Fade from the present output to `target` over `ms`, whatever the configured fade time (a soft start).
  void fade_to(const float target[NUM_CHANNELS], uint32_t ms) {
    for (size_t c = 0; c < NUM_CHANNELS; c++) {
      target_[c] = target[c];
      from_[c] = current_[c];
    }
    ready_ = true;
    if (ms == 0) {
      for (size_t c = 0; c < NUM_CHANNELS; c++)
        current_[c] = target_[c];
      fading_ = false;
      return;
    }
    run_ms_ = ms;
    elapsed_ = 0;
    fading_ = true;
  }

  // Take `value` as the present output without fading (frames written around the stage, such as an
  // effect's, so a later jump fades from where the lamp really is).
  void reset(const float value[NUM_CHANNELS]) {
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      current_[c] = target_[c] = value[c];
    ready_ = true;
    fading_ = false;
  }

  // Advance `dt_ms`, write the bytes for this instant. Returns true while still moving.
  bool step(uint32_t dt_ms, uint8_t out[NUM_CHANNELS]) {
    if (fading_) {
      elapsed_ += dt_ms;
      if (elapsed_ >= run_ms_) {  // land exactly on the target, no rounding residue
        elapsed_ = run_ms_;
        fading_ = false;
        for (size_t c = 0; c < NUM_CHANNELS; c++)
          current_[c] = target_[c];
      } else {
        interpolate(static_cast<float>(elapsed_) / static_cast<float>(run_ms_));
      }
    }
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      out[c] = encode(c, current_[c]);
    return !settled();
  }

  bool settled() const { return !fading_; }

  void current(float out[NUM_CHANNELS]) const {
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      out[c] = current_[c];
  }

 private:
  // p is fade progress 0..1. Channel 1 (UI2, color) is always a plain linear lerp: moving it
  // through perceptual space would shift the hue. No style overshoots [from, to].
  void interpolate(float p) {
    for (size_t c = 0; c < NUM_CHANNELS; c++) {
      float a = from_[c], b = target_[c], v;
      if (c != 1 && (style_ == FADE_PERCEPTUAL || style_ == FADE_EASED)) {
        float q = style_ == FADE_EASED ? p * p * (3.0f - 2.0f * p) : p;
        float maxv = static_cast<float>(CHANNEL_MAX[c]);
        float pa = linear_to_perceptual(a / maxv), pb = linear_to_perceptual(b / maxv);
        v = perceptual_to_linear(pa + (pb - pa) * q) * maxv;
      } else {
        v = a + (b - a) * p;
      }
      current_[c] = std::min(std::max(v, std::min(a, b)), std::max(a, b));
    }
  }

  uint8_t encode(size_t c, float v) const {
    const float q = std::floor(v + 0.5f);
    if (q < 0.0f)
      return 0;
    if (q > CHANNEL_MAX[c])
      return CHANNEL_MAX[c];
    return static_cast<uint8_t>(q);
  }

  FadeStyle style_;
  uint32_t fade_ms_;
  uint32_t run_ms_;  // the length of the fade that is running (a soft start can differ from fade_ms_)
  bool ready_;
  bool fading_;
  uint32_t elapsed_;
  float current_[NUM_CHANNELS];
  float target_[NUM_CHANNELS];
  float from_[NUM_CHANNELS];
};

}  // namespace klink
}  // namespace kessil
