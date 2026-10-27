#include "klink_light_program.h"

#include <algorithm>
#include <utility>

#include "esphome/core/log.h"

#include "klink.h"  // KLinkHub

namespace esphome {
namespace klink {

static const char *const TAG = "klink.light_program";

namespace kk = kessil::klink;

static std::vector<KLinkLightProgram *> &all_programs() {
  static std::vector<KLinkLightProgram *> programs;
  return programs;
}

KLinkLightProgram *KLinkLightProgram::for_light(const light::LightState *light) {
  for (KLinkLightProgram *program : all_programs())
    for (const light::LightState *candidate : program->lights_)
      if (candidate == light)
        return program;
  return nullptr;
}

KLinkLightProgram::KLinkLightProgram() { all_programs().push_back(this); }

void KLinkLightProgram::setup() {
  // Register with the hub first so the web API can already see the program.
  if (this->hub_ != nullptr) {
    this->hub_->register_program(this);
    this->hub_->note_clock(this->time_);
  }
  this->setup_program_();
  this->setup_source_();

  // The schedule tick is once a second for every program; refresh_ms_ (the configured update
  // interval) decides how often an unchanged value is re-sent.
  this->set_interval("program", 1000, [this]() { this->update(); });

  const size_t n = this->lights_.size();
  this->caps_.resize(n);
  this->ranges_.resize(n);
  this->expected_.resize(n);
  for (size_t i = 0; i < n; i++) {
    this->expected_[i] = kk::LightValues{};
    this->ranges_[i].min_mireds = kk::DEFAULT_MIN_MIREDS;
    this->ranges_[i].max_mireds = kk::DEFAULT_MAX_MIREDS;
  }
  // Reserve before any listener is registered: the vector must never reallocate afterwards (the
  // pointees would survive, but the rule keeps the ownership obvious).
  this->watchers_.reserve(n);

  for (size_t i = 0; i < n; i++) {
    light::LightState *light = this->lights_[i];
    if (light == nullptr) {
      this->caps_[i] = kk::CAP_BRIGHTNESS;
      continue;
    }
    const light::LightTraits traits = light->get_traits();
    this->caps_[i] = light_caps_of(traits);
    this->ranges_[i].min_mireds = traits.get_min_mireds();
    this->ranges_[i].max_mireds = traits.get_max_mireds();

    std::unique_ptr<Watcher> watcher(new Watcher());
    watcher->owner = this;
    watcher->light = light;
    light->add_remote_values_listener(watcher.get());
    this->watchers_.push_back(std::move(watcher));

    this->log_missing_caps_(i);
  }
}

void KLinkLightProgram::log_missing_caps_(size_t index) const {
  if (index >= this->caps_.size() || index >= this->lights_.size() || this->lights_[index] == nullptr)
    return;
  const uint8_t caps = this->caps_[index];
  const std::string light_name = this->lights_[index]->get_name().str();
  if (this->model_ == kk::LightModel::CT && (caps & kk::CAP_CT) == 0) {
    ESP_LOGW(TAG, "Program '%s': light '%s' has no color temperature; its ct channel is ignored for it",
             this->name_.c_str(), light_name.c_str());
  }
  if ((this->model_ == kk::LightModel::RGB || this->model_ == kk::LightModel::RGBW) && (caps & kk::CAP_RGB) == 0) {
    ESP_LOGW(TAG, "Program '%s': light '%s' has no RGB control; its r/g/b channels are ignored for it",
             this->name_.c_str(), light_name.c_str());
  }
  if (this->model_ == kk::LightModel::RGBW && (caps & kk::CAP_WHITE) == 0) {
    ESP_LOGW(TAG, "Program '%s': light '%s' has no white channel; its w channel is ignored for it",
             this->name_.c_str(), light_name.c_str());
  }
}

void KLinkLightProgram::dump_config() {
  ESP_LOGCONFIG(TAG, "K-Link light program '%s' (id %u):", this->name_.c_str(),
                static_cast<unsigned>(this->program_id_));
  // No K-Link bus and no "Send to lights" gate: these lights are driven even in dry run.
  ESP_LOGCONFIG(TAG, "  Drives %u ESPHome light(s) directly (no K-Link bus, also in dry run)",
                static_cast<unsigned>(this->lights_.size()));
  ESP_LOGCONFIG(TAG, "  Model: %s, slots: %u, update: %ums, transition: %ums",
                kk::light_model_name(this->model_), static_cast<unsigned>(this->num_slots()),
                static_cast<unsigned>(this->refresh_ms_), static_cast<unsigned>(this->transition_ms_));
  for (size_t i = 0; i < this->lights_.size(); i++) {
    if (this->lights_[i] == nullptr) {
      ESP_LOGCONFIG(TAG, "  Light %u: (missing)", static_cast<unsigned>(i + 1));
      continue;
    }
    ESP_LOGCONFIG(TAG, "  Light %u: '%s' caps=0x%02x mireds=%.0f-%.0f", static_cast<unsigned>(i + 1),
                  this->lights_[i]->get_name().str().c_str(), static_cast<unsigned>(this->caps_[i]),
                  this->ranges_[i].min_mireds, this->ranges_[i].max_mireds);
    this->log_missing_caps_(i);
  }
}

void KLinkLightProgram::output_(const uint8_t channels[5]) {
  // An effect (or a find) owns the light right now: hold the schedule back (the base still marks the value as
  // sent, and the periodic refresh resumes control once it stops).
  if (this->effect_active() || this->find_step_ >= 0)
    return;

  const uint32_t transition_ms = this->mode_.mode == kk::Mode::PREVIEW ? 200 : this->transition_ms_;

  // One flag around every perform(): each one publishes synchronously and fires our listeners.
  this->applying_ = true;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    light::LightState *light = this->lights_[i];
    if (light == nullptr)
      continue;
    kk::LightValues v = kk::to_light_values(this->model_, channels, this->ranges_[i]);
    // A point with brightness but no color at all (r = g = b = 0, no white) would light the light
    // black: treat it as off instead of sending an all-zero color.
    if ((this->model_ == kk::LightModel::RGB || this->model_ == kk::LightModel::RGBW) && v.r <= 0.0f &&
        v.g <= 0.0f && v.b <= 0.0f && (this->model_ == kk::LightModel::RGB || v.w <= 0.0f))
      v.on = false;
    this->expected_[i] = v;

    auto call = light->make_call();
    call.set_state(v.on);
    if (v.on) {
      call.set_brightness(v.brightness);
      const uint8_t caps = this->caps_[i];
      // Only setters this light actually supports: ESPHome logs a warning and may drop the call
      // for a value the light's color mode cannot represent.
      if (this->model_ == kk::LightModel::CT && (caps & kk::CAP_CT) != 0)
        call.set_color_temperature(v.mireds);
      if ((this->model_ == kk::LightModel::RGB || this->model_ == kk::LightModel::RGBW) &&
          (caps & kk::CAP_RGB) != 0)
        call.set_rgb(v.r, v.g, v.b);
      if (this->model_ == kk::LightModel::RGBW && (caps & kk::CAP_WHITE) != 0)
        call.set_white(v.w);
    }
    call.set_transition_length(transition_ms);
    // Do not write every program tick to flash.
    call.set_save(false);
    call.perform();
  }
  this->applying_ = false;
  this->armed_ = true;
}

bool KLinkLightProgram::effect_active() const {
  for (auto *light : this->lights_) {
    if (light != nullptr && light->get_current_effect_index() != 0)
      return true;
  }
  return false;
}

std::string KLinkLightProgram::effect_name() const {
  for (auto *light : this->lights_) {
    if (light != nullptr && light->get_current_effect_index() != 0)
      return std::string(light->get_effect_name());
  }
  return "None";
}

// ---- Effects and find ---------------------------------------------------------------------------

std::vector<std::string> KLinkLightProgram::effect_names() const {
  std::vector<std::string> names;
  bool first = true;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    light::LightState *light = this->lights_[i];
    if (light == nullptr)
      continue;
    const kk::FxLight kind = kk::fx_light_for_caps(this->caps_.size() > i ? this->caps_[i] : kk::CAP_BRIGHTNESS);
    std::vector<std::string> mine;
    for (auto *effect : light->get_effects()) {
      if (effect == nullptr)
        continue;
      const std::string name = effect->get_name().str();
      if (name == "None")
        continue;
      const KLinkEffect *ours = KLinkEffect::from(effect);
      if (ours != nullptr && !kk::fx_offered(static_cast<kk::EffectKind>(ours->kind()), kind))
        continue;
      mine.push_back(name);
    }
    if (first) {
      names = mine;
      first = false;
    } else {
      std::vector<std::string> both;
      for (const auto &name : names)
        if (std::find(mine.begin(), mine.end(), name) != mine.end())
          both.push_back(name);
      names = both;
    }
  }
  return names;
}

