// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_light_map.cpp -o /tmp/t_light_map && /tmp/t_light_map
#include <cmath>
#include <cstdio>
#include <cstring>
#include "klink_light_map.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

static LightValues values(bool on, float brightness, float mireds, float r, float g, float b, float w) {
  LightValues v;
  v.on = on;
  v.brightness = brightness;
  v.mireds = mireds;
  v.r = r;
  v.g = g;
  v.b = b;
  v.w = w;
  return v;
}

int main() {
  const CtRange kTunable = {153.0f, 370.0f};
  const MatchTolerance kTol = MatchTolerance::default_tolerance();

  // ---- capabilities ----
  check("caps empty", caps_common(nullptr, 0) == CAP_BRIGHTNESS);
  {
    const uint8_t one[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE)};
    check("caps n=0 ignores data", caps_common(one, 0) == CAP_BRIGHTNESS);
    check("caps nullptr", caps_common(nullptr, 3) == CAP_BRIGHTNESS);
  }
  {
    const uint8_t two_rgb[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB),
                               static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB)};
    check("caps two rgb", caps_common(two_rgb, 2) == (CAP_BRIGHTNESS | CAP_RGB));
  }
  {
    // RGB + CT lights with a brightness-only light in the group: only brightness survives.
    const uint8_t mixed[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB | CAP_CT), CAP_BRIGHTNESS};
    check("caps mixed with brightness only", caps_common(mixed, 2) == CAP_BRIGHTNESS);
  }
  {
    // A light without the brightness bit still ends up with it.
    check("caps always brightness", caps_common(nullptr, 0) == CAP_BRIGHTNESS);
    const uint8_t no_bright[] = {CAP_RGB};
    check("caps adds brightness", caps_common(no_bright, 1) == (CAP_BRIGHTNESS | CAP_RGB));
  }
  {
    // RGBW and RGB lights: the shared set is plain RGB.
    const uint8_t rgbw_rgb[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE),
                                static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB)};
    check("caps rgbw + rgb", caps_common(rgbw_rgb, 2) == (CAP_BRIGHTNESS | CAP_RGB));
  }
  {
    const uint8_t rgbw_rgbw[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE),
                                 static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE)};
    check("caps two rgbw", caps_common(rgbw_rgbw, 2) == (CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE));
  }
  {
    const uint8_t ct[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_CT)};
    check("caps ct", caps_common(ct, 1) == (CAP_BRIGHTNESS | CAP_CT));
  }
  {
    // Undefined bits are dropped from the result.
    const uint8_t junk[] = {static_cast<uint8_t>(CAP_BRIGHTNESS | 0x80)};
    check("caps drops unknown bits", caps_common(junk, 1) == CAP_BRIGHTNESS);
  }

  // ---- model selection ----
  check("model brightness", model_for_caps(CAP_BRIGHTNESS) == LightModel::BRIGHTNESS);
  check("model brightness from 0", model_for_caps(0) == LightModel::BRIGHTNESS);
  check("model ct", model_for_caps(CAP_BRIGHTNESS | CAP_CT) == LightModel::CT);
  check("model rgb", model_for_caps(CAP_BRIGHTNESS | CAP_RGB) == LightModel::RGB);
  check("model rgbw", model_for_caps(CAP_BRIGHTNESS | CAP_RGB | CAP_WHITE) == LightModel::RGBW);
  check("model white without rgb stays brightness",
        model_for_caps(CAP_BRIGHTNESS | CAP_WHITE) == LightModel::BRIGHTNESS);
  // RGB(W) beats CT when both are available.
  check("model rgb beats ct", model_for_caps(CAP_BRIGHTNESS | CAP_CT | CAP_RGB) == LightModel::RGB);
  check("model rgbw beats ct", model_for_caps(CAP_BRIGHTNESS | CAP_CT | CAP_RGB | CAP_WHITE) == LightModel::RGBW);

  // ---- names and labels ----
  check("name brightness", std::strcmp(light_model_name(LightModel::BRIGHTNESS), "light_brightness") == 0);
  check("name ct", std::strcmp(light_model_name(LightModel::CT), "light_ct") == 0);
  check("name rgb", std::strcmp(light_model_name(LightModel::RGB), "light_rgb") == 0);
  check("name rgbw", std::strcmp(light_model_name(LightModel::RGBW), "light_rgbw") == 0);

  {
    const SlotLabels *l = light_labels(LightModel::BRIGHTNESS);
    check("labels brightness",
          l != nullptr && std::strcmp(l->name[0], "int") == 0 && l->name[1] == nullptr &&
              l->name[2] == nullptr && l->name[3] == nullptr && l->name[4] == nullptr);
  }
  {
    const SlotLabels *l = light_labels(LightModel::CT);
    check("labels ct", l != nullptr && std::strcmp(l->name[0], "int") == 0 &&
                           std::strcmp(l->name[1], "ct") == 0 && l->name[2] == nullptr &&
                           l->name[3] == nullptr && l->name[4] == nullptr);
  }
  {
    const SlotLabels *l = light_labels(LightModel::RGB);
    check("labels rgb", l != nullptr && std::strcmp(l->name[0], "int") == 0 && l->name[1] == nullptr &&
                            std::strcmp(l->name[2], "r") == 0 && std::strcmp(l->name[3], "g") == 0 &&
                            std::strcmp(l->name[4], "b") == 0);
  }
  {
    const SlotLabels *l = light_labels(LightModel::RGBW);
    check("labels rgbw", l != nullptr && std::strcmp(l->name[0], "int") == 0 &&
                             std::strcmp(l->name[1], "w") == 0 && std::strcmp(l->name[2], "r") == 0 &&
                             std::strcmp(l->name[3], "g") == 0 && std::strcmp(l->name[4], "b") == 0);
  }
  // The labels parse with the slot code, so a program point can be shown per model.
  {
    SchedulePoint p;
    check("labels parse rgbw slot",
          parse_slot("13:00 int=100 w=50 r=10 g=20 b=30", p, light_labels(LightModel::RGBW)) &&
              p.enabled && p.minute == 780 && p.channels[0] == 100 && p.channels[1] == 50 &&
              p.channels[2] == 10 && p.channels[3] == 20 && p.channels[4] == 30);
  }

  // ---- channel -> values: brightness ----
  {
    const uint8_t off[NUM_CHANNELS] = {0, 0, 0, 0, 0};
    LightValues v = to_light_values(LightModel::BRIGHTNESS, off, kTunable);
    check("brightness off", !v.on && near(v.brightness, 0.0f) && near(v.mireds, 0.0f) && near(v.r, 0.0f) &&
                                near(v.g, 0.0f) && near(v.b, 0.0f) && near(v.w, 0.0f));
  }
  {
    const uint8_t full[NUM_CHANNELS] = {255, 255, 100, 100, 100};
    LightValues v = to_light_values(LightModel::BRIGHTNESS, full, kTunable);
    check("brightness full", v.on && near(v.brightness, 1.0f) && near(v.mireds, 0.0f) && near(v.r, 0.0f) &&
                                 near(v.g, 0.0f) && near(v.b, 0.0f) && near(v.w, 0.0f));
  }
  {
    const uint8_t half[NUM_CHANNELS] = {128, 0, 0, 0, 0};
    LightValues v = to_light_values(LightModel::BRIGHTNESS, half, kTunable);
    check("brightness 128", v.on && near(v.brightness, 128.0f / 255.0f));
  }
  {
    const uint8_t one[NUM_CHANNELS] = {1, 0, 0, 0, 0};
    check("brightness one is on", to_light_values(LightModel::BRIGHTNESS, one, kTunable).on);
  }

  // ---- channel -> values: ct (UI2 = 0 is the warm end) ----
  {
    const uint8_t warm[NUM_CHANNELS] = {128, 0, 0, 0, 0};
    LightValues v = to_light_values(LightModel::CT, warm, kTunable);
    check("ct warm end", v.on && near(v.brightness, 128.0f / 255.0f) && near(v.mireds, 370.0f) &&
                             near(v.r, 0.0f) && near(v.g, 0.0f) && near(v.b, 0.0f) && near(v.w, 0.0f));
  }
  {
    const uint8_t cold[NUM_CHANNELS] = {255, 255, 0, 0, 0};
    LightValues v = to_light_values(LightModel::CT, cold, kTunable);
    check("ct cold end", v.on && near(v.brightness, 1.0f) && near(v.mireds, 153.0f));
  }
  {
    const uint8_t mid[NUM_CHANNELS] = {255, 100, 0, 0, 0};
    LightValues v = to_light_values(LightModel::CT, mid, kTunable);
    check("ct ui2 100", near(v.mireds, 370.0f - (100.0f / 255.0f) * 217.0f, 1e-4f));
  }
  {
    const uint8_t off[NUM_CHANNELS] = {0, 255, 0, 0, 0};
    LightValues v = to_light_values(LightModel::CT, off, kTunable);
    check("ct off keeps mireds", !v.on && near(v.brightness, 0.0f) && near(v.mireds, 153.0f));
  }
  {
    // A reversed / non-positive range falls back to {153, 370}.
    const CtRange reversed = {370.0f, 153.0f};
    const CtRange zero = {0.0f, 300.0f};
    const CtRange negative = {-5.0f, 300.0f};
    const CtRange equal = {200.0f, 200.0f};
    const uint8_t warm[NUM_CHANNELS] = {100, 0, 0, 0, 0};
    const uint8_t cold[NUM_CHANNELS] = {100, 255, 0, 0, 0};
    check("ct reversed range falls back", near(to_light_values(LightModel::CT, warm, reversed).mireds, 370.0f) &&
                                              near(to_light_values(LightModel::CT, cold, reversed).mireds, 153.0f));
    check("ct zero min falls back", near(to_light_values(LightModel::CT, warm, zero).mireds, 370.0f) &&
                                        near(to_light_values(LightModel::CT, cold, zero).mireds, 153.0f));
    check("ct negative min falls back", near(to_light_values(LightModel::CT, warm, negative).mireds, 370.0f));
    check("ct equal ends fall back", near(to_light_values(LightModel::CT, warm, equal).mireds, 370.0f) &&
                                         near(to_light_values(LightModel::CT, cold, equal).mireds, 153.0f));
    check("ct custom range", near(to_light_values(LightModel::CT, warm, CtRange{200.0f, 500.0f}).mireds, 500.0f) &&
                                 near(to_light_values(LightModel::CT, cold, CtRange{200.0f, 500.0f}).mireds, 200.0f));
  }
  {
    CtRange r = resolve_ct_range(CtRange{153.0f, 370.0f});
    check("resolve valid range", near(r.min_mireds, 153.0f) && near(r.max_mireds, 370.0f));
    r = resolve_ct_range(CtRange{0.0f, 0.0f});
    check("resolve default range", near(r.min_mireds, 153.0f) && near(r.max_mireds, 370.0f));
  }

  // ---- channel -> values: rgb / rgbw ----
  {
    const uint8_t col[NUM_CHANNELS] = {255, 255, 100, 50, 0};
    LightValues v = to_light_values(LightModel::RGB, col, kTunable);
    check("rgb values", v.on && near(v.brightness, 1.0f) && near(v.r, 1.0f) && near(v.g, 0.5f) &&
                            near(v.b, 0.0f) && near(v.mireds, 0.0f) && near(v.w, 0.0f));
  }
  {
    const uint8_t col[NUM_CHANNELS] = {0, 255, 1, 99, 100};
    LightValues v = to_light_values(LightModel::RGB, col, kTunable);
    check("rgb off with color", !v.on && near(v.brightness, 0.0f) && near(v.r, 0.01f) &&
                                     near(v.g, 0.99f) && near(v.b, 1.0f) && near(v.w, 0.0f));
  }
  {
    const uint8_t col[NUM_CHANNELS] = {100, 128, 100, 100, 100};
    LightValues v = to_light_values(LightModel::RGBW, col, kTunable);
    check("rgbw values", v.on && near(v.brightness, 100.0f / 255.0f) && near(v.w, 128.0f / 255.0f) &&
                             near(v.r, 1.0f) && near(v.g, 1.0f) && near(v.b, 1.0f) && near(v.mireds, 0.0f));
  }
  {
    const uint8_t col[NUM_CHANNELS] = {100, 0, 0, 0, 0};
    LightValues v = to_light_values(LightModel::RGBW, col, kTunable);
    check("rgbw w 0", near(v.w, 0.0f) && near(v.r, 0.0f) && near(v.g, 0.0f) && near(v.b, 0.0f));
  }
  {
    const uint8_t col[NUM_CHANNELS] = {100, 255, 0, 0, 0};
    check("rgbw w full", near(to_light_values(LightModel::RGBW, col, kTunable).w, 1.0f));
  }
  {
    // Channel values above the model max are clamped, never renormalized.
    const uint8_t col[NUM_CHANNELS] = {200, 0, 200, 200, 200};
    LightValues v = to_light_values(LightModel::RGB, col, kTunable);
    check("rgb clamps", near(v.r, 1.0f) && near(v.g, 1.0f) && near(v.b, 1.0f) && near(v.brightness, 200.0f / 255.0f));
  }

  // ---- mireds <-> UI2 fraction ----
  check("fraction warm end", near(fraction_from_mireds(370.0f, kTunable), 0.0f));
  check("fraction cold end", near(fraction_from_mireds(153.0f, kTunable), 1.0f));
  check("fraction middle", near(fraction_from_mireds(261.5f, kTunable), 0.5f));
  check("fraction clamps cold", near(fraction_from_mireds(0.0f, kTunable), 1.0f));
  check("fraction clamps warm", near(fraction_from_mireds(1000.0f, kTunable), 0.0f));
  check("fraction default range", near(fraction_from_mireds(370.0f, CtRange{0.0f, 0.0f}), 0.0f));
  {
    // Round trip for several UI2 values: fraction -> mireds -> fraction.
    const int ui2[] = {0, 1, 100, 128, 254, 255};
    bool ok = true;
    for (size_t i = 0; i < sizeof(ui2) / sizeof(ui2[0]); i++) {
      const uint8_t ch[NUM_CHANNELS] = {255, static_cast<uint8_t>(ui2[i]), 0, 0, 0};
      LightValues v = to_light_values(LightModel::CT, ch, kTunable);
      float f = fraction_from_mireds(v.mireds, kTunable);
      ok = ok && near(f, ui2[i] / 255.0f);
    }
    check("fraction round trip", ok);
  }
  {
    const uint8_t ch[NUM_CHANNELS] = {255, 100, 0, 0, 0};
    LightValues v = to_light_values(LightModel::CT, ch, kTunable);
    float back = kTunable.max_mireds - fraction_from_mireds(v.mireds, kTunable) * (kTunable.max_mireds - kTunable.min_mireds);
    check("mireds round trip", near(back, v.mireds));
  }

  // ---- tolerance defaults ----
  check("tolerance defaults", near(kTol.brightness, 0.02f) && near(kTol.mireds_fraction, 0.03f) &&
                                  near(kTol.color, 0.02f));

  // ---- matches_expected ----
  {
    LightValues e = values(true, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    check("match brightness exact", matches_expected(LightModel::BRIGHTNESS, e, e, kTunable, kTol));
    LightValues within = e;
    within.brightness = 0.51f;
    check("match brightness within", matches_expected(LightModel::BRIGHTNESS, e, within, kTunable, kTol));
    LightValues outside = e;
    outside.brightness = 0.53f;
    check("match brightness outside", !matches_expected(LightModel::BRIGHTNESS, e, outside, kTunable, kTol));
    LightValues dim = e;
    dim.brightness = 0.485f;
    check("match brightness below within", matches_expected(LightModel::BRIGHTNESS, e, dim, kTunable, kTol));
    LightValues lower = e;
    lower.brightness = 0.47f;
    check("match brightness below outside", !matches_expected(LightModel::BRIGHTNESS, e, lower, kTunable, kTol));
  }
  {
    LightValues e = values(false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    LightValues remembered = values(false, 0.9f, 250.0f, 0.7f, 0.2f, 0.1f, 0.8f);
    check("match both off ignores fields",
          matches_expected(LightModel::RGBW, e, remembered, kTunable, kTol));
    LightValues on = values(true, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    check("match off vs on", !matches_expected(LightModel::BRIGHTNESS, e, on, kTunable, kTol));
    check("match on vs off", !matches_expected(LightModel::BRIGHTNESS, on, e, kTunable, kTol));
  }
  {
    // CT compares the UI2 fraction, so the same relative move matches at any range.
    LightValues e = values(true, 1.0f, 370.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    LightValues at_tol = e;
    at_tol.mireds = 370.0f - 0.02f * 217.0f;
    check("match ct within", matches_expected(LightModel::CT, e, at_tol, kTunable, kTol));
    LightValues beyond = e;
    beyond.mireds = 370.0f - 0.05f * 217.0f;
    check("match ct outside", !matches_expected(LightModel::CT, e, beyond, kTunable, kTol));
    LightValues warmer = e;
    warmer.mireds = 370.0f;  // unchanged, color fields ignored for CT
    warmer.r = 1.0f;
    warmer.w = 1.0f;
    check("match ct ignores color", matches_expected(LightModel::CT, e, warmer, kTunable, kTol));
  }
  {
    LightValues e = values(true, 0.8f, 200.0f, 0.5f, 0.2f, 0.1f, 0.0f);
    LightValues same = e;
    same.mireds = 300.0f;  // unused for RGB
    check("match rgb exact", matches_expected(LightModel::RGB, e, same, kTunable, kTol));
    LightValues r_off = same;
    r_off.r = 0.53f;
    check("match rgb r outside", !matches_expected(LightModel::RGB, e, r_off, kTunable, kTol));
    LightValues g_off = same;
    g_off.g = 0.24f;
    check("match rgb g outside", !matches_expected(LightModel::RGB, e, g_off, kTunable, kTol));
    LightValues b_off = same;
    b_off.b = 0.0f;
    check("match rgb b outside", !matches_expected(LightModel::RGB, e, b_off, kTunable, kTol));
    LightValues b_near = same;
    b_near.b = 0.11f;
    check("match rgb b within", matches_expected(LightModel::RGB, e, b_near, kTunable, kTol));
    LightValues w_only = same;
    w_only.w = 0.8f;  // unused for RGB
    check("match rgb ignores w", matches_expected(LightModel::RGB, e, w_only, kTunable, kTol));
  }
  {
    LightValues e = values(true, 0.8f, 0.0f, 0.5f, 0.2f, 0.1f, 0.6f);
    check("match rgbw exact", matches_expected(LightModel::RGBW, e, e, kTunable, kTol));
    LightValues w_near = e;
    w_near.w = 0.61f;
    check("match rgbw w within", matches_expected(LightModel::RGBW, e, w_near, kTunable, kTol));
    LightValues w_off = e;
    w_off.w = 0.65f;
    check("match rgbw w outside", !matches_expected(LightModel::RGBW, e, w_off, kTunable, kTol));
    LightValues rgb_off = e;
    rgb_off.g = 0.3f;
    check("match rgbw rgb outside", !matches_expected(LightModel::RGBW, e, rgb_off, kTunable, kTol));
  }
  {
    // A stricter/looser tolerance is honored.
    LightValues e = values(true, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    LightValues obs = e;
    obs.brightness = 0.53f;
    MatchTolerance loose = {0.05f, 0.03f, 0.02f};
    check("match loose tolerance", matches_expected(LightModel::BRIGHTNESS, e, obs, kTunable, loose));
    MatchTolerance none = {0.0f, 0.0f, 0.0f};
    check("match zero tolerance exact", matches_expected(LightModel::BRIGHTNESS, e, e, kTunable, none));
    check("match zero tolerance differs", !matches_expected(LightModel::BRIGHTNESS, e, obs, kTunable, none));
  }

  // ---- channels_differ ----
  {
    const uint8_t a[NUM_CHANNELS] = {10, 20, 30, 40, 50};
    const uint8_t b[NUM_CHANNELS] = {10, 20, 30, 40, 50};
    const uint8_t c[NUM_CHANNELS] = {10, 20, 30, 40, 51};
    const uint8_t d[NUM_CHANNELS] = {11, 20, 30, 40, 50};
    check("channels equal", !channels_differ(a, b));
    check("channels differ last", channels_differ(a, c));
    check("channels differ first", channels_differ(a, d));
    check("channels differ symmetric", channels_differ(c, a) && !channels_differ(b, a));
  }

  // ---- needs_light_update ----
  {
    const uint8_t a[NUM_CHANNELS] = {10, 20, 30, 40, 50};
    const uint8_t b[NUM_CHANNELS] = {11, 20, 30, 40, 50};
    check("update never applied", needs_light_update(false, a, a, 0, 0));
    check("update never applied ignores refresh", needs_light_update(false, a, a, 10, 60000));
    check("update unchanged no refresh", !needs_light_update(true, a, a, 999999, 0));
    check("update changed", needs_light_update(true, a, b, 0, 0));
    check("update changed with refresh", needs_light_update(true, a, b, 0, 60000));
    check("update refresh not due", !needs_light_update(true, a, a, 59999, 60000));
    check("update refresh due", needs_light_update(true, a, a, 60000, 60000));
    check("update refresh overdue", needs_light_update(true, a, a, 120000, 60000));
    check("update refresh disabled unchanged", !needs_light_update(true, a, a, 1000000, 0));
    check("update refresh one ms", needs_light_update(true, a, a, 1, 1));
  }

  // ---- program ids ----
  check("program id 100", !is_program_id(100));
  check("program id 101", is_program_id(101));
  check("program id 164", is_program_id(164));
  check("program id 165", !is_program_id(165));
  check("program id 0", !is_program_id(0));
  check("program id 255", !is_program_id(255));
  check("program id 1000", !is_program_id(1000));

  check("bus gid 0", !is_bus_gid(0));
  check("bus gid 1", is_bus_gid(1));
  check("bus gid 40", is_bus_gid(40));
  check("bus gid 41", !is_bus_gid(41));
  check("bus gid 101", !is_bus_gid(101));

  check("index 0 valid", program_index_valid(0));
  check("index 63 valid", program_index_valid(63));
  check("index 64 invalid", !program_index_valid(64));
  check("id for index 0", program_id_for_index(0) == 101);
  check("id for index 62", program_id_for_index(62) == 163);
  check("id for index 63", program_id_for_index(63) == 164);
  check("id for index 64 clamps", program_id_for_index(64) == 164);
  check("id for index 1000 clamps", program_id_for_index(1000) == 164);
  {
    bool ok = true;
    for (size_t i = 0; i < 64; i++) {
      uint8_t id = program_id_for_index(i);
      ok = ok && is_program_id(id) && !is_bus_gid(id) && id == static_cast<uint8_t>(101 + i);
    }
    check("all 64 ids valid and in range", ok);
    check("id count matches range", PROGRAM_ID_MAX - PROGRAM_ID_MIN + 1 == NUM_PROGRAM_IDS);
  }
  check("id MIN/MAX", PROGRAM_ID_MIN == 101 && PROGRAM_ID_MAX == 164);
  check("bus MIN/MAX", BUS_GID_MIN == 1 && BUS_GID_MAX == 40);

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
