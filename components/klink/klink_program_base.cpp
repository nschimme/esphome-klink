#include "klink_program_base.h"

#include "klink.h"  // KLinkSlotText

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"

#include "klink_effects.h"

namespace esphome {
namespace klink {

static const char *const TAG = "klink.program";

namespace kk = kessil::klink;

static bool plan_valid(const kk::AcclimationPlan &p) {
  return p.start_pct >= 10 && p.start_pct <= 90 && p.days >= 1 && p.days <= 90;
}

void ProgramBase::setup_program_() {
  this->acc_pref_ = global_preferences->make_preference<kk::AcclimationPlan>(fnv1_hash("klink_acc_") + this->preference_key());
  kk::AcclimationPlan stored_plan;
  if (this->acc_pref_.load(&stored_plan) && plan_valid(stored_plan))
    this->acc_ = stored_plan;
  this->mode_pref_ = global_preferences->make_preference<uint8_t>(this->preference_key());
  uint8_t stored_mode = static_cast<uint8_t>(kk::Mode::PROGRAM);
  if (this->mode_pref_.load(&stored_mode)) {
    if (stored_mode > static_cast<uint8_t>(kk::Mode::OFF))
      stored_mode = static_cast<uint8_t>(kk::Mode::PROGRAM);
    // Restore silently: the next update() tick sends whatever this mode requires.
    this->mode_.mode = static_cast<kk::Mode>(stored_mode);
  }
}

float ProgramBase::acclimation_share() const {
  if (this->time_ == nullptr)
    return 1.0f;
  ESPTime now = this->time_->now();
  return now.is_valid() ? kk::acclimation_scale(this->acc_, static_cast<uint32_t>(now.timestamp)) : 1.0f;
}

void ProgramBase::set_acclimation(const kk::AcclimationPlan &requested) {
  kk::AcclimationPlan plan = requested;
  if (plan.start_pct < 10) plan.start_pct = 10;
  if (plan.start_pct > 90) plan.start_pct = 90;
  if (plan.days < 1) plan.days = 1;
  if (plan.days > 90) plan.days = 90;
  if (plan.on && !this->acc_.on) {  // starting now
    plan.since = 0;
    if (this->time_ != nullptr) {
      ESPTime now = this->time_->now();
      if (now.is_valid())
        plan.since = static_cast<uint32_t>(now.timestamp);
    }
  } else if (!plan.on) {
    plan.since = 0;
  } else {
    plan.since = this->acc_.since;  // changing the numbers does not restart the days
  }
  this->acc_ = plan;
  this->acc_pref_.save(&this->acc_);
  this->have_sent_ = false;
  this->acc_cb_.call();
  this->mode_cb_.call();
}

void ProgramBase::setup_source_() {
  if (!this->has_sources())
    return;
  this->generated_.reserve(16);
  this->source_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("klink_src_") + this->preference_key());
  uint8_t stored = 0;
  if (this->source_pref_.load(&stored) && stored <= static_cast<uint8_t>(ProgramSource::MOON) &&
      (stored != static_cast<uint8_t>(ProgramSource::MOON) || this->can_moon()))
    this->source_ = static_cast<ProgramSource>(stored);
  this->sky_pref_ = global_preferences->make_preference<SkySettings>(fnv1_hash("klink_sky_") + this->preference_key());
  SkySettings sky;
  if (this->sky_pref_.load(&sky) && sky.peak <= 100 && (sky.hours == 0 || (sky.hours >= 4 && sky.hours <= 16)) &&
      sky.glow <= 100 && sky.amber <= 10 && sky.ramp <= 24 && sky.shift >= -120 && sky.shift <= 120 && sky.moon <= 100 &&
      sky.phase <= 1)
    this->sky_ = sky;
  this->apply_sky_();
}

void ProgramBase::apply_sky_() {
  this->sun_params_ = kk::SunParams();
  this->sun_params_.peak_int = static_cast<int>(std::lround(this->sky_.peak * 2.55));
  this->sun_params_.glow = this->sky_.glow;
  this->sun_params_.night_amber = this->sky_.amber;
  this->sun_params_.ramp_min = static_cast<int>(this->sky_.ramp) * 10;
  this->sun_params_.shift_min = this->sky_.shift;
  this->moon_params_ = kk::MoonParams();
  this->moon_params_.max_int = static_cast<int>(std::lround(this->sky_.moon * 2.55));
  this->moon_params_.follow_phase = this->sky_.phase != 0;
  this->moon_params_.shift_min = this->sky_.shift;
  this->moon_params_.amber = this->sky_.amber;
}

void ProgramBase::set_sky_settings(const SkySettings &settings) {
  SkySettings s = settings;
  if (s.peak > 100) s.peak = 100;
  if (s.hours != 0) s.hours = s.hours < 4 ? 4 : (s.hours > 16 ? 16 : s.hours);
  if (s.glow > 100) s.glow = 100;
  if (s.amber > 10) s.amber = 10;
  if (s.ramp > 24) s.ramp = 24;
  if (s.shift < -120) s.shift = -120;
  if (s.shift > 120) s.shift = 120;
  if (s.moon > 100) s.moon = 100;
  if (s.phase > 1) s.phase = 1;
  this->sky_ = s;
  this->sky_pref_.save(&this->sky_);
  this->apply_sky_();
  this->generated_ok_ = false;  // the settings changed: rebuild now
  this->generated_key_ = -1;
  this->refresh_generated_();
  this->have_sent_ = false;
  this->source_cb_.call();
  this->mode_cb_.call();
}

void ProgramBase::set_source(ProgramSource source) {
  if (!this->has_sources() || source == this->source_)
    return;
  if (source == ProgramSource::MOON && !this->can_moon()) {
    ESP_LOGW(TAG, "The moon needs a light that makes blue: this program stays on %s",
             this->source_ == ProgramSource::SUN ? "the sun" : "My points");
    this->source_cb_.call();  // snap a select back to the real source
    return;
  }
  this->source_ = source;
  uint8_t stored = static_cast<uint8_t>(source);
  this->source_pref_.save(&stored);
  this->refresh_generated_();
  this->have_sent_ = false;  // the new schedule shows on the next tick
  this->source_cb_.call();
  this->mode_cb_.call();  // the status line changes with it
}

// The UTC offset in effect now, in minutes (the clock gives local fields and a UTC timestamp).
static int local_offset_minutes(const ESPTime &now) {
  ESPTime utc = ESPTime::from_epoch_utc(now.timestamp);
  int day_diff = static_cast<int>(now.day_of_year) - static_cast<int>(utc.day_of_year);
  if (day_diff > 1)
    day_diff = -1;  // local is in the new year, UTC still in the old one
  else if (day_diff < -1)
    day_diff = 1;
  return (now.hour * 60 + now.minute) - (utc.hour * 60 + utc.minute) + day_diff * 1440;
}

// Build the generated day when it is out of date: another day (for the moon, another night, which turns
// over at local noon), another source, or a moved location. Cheap when nothing changed.
void ProgramBase::refresh_generated_() {
  if (this->source_ == ProgramSource::POINTS || !this->has_sources()) {
    this->generated_ok_ = false;
    this->source_note_.clear();
    return;
  }
  double lat = 0.0, lon = 0.0;
  if (this->location_ == nullptr || !this->location_->location(lat, lon)) {
    this->generated_ok_ = false;
    this->source_note_ = "Set a location";
    return;
  }
  if (this->time_ == nullptr)
    return;
  ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    this->generated_ok_ = false;
    this->source_note_ = "waiting for clock";
    return;
  }
  // The key: the calendar day; for the moon the night that started at the last local noon.
  ESPTime anchor = now;
  if (this->source_ == ProgramSource::MOON && now.hour < 12)
    anchor = ESPTime::from_epoch_local(now.timestamp - 24 * 3600);
  const int key = anchor.year * 10000 + anchor.month * 100 + anchor.day_of_month;
  if (this->generated_ok_ && key == this->generated_key_ && this->source_ == this->generated_source_ &&
      lat == this->generated_lat_ && lon == this->generated_lon_)
    return;
  // A usable day already exists and only needs renewing: wait for a quiet moment (the next tick tries again).
  if (this->generated_ok_ && (this->effect_active() || this->output_busy_()))
    return;
  const uint32_t started = micros();
  const int tz = local_offset_minutes(now);
  // Assign in place: the capacity is reserved, so the vector the HTTP task may be reading never moves.
  const std::vector<kk::SchedulePoint> fresh =
      this->build_day_(this->source_, anchor.year, anchor.month, anchor.day_of_month, tz, lat, lon, nullptr);
  this->generated_.assign(fresh.begin(), fresh.end());
  this->generated_ok_ = !this->generated_.empty();
  this->generated_key_ = key;
  this->generated_source_ = this->source_;
  this->generated_lat_ = lat;
  this->generated_lon_ = lon;
  this->source_note_.clear();
  ESP_LOGI(TAG, "Generated %s day for %04d-%02d-%02d: %u points in %u ms", this->source_ == ProgramSource::SUN ? "sun" : "moon",
           anchor.year, anchor.month, anchor.day_of_month, static_cast<unsigned>(this->generated_.size()),
           static_cast<unsigned>((micros() - started) / 1000));
}

