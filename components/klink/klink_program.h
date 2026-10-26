#pragma once
// The interface a program host exposes to the program entities (slot text, point editor, mode
// select, ...) and to the web API. Today the only implementation is KLinkGroup, which drives a
// K-Link bus group; programs that drive ordinary ESPHome lights implement the same interface.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "klink_mode.h"
#include "klink_schedule.h"

namespace esphome {
namespace klink {

class KLinkSlotText;

class ProgramHost {
 public:
  virtual ~ProgramHost() = default;

  // "bus" for a K-Link group; other kinds are up to later implementations.
  virtual const char *kind() const = 0;
  virtual uint8_t gid() const = 0;
  virtual std::string name() const = 0;
  virtual std::string model_name() const = 0;
  virtual const kessil::klink::SlotLabels *slot_labels() const = 0;
  virtual size_t num_slots() const = 0;
  virtual const kessil::klink::SchedulePoint &point(size_t index) const = 0;
  // The shipped ("factory") point for a slot, independent of edits stored since. False when there is
  // no slot text for `index` or its initial value does not parse; `out` is then a disabled point.
  virtual bool default_point(size_t index, kessil::klink::SchedulePoint &out) const = 0;
  virtual size_t selected_point() const = 0;
  virtual void select_point(size_t index) = 0;
  virtual void edit_point_time(uint8_t hour, uint8_t minute) = 0;
  virtual void edit_point_channel(uint8_t index, uint8_t value) = 0;
  virtual void edit_point_enabled(bool enabled) = 0;
  virtual bool set_slot_from_values(size_t index, uint16_t minute,
                                    const uint8_t channels[kessil::klink::NUM_CHANNELS], bool enabled) = 0;
  virtual void set_slot(size_t index, const kessil::klink::SchedulePoint &point) = 0;
  virtual bool copy_program_from(const ProgramHost &other) = 0;
  virtual void register_slot_text(size_t index, KLinkSlotText *text) = 0;
  virtual void add_on_editor_callback(std::function<void()> &&cb) = 0;
  virtual kessil::klink::Mode mode() const = 0;
  virtual void set_mode(kessil::klink::Mode mode) = 0;
  virtual void add_on_mode_callback(std::function<void()> &&cb) = 0;
  virtual std::string status_text() = 0;
  virtual void preview_point(size_t slot) = 0;
  virtual void preview_minute(uint16_t minute_of_day) = 0;
  virtual void end_preview() = 0;
  virtual int now_minute() const = 0;
  virtual void set_extra(uint8_t channel, uint8_t value) = 0;
  virtual uint8_t extra(uint8_t channel) const = 0;
  virtual void set_extra_silent(uint8_t channel, uint8_t value) = 0;

  // Effects as a temporary overlay on the schedule. Hosts without effects keep these defaults.
  // The light's effect names, without "None".
  virtual std::vector<std::string> effect_names() const { return {}; }
  virtual bool start_effect(const std::string &name) { return false; }
  virtual void stop_effect() {}
  // Seconds left, 0 when no effect is running, -1 when it runs until stopped.
  virtual int32_t effect_remaining_seconds() const { return 0; }
  // Name of the running effect, empty when none is running.
  virtual std::string effect_label() const { return ""; }
  virtual uint8_t effect_speed() const { return 0; }  // 0 fast, 1 moderate, 2 slow
  virtual uint32_t effect_duration_ms() const { return 120000; }
  virtual void set_effect_options(uint8_t speed, uint32_t duration_ms) {}
};

}  // namespace klink
}  // namespace esphome
