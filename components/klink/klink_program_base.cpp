#include "klink_program_base.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace klink {

static const char *const TAG = "klink.program";

namespace kk = kessil::klink;

void ProgramBase::setup_program_() {
  this->mode_pref_ = global_preferences->make_preference<uint8_t>(this->preference_key());
  uint8_t stored_mode = static_cast<uint8_t>(kk::Mode::PROGRAM);
  if (this->mode_pref_.load(&stored_mode)) {
    if (stored_mode > static_cast<uint8_t>(kk::Mode::OFF))
      stored_mode = static_cast<uint8_t>(kk::Mode::PROGRAM);
    // Restore silently: the next update() tick sends whatever this mode requires.
    this->mode_.mode = static_cast<kk::Mode>(stored_mode);
  }
}

void ProgramBase::send_(const uint8_t channels[5]) {
  this->output_(channels);
  std::memcpy(this->last_sent_, channels, 5);
  this->last_sent_ms_ = millis();
  this->have_sent_ = true;
}

void ProgramBase::save_mode_() {
  uint8_t mode = static_cast<uint8_t>(this->mode_.mode == kk::Mode::PREVIEW ? this->mode_.before_preview : this->mode_.mode);
  this->mode_pref_.save(&mode);
}

void ProgramBase::mode_changed_() {
  this->save_mode_();
  this->mode_cb_.call();
}

void ProgramBase::on_user_command_() {
  kk::Mode before = this->mode_.mode;
  kk::Mode before_preview = this->mode_.before_preview;
  kk::on_user_command(this->mode_);
  if (this->mode_.mode != before) {
    this->mode_changed_();
  } else if (this->mode_.before_preview != before_preview) {
    this->save_mode_();
  }
}

void ProgramBase::set_slot(size_t index, const kk::SchedulePoint &point) {
  if (index >= this->slots_.size()) {
    ESP_LOGE(TAG, "GID %u has no slot %u", this->gid(), static_cast<unsigned>(index + 1));
    return;
  }
  this->slots_[index] = point;
  this->editor_cb_.call();
}

void ProgramBase::register_slot_text(size_t index, KLinkSlotText *text) {
  if (this->slot_texts_.size() <= index)
    this->slot_texts_.resize(index + 1, nullptr);
  this->slot_texts_[index] = text;
}

void ProgramBase::select_point(size_t index) {
  if (index >= this->slots_.size())
    return;
  this->selected_ = index;
  this->editor_cb_.call();
  this->preview_point(this->selected_);
}

void ProgramBase::point_changed_() {
  if (this->selected_ < this->slot_texts_.size() && this->slot_texts_[this->selected_] != nullptr)
    this->slot_texts_[this->selected_]->store(this->slots_[this->selected_]);
  this->editor_cb_.call();
}

void ProgramBase::slot_changed_(size_t index) {
  if (index < this->slot_texts_.size() && this->slot_texts_[index] != nullptr)
    this->slot_texts_[index]->store(this->slots_[index]);
  this->editor_cb_.call();
}

bool ProgramBase::set_slot_from_values(size_t index, uint16_t minute,
                                       const uint8_t channels[kk::NUM_CHANNELS], bool enabled) {
  if (index >= this->slots_.size())
    return false;
  kk::SchedulePoint &p = this->slots_[index];
  p.minute = minute;
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++)
    p.channels[c] = channels[c];
  p.enabled = enabled;
  this->slot_changed_(index);
  this->preview_point(index);
  return true;
}

bool ProgramBase::copy_program_from(const ProgramHost &other) {
  if (std::strcmp(this->model_name().c_str(), other.model_name().c_str()) != 0 ||
      this->slots_.size() != other.num_slots())
    return false;
  for (size_t i = 0; i < this->slots_.size(); i++) {
    this->slots_[i] = other.point(i);
    this->slot_changed_(i);
  }
  return true;
}

void ProgramBase::edit_point_time(uint8_t hour, uint8_t minute) {
  if (this->slots_.empty() || hour > 23 || minute > 59)
    return;
  kk::SchedulePoint &p = this->slots_[this->selected_];
  p.minute = static_cast<uint16_t>(hour * 60 + minute);
  p.enabled = true;
  this->point_changed_();
  this->preview_point(this->selected_);
}

void ProgramBase::edit_point_channel(uint8_t index, uint8_t value) {
  if (this->slots_.empty() || index >= kk::NUM_CHANNELS)
    return;
  kk::SchedulePoint &p = this->slots_[this->selected_];
  p.channels[index] = kk::clamp_channel(index, value);
  p.enabled = true;
  this->point_changed_();
  this->preview_point(this->selected_);
}

void ProgramBase::edit_point_enabled(bool enabled) {
  if (this->slots_.empty())
    return;
  this->slots_[this->selected_].enabled = enabled;
  this->point_changed_();
  this->preview_point(this->selected_);
}

