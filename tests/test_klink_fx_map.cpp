// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_fx_map.cpp && ./a.out
#include <cmath>
#include <cstdio>

#include "klink_fx_map.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}
static bool near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }

int main() {
  check("brightness only", fx_light_for_caps(CAP_BRIGHTNESS) == FxLight::BRIGHTNESS);
  check("ct light", fx_light_for_caps(CAP_BRIGHTNESS | CAP_CT) == FxLight::CT);
  check("rgb light", fx_light_for_caps(CAP_BRIGHTNESS | CAP_RGB) == FxLight::RGB);
  check("rgb with white is rgb", fx_light_for_caps(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE) == FxLight::RGB);
  check("rgb and ct prefers ct", fx_light_for_caps(CAP_BRIGHTNESS | CAP_RGB | CAP_CT) == FxLight::CT);

  check("storm and aurora on every light", fx_offered(EFFECT_STORM, FxLight::BRIGHTNESS) &&
                                               fx_offered(EFFECT_AURORA, FxLight::BRIGHTNESS) &&
                                               fx_offered(EFFECT_STORM, FxLight::RGB));
  check("cloud needs a color", !fx_offered(EFFECT_CLOUD, FxLight::BRIGHTNESS) && fx_offered(EFFECT_CLOUD, FxLight::CT) &&
                                   fx_offered(EFFECT_CLOUD, FxLight::RGB));
  check("rainbow needs rgb", !fx_offered(EFFECT_RAINBOW, FxLight::CT) && fx_offered(EFFECT_RAINBOW, FxLight::RGB));
  check("no moon effects", !fx_offered(EFFECT_MOON_RED, FxLight::RGB) && !fx_offered(EFFECT_MOON_BLUE, FxLight::RGB));

  CtRange range = {153.0f, 370.0f};
  uint8_t idle[5] = {50, 0, 0, 0, 0}, flash[5] = {100, 100, 0, 0, 0}, off[5] = {0, 0, 0, 0, 0};
  LightValues v = fx_light_values(EFFECT_STORM, FxLight::BRIGHTNESS, idle, range);
  check("brightness idle", v.on && near(v.brightness, 50 / 255.0f) && v.mireds == 0.0f && v.r == 0.0f);
  v = fx_light_values(EFFECT_STORM, FxLight::CT, idle, range);
  check("ct warm end at UI2 0", near(v.mireds, 370.0f, 0.01f));
  v = fx_light_values(EFFECT_STORM, FxLight::CT, flash, range);
  check("ct flash is cooler", v.mireds < 370.0f && near(v.mireds, 370.0f - (100 / 255.0f) * 217.0f, 0.01f));
  uint8_t cool[5] = {255, 255, 0, 0, 0};
  v = fx_light_values(EFFECT_AURORA, FxLight::CT, cool, range);
  check("ct cool end at UI2 255", near(v.mireds, 153.0f, 0.01f) && near(v.brightness, 1.0f));
  v = fx_light_values(EFFECT_STORM, FxLight::CT, off, range);
  check("zero intensity is off", !v.on);

  v = fx_light_values(EFFECT_STORM, FxLight::RGB, idle, range);
  check("rgb warm tint", v.on && near(v.r, 1.0f) && v.g < v.r && v.b < v.g && near(v.brightness, 50 / 255.0f));
  v = fx_light_values(EFFECT_STORM, FxLight::RGB, cool, range);
  check("rgb cool tint is nearly white", near(v.r, 1.0f) && v.g > 0.95f && v.b > 0.95f);
  uint8_t mid[5] = {200, 128, 0, 0, 0};
  v = fx_light_values(EFFECT_CLOUD, FxLight::RGB, mid, range);
  uint8_t warm_end[5] = {200, 0, 0, 0, 0};
  LightValues w = fx_light_values(EFFECT_CLOUD, FxLight::RGB, warm_end, range);
  check("rgb tint cools as UI2 rises", v.b > w.b && v.g > w.g);

  uint8_t rb[5] = {0, 0, 100, 50, 0};
  v = fx_light_values(EFFECT_RAINBOW, FxLight::RGB, rb, range);
  check("rainbow shows its colors at full brightness", v.on && near(v.brightness, 1.0f) && near(v.r, 1.0f) &&
                                                          near(v.g, 0.5f) && near(v.b, 0.0f));
  uint8_t rb_dark[5] = {0, 0, 0, 0, 0};
  check("rainbow dark frame is off", !fx_light_values(EFFECT_RAINBOW, FxLight::RGB, rb_dark, range).on);

  // Every waveform stays in range on every kind of light.
  for (int kind = EFFECT_STORM; kind <= EFFECT_AURORA; kind++) {
    for (int variant = 0; variant < 3; variant++) {
      uint32_t seed = 12345;
      EffectEngine engine([&seed]() { seed = seed * 1103515245u + 12345u; return seed >> 8; });
      engine.start(static_cast<EffectKind>(kind), variant);
      for (uint32_t t = 0; t < 120000; t += 40) {
        uint8_t frame[5];
        engine.step(t, frame);
        for (int l = 0; l < 3; l++) {
          LightValues x = fx_light_values(static_cast<EffectKind>(kind), static_cast<FxLight>(l), frame, range);
          if (x.brightness < 0.0f || x.brightness > 1.0f || x.r < 0.0f || x.r > 1.0f || x.g < 0.0f || x.g > 1.0f ||
              x.b < 0.0f || x.b > 1.0f) {
            check("values stay in range", false);
            t = 120000;
            kind = EFFECT_AURORA;
            variant = 3;
            break;
          }
        }
      }
    }
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
