#pragma once
// Nearest-color search ("snap") over the CLR axis plus overlay channels.
//
// One algorithm, written twice: here for the firmware and in components/klink/klink_colors.py for
// the mock server and the golden vectors. All arithmetic is integer so both sides agree exactly.
// The tables (stops and overlay colors) are generated from the Python source into
// klink_color_tables.h (see `klink_colors.py --emit-cpp`); nothing generated is committed.
//
// Host test: see tests/test_color_snap.cpp.
#include <cstddef>
#include <cstdint>

namespace kessil {
namespace klink {

// Highest number of swatch+dist evaluations nearest_i() may make for any target.
static const uint32_t MAX_EVALS = 700;

struct ColorSpec {
  const uint8_t (*stops)[3];  // 17 entries, CLR 0..255 evenly spaced
  uint8_t n_overlays;
  const uint8_t *overlay_channel;    // channel index per overlay, in label order
  const uint8_t (*overlay_rgb)[3];   // overlay color per overlay
  uint8_t clr_max;                   // highest CLR the lamp accepts (240 on a Refugium)
};

struct SnapResult {
  uint8_t clr;
  uint8_t overlays[3];
  uint8_t rgb[3];
  uint32_t dist;
  uint32_t evals;
};

// Base color for `clr` interpolated between the 17 stops, then each overlay in order moves the
// color a fraction k/10000 (k = 60 * value, so 100% moves 60%) toward the overlay color.
inline void swatch_i(const ColorSpec &spec, uint8_t clr, const uint8_t *overlays, uint8_t out[3]) {
  int32_t num = static_cast<int32_t>(clr) * 16;
  int32_t i = num / 255;
  if (i >= 16)
    i = 15;
  int32_t frac = num - i * 255;
  for (int c = 0; c < 3; c++) {
    int32_t a = spec.stops[i][c];
    int32_t b = spec.stops[i + 1][c];
    out[c] = static_cast<uint8_t>((a * (255 - frac) + b * frac + 127) / 255);
  }
  for (uint8_t o = 0; o < spec.n_overlays; o++) {
    int32_t k = 60 * static_cast<int32_t>(overlays[o]);
    const uint8_t *rgb = spec.overlay_rgb[o];
    for (int c = 0; c < 3; c++) {
      int32_t value = out[c];
      out[c] = static_cast<uint8_t>((value * (10000 - k) + static_cast<int32_t>(rgb[c]) * k + 5000) / 10000);
    }
  }
}

// Redmean weighted squared distance; non-negative, fits in 32 bits.
inline uint32_t dist_i(const uint8_t a[3], const uint8_t b[3]) {
  int32_t rm = (static_cast<int32_t>(a[0]) + static_cast<int32_t>(b[0])) / 2;
  int32_t dr = static_cast<int32_t>(a[0]) - static_cast<int32_t>(b[0]);
  int32_t dg = static_cast<int32_t>(a[1]) - static_cast<int32_t>(b[1]);
  int32_t db = static_cast<int32_t>(a[2]) - static_cast<int32_t>(b[2]);
  uint32_t red = static_cast<uint32_t>(((512 + rm) * dr * dr) >> 8);
  uint32_t green = static_cast<uint32_t>(4 * dg * dg);
  uint32_t blue = static_cast<uint32_t>(((767 - rm) * db * db) >> 8);
  return red + green + blue;
}

// Find the CLR and overlay settings whose swatch_i() is closest to `target`.
inline SnapResult nearest_i(const ColorSpec &spec, const uint8_t target[3]) {
  static const int32_t CLR_STEPS[5] = {16, 8, 4, 2, 1};
  static const int32_t OV_STEPS[5] = {20, 10, 5, 2, 1};
  const int n_ov = spec.n_overlays;

  SnapResult best;
  best.clr = 0;
  best.overlays[0] = best.overlays[1] = best.overlays[2] = 0;
  best.rgb[0] = best.rgb[1] = best.rgb[2] = 0;
  best.dist = 0;
  best.evals = 0;

  uint8_t zeros[3] = {0, 0, 0};
  uint8_t rgb[3];

  // Stage 1: coarse CLR scan (0, 8, ... and clr_max), overlays 0.
  bool have = false;
  for (int clr = 0; clr <= spec.clr_max; clr += 8) {
    swatch_i(spec, static_cast<uint8_t>(clr), zeros, rgb);
    uint32_t distance = dist_i(rgb, target);
    best.evals++;
    if (!have || distance < best.dist) {
      have = true;
      best.clr = static_cast<uint8_t>(clr);
      best.dist = distance;
      for (int c = 0; c < 3; c++)
        best.rgb[c] = rgb[c];
    }
  }
  if (spec.clr_max % 8 != 0) {
    swatch_i(spec, spec.clr_max, zeros, rgb);
    uint32_t distance = dist_i(rgb, target);
    best.evals++;
    if (!have || distance < best.dist) {
      best.clr = spec.clr_max;
      best.dist = distance;
      for (int c = 0; c < 3; c++)
        best.rgb[c] = rgb[c];
    }
  }

  // Stage 2: coordinate descent over [clr, ov0, ov1, ...].
  int32_t values[4] = {best.clr, 0, 0, 0};
  for (int s = 0; s < 5; s++) {
    for (int pass = 0; pass < 8; pass++) {
      bool accepted = false;
      for (int vi = 0; vi < 1 + n_ov; vi++) {
        int32_t step = vi == 0 ? CLR_STEPS[s] : OV_STEPS[s];
        int32_t high = vi == 0 ? spec.clr_max : 100;
        int32_t current = values[vi];
        for (int sign = 1; sign >= -1; sign -= 2) {
          int32_t candidate = current + sign * step;
          if (candidate < 0)
            candidate = 0;
          else if (candidate > high)
            candidate = high;
          if (candidate == current)
            continue;
          uint8_t trial[3] = {0, 0, 0};
          for (int ov = 0; ov < n_ov; ov++)
            trial[ov] = static_cast<uint8_t>(values[1 + ov]);
          uint8_t candidate_clr;
          if (vi == 0) {
            candidate_clr = static_cast<uint8_t>(candidate);
          } else {
            trial[vi - 1] = static_cast<uint8_t>(candidate);
            candidate_clr = static_cast<uint8_t>(values[0]);
          }
          swatch_i(spec, candidate_clr, trial, rgb);
          uint32_t distance = dist_i(rgb, target);
          best.evals++;
          if (distance < best.dist) {
            values[vi] = candidate;
            best.dist = distance;
            best.clr = static_cast<uint8_t>(values[0]);
            for (int ov = 0; ov < n_ov; ov++)
              best.overlays[ov] = static_cast<uint8_t>(values[1 + ov]);
            for (int c = 0; c < 3; c++)
              best.rgb[c] = rgb[c];
            accepted = true;
            break;
          }
        }
      }
      if (!accepted)
        break;
    }
  }
  return best;
}

}  // namespace klink
}  // namespace kessil