bool KLinkLightProgram::start_effect(const std::string &name) {
  const std::vector<std::string> offered = this->effect_names();
  if (std::find(offered.begin(), offered.end(), name) == offered.end())
    return false;
  // Remember the lights as they are, so Manual gets them back when the effect ends.
  if (!this->effect_active()) {
    this->saved_.clear();
    for (auto *light : this->lights_)
      this->saved_.push_back(light != nullptr ? light->remote_values : light::LightColorValues());
    this->saved_valid_ = true;
  }
  const uint32_t seed = random_uint32() | 1u;
  this->quiet_until_ms_ = millis() + 1500;
  for (auto *light : this->lights_) {
    if (light == nullptr)
      continue;
    for (auto *effect : light->get_effects()) {
      KLinkEffect *ours = KLinkEffect::from(effect);
      if (ours != nullptr && effect->get_name().str() == name)
        ours->set_seed(seed);
    }
  }
  this->applying_ = true;
  for (auto *light : this->lights_) {
    if (light == nullptr)
      continue;
    auto call = light->make_call();
    call.set_state(true);  // an effect does not start on a light that is off
    call.set_effect(name);
    call.set_save(false);
    call.perform();
  }
  this->applying_ = false;
  return true;
}

void KLinkLightProgram::stop_effect() {
  this->quiet_until_ms_ = millis() + 1500;
  this->applying_ = true;
  for (auto *light : this->lights_) {
    if (light == nullptr || light->get_current_effect_index() == 0)
      continue;
    auto call = light->make_call();
    call.set_effect(static_cast<uint32_t>(0));
    call.set_save(false);
    call.perform();
  }
  this->applying_ = false;
}