// The points (and optionally a JSON summary of the day's events) for one local date. Pure apart from the
// settings it reads, so the HTTP task may call it on a copy.
std::vector<kk::SchedulePoint> ProgramBase::build_day_(ProgramSource source, int y, int m, int d, int tz, double lat,
                                                       double lon, std::string *summary) const {
  char buf[200];
  if (source == ProgramSource::SUN) {
    kk::SunDay day = kk::sun_day(y, m, d, lat, lon, tz);
    if (this->sky_.hours != 0)
      day = kk::fixed_photoperiod(day, this->sky_.hours);
    if (summary != nullptr) {
      const int length = (day.sunrise >= 0 && day.sunset >= 0) ? day.sunset - day.sunrise : (day.always_up ? 1440 : 0);
      std::snprintf(buf, sizeof(buf),
                    "{\"sunrise\":%d,\"noon\":%d,\"sunset\":%d,\"length\":%d,\"always_up\":%s}", day.sunrise, day.noon,
                    day.sunset, length, day.always_up ? "true" : "false");
      *summary = buf;
    }
    return kk::sun_points(day, this->sun_params_, this->dyn_model_());
  }
  kk::MoonNight night = kk::moon_night(y, m, d, lat, lon, tz);
  if (summary != nullptr) {
    std::snprintf(buf, sizeof(buf), "{\"rise\":%d,\"set\":%d,\"illumination\":%.2f}", night.rise, night.set,
                  night.illumination);
    *summary = buf;
  }
  return kk::moon_points(night, this->moon_params_, this->dyn_model_());
}

