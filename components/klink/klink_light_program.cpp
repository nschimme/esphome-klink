#include "klink_light_program.h"

#include <utility>

#include "esphome/core/log.h"

#include "klink.h"  // KLinkHub

namespace esphome {
namespace klink {

static const char *const TAG = "klink.light_program";

namespace kk = kessil::klink;

void KLinkLightProgram::setup() {
  // Register with the hub first so the web API can already see the program.
  if (this->hub_ != nullptr)
    this->hub_->register_program(this);
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
    uint8_t caps = kk::CAP_BRIGHTNESS;  // every light gets brightness
    if (traits.supports_color_capability(light::ColorCapability::COLOR_TEMPERATURE))
      caps |= kk::CAP_CT;
    if (traits.supports_color_capability(light::ColorCapability::RGB))
      caps |= kk::CAP_RGB;
    if (traits.supports_color_capability(light::ColorCapability::WHITE))
      caps |= kk::CAP_WHITE;
    this->caps_[i] = caps;
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
  // An effect owns the light right now: hold the schedule back (the base still marks the value as
  // sent, and the periodic refresh resumes control once the effect stops).
  if (this->effect_active())
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

uint32_t KLinkLightProgram::preference_key() const {
  // Keyed on the YAML id, not the numeric program id: reordering programs in YAML must not swap
  // the stored modes.
  return fnv1_hash("klink_prog_" + this->yaml_id_);
}

void KLinkLightProgram::light_changed_(light::LightState *light) {
  // Our own perform() publishes synchronously; the boot restore publishes before the first output.
  if (this->applying_ || !this->armed_)
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