int32_t KLinkLightProgram::effect_remaining_seconds() const {
  if (!this->effect_active())
    return 0;
  if (this->effect_end_ms_ == 0)
    return -1;  // until stopped
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->effect_end_ms_) >= 0)
    return 0;
  return static_cast<int32_t>((this->effect_end_ms_ - now + 999) / 1000);
}

std::string KLinkLightProgram::effect_label() const {
  return this->effect_active() ? this->effect_name() : std::string();
}

void KLinkLightProgram::set_effect_options(uint8_t speed, uint32_t duration_ms) {
  this->effect_speed_ = speed > 2 ? 2 : speed;
  // 0 means "until stopped"; otherwise 0.5 to 5 minutes, as on a bus group.
  if (duration_ms != 0)
    duration_ms = duration_ms < 30000 ? 30000 : (duration_ms > 300000 ? 300000 : duration_ms);
  this->effect_duration_ms_ = duration_ms;
}

void KLinkLightProgram::note_effect_started(uint32_t duration_ms) {
  this->effect_end_ms_ = duration_ms == 0 ? 0 : millis() + duration_ms;
}

void KLinkLightProgram::restore_(const std::vector<light::LightColorValues> &values) {
  this->applying_ = true;
  for (size_t i = 0; i < this->lights_.size() && i < values.size(); i++) {
    if (this->lights_[i] == nullptr)
      continue;
    auto call = this->lights_[i]->make_call();
    call.from_light_color_values(values[i]);
    call.set_transition_length(0);
    call.set_save(false);
    call.perform();
  }
  this->applying_ = false;
}