// The UTC offset in minutes at local noon of a date (so a date in the other half of the year gets its own).
int ProgramBase::utc_offset_for_(int year, int month, int day) const {
  ESPTime t{};
  t.year = year;
  t.month = month;
  t.day_of_month = day;
  t.hour = 12;
  t.minute = 0;
  t.second = 0;
  t.recalc_timestamp_local();
  ESPTime local = ESPTime::from_epoch_local(t.timestamp);
  return local_offset_minutes(local);
}

bool ProgramBase::generate_day(ProgramSource source, int year, int month, int day,
                               std::vector<kk::SchedulePoint> &points, std::string &summary) const {
  double lat = 0.0, lon = 0.0;
  if (source == ProgramSource::POINTS || !this->has_sources() || this->location_ == nullptr ||
      !this->location_->location(lat, lon))
    return false;
  points = this->build_day_(source, year, month, day, this->utc_offset_for_(year, month, day), lat, lon, &summary);
  return !points.empty();
}

// Today's generated day as the user's own points, simplified to the slots there are.
bool ProgramBase::copy_generated_to_points() {
  if (!this->generated_ok_)
    return false;
  std::vector<kk::SchedulePoint> fit = kk::simplify_points(this->generated_, this->slots_.size());
  kk::SchedulePoint off = {0, {0, 0, 0, 0, 0}, false};
  for (size_t i = 0; i < this->slots_.size(); i++)
    this->set_slot(i, i < fit.size() ? fit[i] : off);
  this->set_source(ProgramSource::POINTS);
  return true;
}

