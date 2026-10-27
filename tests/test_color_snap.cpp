// Host test for the integer nearest-color search in components/klink/klink_colors.h.
//
// The color tables and the golden vectors are generated from the Python reference in
// components/klink/klink_colors.py (nothing generated is committed). Build and run:
//
//   python3 components/klink/klink_colors.py --emit-cpp > .tmp/klink_color_tables.h
//   python3 components/klink/klink_colors.py --emit-golden > .tmp/snap_golden.txt
//   g++ -std=c++11 -Wall -Wextra -I components/klink -I .tmp tests/test_color_snap.cpp -o .tmp/test_color_snap
//   .tmp/test_color_snap .tmp/snap_golden.txt
//
// The golden path defaults to .tmp/snap_golden.txt when argv[1] is absent.
#include <cstdio>
#include <cstring>

#include "klink_color_tables.h"

using namespace kessil::klink;

static int failures = 0;

static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}

static const ColorSpec *spec_for(const char *model) {
  if (std::strcmp(model, "tuna_sun") == 0)
    return &COLOR_SUN;
  if (std::strcmp(model, "tuna_blue") == 0)
    return &COLOR_BLUE;
  if (std::strcmp(model, "refugium") == 0)
    return &COLOR_REFUGIUM;
  return nullptr;
}

static bool same_result(const SnapResult &a, const SnapResult &b) {
  if (a.clr != b.clr || a.dist != b.dist || a.evals != b.evals)
    return false;
  for (int i = 0; i < 3; i++)
    if (a.overlays[i] != b.overlays[i] || a.rgb[i] != b.rgb[i])
      return false;
  return true;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : ".tmp/snap_golden.txt";
  FILE *file = std::fopen(path, "r");
  if (file == nullptr) {
    std::printf("cannot open golden file %s\n", path);
    return 2;
  }

  char model[32];
  int target_in[3], clr_in, ov_in[3], rgb_in[3];
  unsigned dist_in, evals_in;
  int lines = 0;
  uint32_t max_evals = 0;

  while (std::fscanf(file, "%31s %d %d %d %d %d %d %d %d %d %d %u %u", model, &target_in[0],
                     &target_in[1], &target_in[2], &clr_in, &ov_in[0], &ov_in[1], &ov_in[2],
                     &rgb_in[0], &rgb_in[1], &rgb_in[2], &dist_in, &evals_in) == 13) {
    const ColorSpec *spec = spec_for(model);
    check("golden model known", spec != nullptr);
    if (spec == nullptr)
      continue;

    uint8_t target[3] = {static_cast<uint8_t>(target_in[0]), static_cast<uint8_t>(target_in[1]),
                         static_cast<uint8_t>(target_in[2])};
    SnapResult got = nearest_i(*spec, target);

    char name[128];
    std::snprintf(name, sizeof(name), "%s(%d,%d,%d) clr", model, target_in[0], target_in[1],
                  target_in[2]);
    check(name, got.clr == clr_in);
    std::snprintf(name, sizeof(name), "%s(%d,%d,%d) rgb", model, target_in[0], target_in[1],
                  target_in[2]);
    check(name, got.rgb[0] == rgb_in[0] && got.rgb[1] == rgb_in[1] && got.rgb[2] == rgb_in[2]);
    std::snprintf(name, sizeof(name), "%s(%d,%d,%d) dist", model, target_in[0], target_in[1],
                  target_in[2]);
    check(name, got.dist == dist_in);
    std::snprintf(name, sizeof(name), "%s(%d,%d,%d) evals", model, target_in[0], target_in[1],
                  target_in[2]);
    check(name, got.evals == evals_in);
    check("evals <= MAX_EVALS", got.evals <= MAX_EVALS);

    for (int o = 0; o < 3; o++) {
      std::snprintf(name, sizeof(name), "%s(%d,%d,%d) overlay %d", model, target_in[0],
                    target_in[1], target_in[2], o);
      if (o < spec->n_overlays)
        check(name, got.overlays[o] == ov_in[o]);
      else
        check(name, ov_in[o] == -1);
    }

    // The reported rgb must be the swatch of the reported settings.
    uint8_t recomputed[3];
    swatch_i(*spec, got.clr, got.overlays, recomputed);
    check("rgb matches swatch_i", recomputed[0] == got.rgb[0] && recomputed[1] == got.rgb[1] &&
                                       recomputed[2] == got.rgb[2]);

    check("clr within the model's limit", got.clr <= spec->clr_max);
    SnapResult again = nearest_i(*spec, target);
    check("deterministic", same_result(got, again));

    if (got.evals > max_evals)
      max_evals = got.evals;
    lines++;
  }
  std::fclose(file);

  check("golden file has vectors", lines > 0);

  // Every one of the model's own gradient stops must be reproduced exactly with overlays 0.
  const ColorSpec *specs[3] = {&COLOR_SUN, &COLOR_BLUE, &COLOR_REFUGIUM};
  for (int m = 0; m < 3; m++) {
    uint8_t zeros[3] = {0, 0, 0};
    for (int i = 0; i < 17; i++) {
      uint8_t target[3] = {specs[m]->stops[i][0], specs[m]->stops[i][1], specs[m]->stops[i][2]};
      SnapResult got = nearest_i(*specs[m], target);
      if (got.dist > (m == 2 ? 2u : 0u)) {  // the Refugium's stops sit between whole CLR steps
        std::printf("FAIL gradient stop %d of model %d not exact (%u)\n", i, m, got.dist);
        failures++;
      }
      for (int o = 0; o < specs[m]->n_overlays; o++)
        if (got.overlays[o] != zeros[o]) {
          std::printf("FAIL gradient stop %d of model %d uses overlays\n", i, m);
          failures++;
        }
      if (got.evals > max_evals)
        max_evals = got.evals;
    }
  }

  std::printf("color snap: %d golden vectors, max evals %u\n", lines, max_evals);
  if (failures != 0) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  return 0;
}