void ProgramBase::set_mode(kk::Mode mode) {
  if (mode == kk::Mode::PREVIEW)
    return;
  kk::set_mode(this->mode_, mode);
  this->have_sent_ = false;  // send the new mode's value on the next tick
  this->mode_changed_();
}

void ProgramBase::end_preview() {
  if (this->mode_.mode != kk::Mode::PREVIEW)
    return;
  kk::end_preview(this->mode_);
  this->have_sent_ = false;  // send the restored mode's value on the next tick
  this->mode_cb_.call();
}

void ProgramBase::preview_point(size_t slot) {
  if (slot >= this->slots_.size())
    return;
  const kk::SchedulePoint &p = this->slots_[slot];
  if (!p.enabled)
    return;
  uint8_t channels[5];
  std::memcpy(channels, p.channels, sizeof(channels));
  this->send_(channels);
  uint32_t now = millis();
  kk::Mode before = this->mode_.mode;
  if (before == kk::Mode::PREVIEW)
    kk::refresh_preview(this->mode_, now, 60000);
  else
    kk::begin_preview(this->mode_, now, 60000);
  if (this->mode_.mode != before)
    this->mode_cb_.call();
}

void ProgramBase::preview_minute(uint16_t minute_of_day) {
  uint8_t channels[5];
  if (!kk::schedule_compute(this->slots_, static_cast<uint32_t>(minute_of_day) * 60u, channels))
    return;  // nothing enabled
  this->send_(channels);
  uint32_t now = millis();
  kk::Mode before = this->mode_.mode;
  if (before == kk::Mode::PREVIEW)
    kk::refresh_preview(this->mode_, now, 60000);
  else
    kk::begin_preview(this->mode_, now, 60000);
  if (this->mode_.mode != before)
    this->mode_cb_.call();
}

std::string ProgramBase::status_text() {
  if (this->effect_active())
    return "Effect: " + this->effect_name();
  switch (this->mode_.mode) {
    case kk::Mode::PROGRAM: {
      if (this->time_ == nullptr)
        return "Program · waiting for clock";
      ESPTime now = this->time_->now();
      if (!now.is_valid())
        return "Program · waiting for clock";
      uint8_t channels[5];
      uint32_t second_of_day = now.hour * 3600u + now.minute * 60u + now.second;
      if (!kk::schedule_compute(this->slots_, second_of_day, channels))
        return "Program · no points enabled";
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Program · %02u:%02u · INT %u%% CLR %u%%",
                    static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute),
                    static_cast<unsigned>(std::lround(channels[0] * 100.0f / 255.0f)),
                    static_cast<unsigned>(std::lround(channels[1] * 100.0f / 255.0f)));
      return buf;
    }
    case kk::Mode::MANUAL:
      return "Manual";
    case kk::Mode::OFF:
      return "Off";
    case kk::Mode::PREVIEW:
      return "Editing point " + std::to_string(this->selected_ + 1) + " (previewing)";
  }
  return "";
}

int ProgramBase::now_minute() const {
  if (this->time_ == nullptr)
    return -1;
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return -1;
  return static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute);
}

bool ProgramBase::compute_now(uint8_t out[5]) {
  if (this->time_ == nullptr)
    return false;
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return false;
  return kk::schedule_compute(this->slots_, now.hour * 3600u + now.minute * 60u + now.second, out);
}

void ProgramBase::update() {
  switch (this->mode_.mode) {
    case kk::Mode::PROGRAM: {
      if (this->time_ == nullptr)
        return;
      ESPTime now = this->time_->now();
      if (!now.is_valid())
        return;  // no clock yet: hold whatever the lamps are doing
      uint8_t channels[5];
      uint32_t second_of_day = now.hour * 3600u + now.minute * 60u + now.second;
      if (!kk::schedule_compute(this->slots_, second_of_day, channels))
        return;  // nothing enabled
      bool changed = !this->have_sent_ || std::memcmp(channels, this->last_sent_, 5) != 0;
      if (changed || millis() - this->last_sent_ms_ >= this->refresh_ms_)
        this->send_(channels);
      return;
    }
    case kk::Mode::OFF: {
      uint8_t channels[5] = {0, 0, 0, 0, 0};
      bool changed = !this->have_sent_ || std::memcmp(channels, this->last_sent_, 5) != 0;
      if (changed || millis() - this->last_sent_ms_ >= this->refresh_ms_)
        this->send_(channels);
      return;
    }
    case kk::Mode::MANUAL:
      return;
    case kk::Mode::PREVIEW:
      if (kk::preview_expired(this->mode_, millis()))
        this->end_preview();
      return;
  }
}

}  // namespace klink
}  // namespace esphome