void KLinkLightProgram::loop() {
  // An effect ends in many ways (its duration, Stop, the light's own controls, Pulse and Flicker never tell us), so
  // the end is seen here as the change from running to not running.
  const bool running = this->effect_active();
  if (running)
    this->quiet_until_ms_ = millis() + 1500;
  if (this->was_effect_active_ && !running) {
    this->effect_end_ms_ = 0;
    // The schedule shows its value again on the next tick, and Manual gets the lights back as they were.
    this->have_sent_ = false;
    if (this->saved_valid_ && this->mode_.mode == kk::Mode::MANUAL)
      this->restore_(this->saved_);
    this->saved_valid_ = false;
  }
  this->was_effect_active_ = running;
  if (this->find_step_ >= 0 && static_cast<int32_t>(millis() - this->find_next_ms_) >= 0)
    this->apply_find_step_();
}

bool KLinkLightProgram::find() {
  if (this->find_step_ >= 0 || this->effect_active())
    return true;  // already flashing, or an effect is showing the lights
  this->find_saved_.clear();
  for (auto *light : this->lights_)
    this->find_saved_.push_back(light != nullptr ? light->remote_values : light::LightColorValues());
  this->find_step_ = 0;
  this->find_next_ms_ = millis();
  return true;
}

void KLinkLightProgram::apply_find_step_() {
  static const int8_t LAST_STEP = 6;  // three dark/bright pairs, then back
  if (this->find_step_ >= LAST_STEP) {
    this->find_step_ = -1;
    this->restore_(this->find_saved_);
    this->have_sent_ = false;  // the schedule's value shows again on the next tick
    return;
  }
  const bool bright = (this->find_step_ % 2) == 1;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    light::LightState *light = this->lights_[i];
    if (light == nullptr)
      continue;
    auto call = light->make_call();
    call.set_state(bright);
    if (bright) {
      call.set_brightness(1.0f);
      if (this->caps_.size() > i && (this->caps_[i] & kk::CAP_RGB) != 0)
        call.set_rgb(1.0f, 1.0f, 1.0f);
    }
    call.set_transition_length(0);
    call.set_publish(false);  // the lights blink; Home Assistant is not told about each step
    call.set_save(false);
    call.perform();
  }
  this->find_step_++;
  this->find_next_ms_ = millis() + 250;
}

uint32_t KLinkLightProgram::preference_key() const {
  // Keyed on the YAML id, not the numeric program id: reordering programs in YAML must not swap
  // the stored modes.
  return fnv1_hash("klink_prog_" + this->yaml_id_);
}

void KLinkLightProgram::light_changed_(light::LightState *light) {
  // Our own perform() publishes synchronously; the boot restore publishes before the first output.
  if (this->applying_ || !this->armed_)
    return;
  // An effect (or the moment after one) owns the lights: its frames are not a user's change.
  if (this->effect_active() || static_cast<int32_t>(millis() - this->quiet_until_ms_) < 0)
    return;
  // In MANUAL the user is in charge. PROGRAM and OFF already hand over on a change; a PREVIEW is
  // ended the same way (on_user_command_ records MANUAL as the restored mode).
  if (this->mode_.mode == kk::Mode::MANUAL)
    return;

  size_t index = this->lights_.size();
  for (size_t i = 0; i < this->lights_.size(); i++) {
    if (this->lights_[i] == light) {
      index = i;
      break;
    }
  }
  if (index >= this->lights_.size() || index >= this->expected_.size())
    return;

  const light::LightColorValues &rv = light->remote_values;
  kk::LightValues observed;
  observed.on = rv.is_on();
  observed.brightness = rv.get_brightness();
  observed.mireds = rv.get_color_temperature();
  observed.r = rv.get_red();
  observed.g = rv.get_green();
  observed.b = rv.get_blue();
  observed.w = rv.get_white();

  if (!kk::matches_expected(this->model_, this->expected_[index], observed, this->ranges_[index],
                            kk::MatchTolerance::default_tolerance()))
    this->on_user_command_();  // switches to Manual and fires the mode callbacks; nothing is re-applied
}

}  // namespace klink
}  // namespace esphome
