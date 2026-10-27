#pragma once
// Effects on ordinary ESPHome lights. The waveforms in klink_effects.h are written for the K-Link
// channels (UI1 intensity, UI2 a color axis running from warm to cool, UI3-UI5 extra colors). A native
// light shows them through what it can do: brightness for UI1, color temperature for UI2 when it has one,
// and on an RGB light a warm-to-cool white tint for UI2 and its own red, green and blue for UI3-UI5.
// Pure logic, no ESPHome dependencies (host tested).
#include <cmath>
#include <cstdint>

#include "klink_effects.h"
#include "klink_light_map.h"

namespace kessil {
namespace klink {

// How a light is driven by an effect, from its own capabilities.
enum class FxLight : uint8_t { BRIGHTNESS, CT, RGB };

// Color temperature wins over RGB here: the effect's UI2 is a temperature, and a light that has one shows it
// exactly, while an RGB-only light has to approximate it with a tint.
inline FxLight fx_light_for_caps(uint8_t caps) {
  if ((caps & CAP_CT) != 0)
    return FxLight::CT;
  if ((caps & CAP_RGB) != 0)
    return FxLight::RGB;
  return FxLight::BRIGHTNESS;
}

// Whether the effect shows anything on this kind of light. Cloud only moves UI2, so a brightness-only light
// would stay still; Rainbow needs red, green and blue; the Moon effects hold K-Link channels (UI3 or UI4)
// that are not a moon color on another kind of light.
inline bool fx_offered(EffectKind kind, FxLight light) {
  switch (kind) {
    case EFFECT_STORM:
    case EFFECT_AURORA:
      return true;
    case EFFECT_CLOUD:
      return light != FxLight::BRIGHTNESS;
    case EFFECT_RAINBOW:
      return light == FxLight::RGB;
    case EFFECT_MOON_RED:
    case EFFECT_MOON_BLUE:
      return false;
  }
  return false;
}

// The white an RGB light shows for UI2: warm (0) to cool (255), an approximation of 2700 K to 6500 K
// normalized so the strongest channel is 1.
inline void fx_tint(uint8_t ui2, float rgb[3]) {
  static const float WARM[3] = {255.0f, 169.0f, 87.0f};
  static const float COOL[3] = {255.0f, 249.0f, 251.0f};
  const float t = ui2 / 255.0f;
  for (int c = 0; c < 3; c++)
    rgb[c] = (WARM[c] + (COOL[c] - WARM[c]) * t) / 255.0f;
}

// One effect frame (UI1..UI5) as what the light is asked to show.
inline LightValues fx_light_values(EffectKind kind, FxLight light, const uint8_t frame[NUM_CHANNELS], CtRange range) {
  LightValues v = LightValues{};
  if (kind == EFFECT_RAINBOW && light == FxLight::RGB) {
    // The rainbow's UI1 stays 0 on the bus; the light shows its colors at full brightness.
    v.r = clamp01(frame[2] / 100.0f);
    v.g = clamp01(frame[3] / 100.0f);
    v.b = clamp01(frame[4] / 100.0f);
    v.brightness = 1.0f;
    v.on = v.r > 0.0f || v.g > 0.0f || v.b > 0.0f;
    return v;
  }
  v.brightness = frame[0] / 255.0f;
  v.on = frame[0] > 0;
  if (light == FxLight::CT) {
    const CtRange r = resolve_ct_range(range);
    v.mireds = r.max_mireds - (frame[1] / 255.0f) * (r.max_mireds - r.min_mireds);
  } else if (light == FxLight::RGB) {
    float rgb[3];
    fx_tint(frame[1], rgb);
    v.r = rgb[0];
    v.g = rgb[1];
    v.b = rgb[2];
  }
  return v;
}

}  // namespace klink
}  // namespace kessil
