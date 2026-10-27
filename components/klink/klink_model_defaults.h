#pragma once
// The stock daily schedule of each lamp model, used when a group has no YAML point entities.
// Values are copied from common/group-*.yaml. Pure logic, host tested.
#include <cstddef>
#include <cstdint>

#include "klink_schedule.h"

namespace kessil {
namespace klink {

static const size_t STOCK_POINTS = 16;

inline const SchedulePoint *stock_defaults(uint8_t lamp_type) {
  // 1 = Tuna Blue / AP9X, 2 = Tuna Sun, 3 = Refugium.
  static const SchedulePoint TUNA_BLUE[STOCK_POINTS] = {
      {8 * 60, {2, 0, 1, 0, 0}, true},       {9 * 60, {51, 25, 20, 0, 0}, true},
      {11 * 60, {102, 102, 40, 3, 3}, true}, {13 * 60, {153, 178, 60, 5, 5}, true},
      {15 * 60, {153, 178, 60, 5, 5}, true}, {17 * 60, {102, 127, 40, 3, 3}, true},
      {19 * 60, {51, 51, 20, 0, 0}, true},   {20 * 60, {0, 0, 0, 0, 0}, true},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
  };
  static const SchedulePoint TUNA_SUN[STOCK_POINTS] = {
      {8 * 60, {2, 0, 0, 0, 0}, true},       {9 * 60, {51, 12, 0, 10, 30}, true},
      {11 * 60, {102, 51, 0, 5, 10}, true},  {13 * 60, {153, 89, 0, 8, 15}, true},
      {15 * 60, {153, 89, 0, 8, 15}, true},  {17 * 60, {102, 63, 0, 5, 10}, true},
      {19 * 60, {51, 25, 0, 10, 30}, true},  {20 * 60, {0, 0, 0, 0, 0}, true},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
  };
  static const SchedulePoint REFUGIUM[STOCK_POINTS] = {
      {8 * 60, {0, 0, 0, 0, 0}, true},       {9 * 60, {102, 150, 0, 0, 0}, true},
      {17 * 60, {102, 150, 0, 0, 0}, true},  {18 * 60, {0, 0, 0, 0, 0}, true},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
      {0, {0, 0, 0, 0, 0}, false},           {0, {0, 0, 0, 0, 0}, false},
  };
  if (lamp_type == 1)
    return TUNA_BLUE;
  if (lamp_type == 2)
    return TUNA_SUN;
  return REFUGIUM;
}

inline bool stock_default_point(uint8_t lamp_type, size_t index, SchedulePoint &out) {
  if (index >= STOCK_POINTS)
    return false;
  out = stock_defaults(lamp_type)[index];
  return true;
}

}  // namespace klink
}  // namespace kessil