void ProgramBase::send_(const uint8_t channels[5]) {
  this->output_(channels);
  this->direct_next_ = false;
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

bool ProgramBase::default_point(size_t index, kk::SchedulePoint &out) const {
  out.minute = 0;
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++)
    out.channels[c] = 0;
  out.enabled = false;
  if (index >= this->slot_texts_.size() || this->slot_texts_[index] == nullptr)
    return false;
  kk::SchedulePoint parsed;
  if (!kk::parse_slot(this->slot_texts_[index]->initial_value(), parsed, this->slot_labels()))
    return false;
  out = parsed;
  return true;
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
  if (this->effect_active())
    return;  // an effect owns the output; previews must not fight it
  if (slot >= this->slots_.size())
    return;
  const kk::SchedulePoint &p = this->slots_[slot];
  if (!p.enabled)
    return;
  this->preview_minute_ = -1;
  uint8_t channels[5];
  std::memcpy(channels, p.channels, sizeof(channels));
  this->direct_next_ = true;
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
  if (this->effect_active())
    return;  // an effect owns the output; previews must not fight it
  uint8_t channels[5];
  if (!kk::schedule_compute(this->active_points(), static_cast<uint32_t>(minute_of_day) * 60u, channels))
    return;  // nothing enabled
  this->preview_minute_ = static_cast<int>(minute_of_day);
  this->direct_next_ = true;
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
  if (this->effect_active()) {
    std::string label = this->effect_label();
    if (label.empty())
      label = this->effect_name();
    int32_t remaining = this->effect_remaining_seconds();
    if (remaining < 0)
      return "Effect: " + label;  // runs until stopped
    if (remaining == 0)
      return "Effect: " + label;  // about to end
    return "Effect: " + label + " · " + kessil::klink::format_remaining(remaining) + " left";
  }
  switch (this->mode_.mode) {
    case kk::Mode::PROGRAM: {
      if (this->time_ == nullptr)
        return "Program · waiting for clock";
      ESPTime now = this->time_->now();
      if (!now.is_valid())
        return "Program · waiting for clock";
      if (!this->source_note_.empty() && this->source_ != ProgramSource::POINTS)
        return "Program · " + this->source_note_;
      uint8_t channels[5];
      uint32_t second_of_day = now.hour * 3600u + now.minute * 60u + now.second;
      if (!kk::schedule_compute(this->active_points(), second_of_day, channels))
        return "Program · no points enabled";
      // Every channel the model has, by its own label: "INT 42% CLR 26% R 0% AM 0%", "INT 53% R 90% G 100% B 60%".
      char buf[128];
      const char *name = this->generated_ok_ ? (this->source_ == ProgramSource::SUN ? "Sun" : "Moon") : "Program";
      int len = std::snprintf(buf, sizeof(buf), "%s · %02u:%02u", name, static_cast<unsigned>(now.hour),
                              static_cast<unsigned>(now.minute));
      const kk::SlotLabels *labels = this->slot_labels();
      for (size_t i = 0; i < kk::NUM_CHANNELS && len > 0 && static_cast<size_t>(len) < sizeof(buf); i++) {
        if (labels->name[i] == nullptr)
          continue;
        unsigned value = kk::CHANNEL_MAX[i] == 255 ? static_cast<unsigned>(std::lround(channels[i] * 100.0f / 255.0f))
                                                   : static_cast<unsigned>(channels[i]);
        char label[8];
        size_t n = 0;
        for (; labels->name[i][n] != '\0' && n < sizeof(label) - 1; n++)
          label[n] = static_cast<char>(std::toupper(static_cast<unsigned char>(labels->name[i][n])));
        label[n] = '\0';
        len += std::snprintf(buf + len, sizeof(buf) - len, " · %s %u%%", label, value);
      }
      if (this->generated_ok_ && this->source_ == ProgramSource::SUN && this->dim_ >= 0.01f && len > 0 &&
          static_cast<size_t>(len) < sizeof(buf)) {
        len += std::snprintf(buf + len, sizeof(buf) - len, " · clouds -%u%%", static_cast<unsigned>(std::lround(this->dim_ * 100.0f)));
      }
      if (this->acc_.on && this->acc_.since != 0 && len > 0 && static_cast<size_t>(len) < sizeof(buf)) {
        const float share = kk::acclimation_scale(this->acc_, static_cast<uint32_t>(now.timestamp));
        std::snprintf(buf + len, sizeof(buf) - len, " · acclimating %u%%", static_cast<unsigned>(std::lround(share * 100.0f)));
      }
      return buf;
    }
    case kk::Mode::MANUAL:
      return "Manual";
    case kk::Mode::OFF:
      return "Off";
    case kk::Mode::PREVIEW:
      if (this->preview_minute_ >= 0) {
        char when[40];
        std::snprintf(when, sizeof(when), "Previewing %02d:%02d", this->preview_minute_ / 60, this->preview_minute_ % 60);
        return when;
      }
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
  return kk::schedule_compute(this->active_points(), now.hour * 3600u + now.minute * 60u + now.second, out);
}

void ProgramBase::update() {
  this->refresh_generated_();
  // The acclimation ramp: start counting once the clock is known, switch off when the days are up.
  if (this->acc_.on && this->time_ != nullptr) {
    ESPTime clock = this->time_->now();
    if (clock.is_valid()) {
      const uint32_t t = static_cast<uint32_t>(clock.timestamp);
      if (this->acc_.since == 0) {
        this->acc_.since = t;
        this->acc_pref_.save(&this->acc_);
      } else if (kk::acclimation_finished(this->acc_, t)) {
        this->acc_.on = false;
        this->acc_.since = 0;
        this->acc_pref_.save(&this->acc_);
        this->acc_cb_.call();
        this->mode_cb_.call();
      }
    }
  }
  // An effect is a temporary overlay: while one runs it is the only thing writing to the lamps.
  if (this->effect_active())
    return;
  switch (this->mode_.mode) {
    case kk::Mode::PROGRAM: {
      if (this->time_ == nullptr)
        return;
      ESPTime now = this->time_->now();
      if (!now.is_valid())
        return;  // no clock yet: hold whatever the lamps are doing
      uint8_t channels[5];
      uint32_t second_of_day = now.hour * 3600u + now.minute * 60u + now.second;
      if (!kk::schedule_compute(this->active_points(), second_of_day, channels))
        return;  // nothing enabled
      // Clouds take a share off the sun's intensity, easing in and out over minutes so it never flickers.
      this->dim_ = kk::weather_ease(this->dim_, this->location_ != nullptr ? this->location_->weather_dimming() : 0.0f, 0.002f);
      if (this->generated_ok_ && this->source_ == ProgramSource::SUN && this->dim_ > 0.001f)
        channels[0] = static_cast<uint8_t>(std::lround(channels[0] * (1.0f - this->dim_)));
      if (this->acc_.on)
        channels[0] = static_cast<uint8_t>(std::lround(channels[0] * kk::acclimation_scale(this->acc_, static_cast<uint32_t>(now.timestamp))));
      bool changed = !this->have_sent_ || std::memcmp(channels, this->last_sent_, 5) != 0;
      if (changed || millis() - this->last_sent_ms_ >= this->refresh_ms_)
        this->send_(channels);
      return;
    }
    case kk::Mode::OFF: {
      // Off is intensity and the extra LEDs at zero; the color axis keeps its value so it does not
      // sweep while the lamp fades out and back in.
      uint8_t channels[5] = {0, this->last_sent_[1], 0, 0, 0};
      bool changed = !this->have_sent_ || std::memcmp(channels, this->last_sent_, 5) != 0;
      if (changed || millis() - this->last_sent_ms_ >= this->refresh_ms_)
        this->send_(channels);
      return;
    }
    case kk::Mode::MANUAL:
      this->repeat_held_value_if_due_();
      return;
    case kk::Mode::PREVIEW:
      if (kk::preview_expired(this->mode_, millis())) {
        this->end_preview();
        return;
      }
      this->repeat_held_value_if_due_();
      return;
  }
}

void ProgramBase::repeat_held_value_if_due_() {
  if (!this->repeat_held_value_() || !this->have_sent_ || millis() - this->last_sent_ms_ < this->refresh_ms_)
    return;
  uint8_t channels[5];
  std::memcpy(channels, this->last_sent_, sizeof(channels));
  this->send_(channels);
}

}  // namespace klink
}  // namespace esphome
