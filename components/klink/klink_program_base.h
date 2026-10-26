#pragma once
// Everything a program host does that is not specific to the thing it drives: slots, the point
// editor, the operating mode, previews, the status line and the per-second schedule tick. KLinkGroup
// (a K-Link bus group) derives from this; a program that drives ordinary ESPHome lights implements
// the same ProgramHost interface on top of the same base.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"

#include "klink_mode.h"
#include "klink_program.h"
#include "klink_schedule.h"

namespace esphome {
namespace klink {

class KLinkSlotText;

class ProgramBase : public ProgramHost {
 public:
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_refresh_interval(uint32_t ms) { this->refresh_ms_ = ms; }
  void set_num_slots(size_t n) {
    kessil::klink::SchedulePoint blank = {0, {0, 0, 0, 0, 0}, false};
    this->slots_.assign(n, blank);
  }

  size_t num_slots() const override { return this->slots_.size(); }
  size_t selected_point() const override { return this->selected_; }
  const kessil::klink::SchedulePoint &point(size_t index) const override { return this->slots_[index]; }
  bool default_point(size_t index, kessil::klink::SchedulePoint &out) const override;
  void select_point(size_t index) override;
  void add_on_editor_callback(std::function<void()> &&cb) override { this->editor_cb_.add(std::move(cb)); }
  void register_slot_text(size_t index, KLinkSlotText *text) override;
  void set_slot(size_t index, const kessil::klink::SchedulePoint &point) override;
  bool set_slot_from_values(size_t index, uint16_t minute,
                            const uint8_t channels[kessil::klink::NUM_CHANNELS], bool enabled) override;
  bool copy_program_from(const ProgramHost &other) override;
  void edit_point_time(uint8_t hour, uint8_t minute) override;
  void edit_point_channel(uint8_t index, uint8_t value) override;
  void edit_point_enabled(bool enabled) override;
  kessil::klink::Mode mode() const override { return this->mode_.mode; }
  void set_mode(kessil::klink::Mode mode) override;
  void add_on_mode_callback(std::function<void()> &&cb) override { this->mode_cb_.add(std::move(cb)); }
  void preview_point(size_t slot) override;
  void preview_minute(uint16_t minute_of_day) override;
  void end_preview() override;
  std::string status_text() override;
  int now_minute() const override;
  void update();  // schedule tick, run every few seconds from a timer
  // Our scheduled value right now. False when there is no clock or no enabled slot.
  bool compute_now(uint8_t out[5]);

 protected:
  // What send_() does with the channels besides remembering them (the bus group writes its group).
  virtual void output_(const uint8_t channels[5]) = 0;
  // Whether a held value (Manual mode, a point preview) must be repeated about once a second. The
  // Kessil WiFi dongle repeats group writes once a second, and whether lamps hold a level without
  // repeats is untested, so the bus group repeats; programs for ordinary lights do not.
  virtual bool repeat_held_value_() const { return false; }
  virtual bool effect_active() const = 0;
  virtual std::string effect_name() const = 0;
  // The preference key used for the operating mode.
  virtual uint32_t preference_key() const = 0;
  // Create/restore the mode preference. Subclasses call this from their setup().
  void setup_program_();

  void send_(const uint8_t channels[5]);
  void repeat_held_value_if_due_();
  void point_changed_();
  // Persist one specific slot through its text entity (unlike point_changed_, which stores the
  // selected point). Used by the web API's per-slot editor.
  void slot_changed_(size_t index);
  // A user command takes manual control and notifies/persists when the mode changes.
  void on_user_command_();
  void mode_changed_();
  void save_mode_();

  time::RealTimeClock *time_{nullptr};
  size_t selected_{0};
  std::vector<KLinkSlotText *> slot_texts_;
  CallbackManager<void()> editor_cb_;
  CallbackManager<void()> mode_cb_;
  ESPPreferenceObject mode_pref_;
  uint32_t refresh_ms_{1000};
  std::vector<kessil::klink::SchedulePoint> slots_;
  kessil::klink::ModeState mode_;
  bool have_sent_{false};
  uint8_t last_sent_[5]{0, 0, 0, 0, 0};
  uint32_t last_sent_ms_{0};
};

}  // namespace klink
}  // namespace esphome
