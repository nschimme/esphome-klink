#pragma once
// A program that drives one or more ordinary ESPHome lights directly, with no K-Link bus at all.
//
// It is a ProgramHost like KLinkGroup (so the same slot texts, point editor, mode select, status
// sensor and web API attach to it with `group: <program id>`), but its output is a LightCall on each
// configured light instead of a bus frame. Because it never touches the bus it also controls its
// lights while the hub is in dry run ("Send to lights" off): that switch only gates bus writes.
//
// The program id lives in the 101-164 range (see klink_light_map.h), above the bus group ids, so
// both kinds share one id space in the web API.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/time/real_time_clock.h"

#include "klink_light_map.h"
#include "klink_program.h"
#include "klink_program_base.h"

namespace esphome {
namespace klink {

class KLinkHub;

class KLinkLightProgram : public ProgramBase, public Component {
 public:
  const char *kind() const override { return "light"; }
  uint8_t gid() const override { return this->program_id_; }
  std::string name() const override { return this->name_; }
  std::string model_name() const override { return kessil::klink::light_model_name(this->model_); }
  const kessil::klink::SlotLabels *slot_labels() const override {
    return kessil::klink::light_labels(this->model_);
  }
  // Programs have no manual extra channels: the program drives every channel of the lights.
  void set_extra(uint8_t, uint8_t) override {}
  uint8_t extra(uint8_t) const override { return 0; }
  void set_extra_silent(uint8_t, uint8_t) override {}

  void set_name(const std::string &name) { this->name_ = name; }
  // 101 + index, assigned by the Python schema so bus groups and programs share one id space.
  void set_program_id(uint8_t id) { this->program_id_ = id; }
  // The YAML id, used for the mode preference key so reordering programs keeps stored modes.
  void set_yaml_id(const std::string &id) { this->yaml_id_ = id; }
  void set_hub(KLinkHub *hub) { this->hub_ = hub; }
  void set_model(kessil::klink::LightModel model) { this->model_ = model; }
  void add_light(light::LightState *light) { this->lights_.push_back(light); }
  // How often an unchanged program value is re-asserted (0 disables the refresh).
  void set_update_ms(uint32_t ms) { this->set_refresh_interval(ms); }
  void set_transition_ms(uint32_t ms) { this->transition_ms_ = ms; }

  void setup() override;
  void dump_config() override;
  // After the lights (LightState is HARDWARE - 1), so their restored state is already published
  // and their traits are known.
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // The listener interface carries no light pointer, so each light gets its own tiny object. The
  // objects are owned by watchers_ and never moved after registration: remote_values_listeners_
  // in LightState keeps the raw pointers.
  struct Watcher : public light::LightRemoteValuesListener {
    KLinkLightProgram *owner{nullptr};
    light::LightState *light{nullptr};
    // The listener base has no virtual destructor; this one is virtual so unique_ptr<Watcher> can
    // delete the object without a -Wdelete-non-virtual-dtor warning.
    virtual ~Watcher() = default;
    void on_light_remote_values_update() override { this->owner->light_changed_(this->light); }
  };

  void output_(const uint8_t channels[5]) override;
  bool effect_active() const override;
  std::string effect_name() const override;
  uint32_t preference_key() const override;
  // A light changed by somebody else while we are not driving it: hand control to the user.
  void light_changed_(light::LightState *light);
  // Log what this light cannot do for the configured model (also repeated in dump_config).
  void log_missing_caps_(size_t index) const;

  std::string name_;
  std::string yaml_id_;
  KLinkHub *hub_{nullptr};
  uint8_t program_id_{0};
  kessil::klink::LightModel model_{kessil::klink::LightModel::BRIGHTNESS};
  uint32_t transition_ms_{0};
  std::vector<light::LightState *> lights_;
  // Parallel to lights_, filled in setup() from each light's traits.
  std::vector<uint8_t> caps_;
  std::vector<kessil::klink::CtRange> ranges_;
  std::vector<kessil::klink::LightValues> expected_;
  std::vector<std::unique_ptr<Watcher>> watchers_;
  // True while output_() calls perform(): the listeners fire synchronously and must not mistake
  // our own write for a user command.
  bool applying_{false};
  // False until the first output_() has completed: the lights' boot restore publishes long before
  // that and must never be treated as a manual change.
  bool armed_{false};
};

}  // namespace klink
}  // namespace esphome
