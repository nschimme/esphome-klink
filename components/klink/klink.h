#pragma once

#include <functional>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/button/button.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#ifdef USE_DATETIME_TIME
#include "esphome/components/datetime/time_entity.h"
#endif
#include "esphome/components/uart/uart.h"

#include "esphome/components/light/light_effect.h"

#include "klink_bus.h"
#include "klink_colors.h"
#include "klink_effects.h"
#include "klink_guard.h"
#include "klink_inventory.h"
#include "klink_mode.h"
#include "klink_fx_map.h"
#include "klink_names.h"
#include "klink_weather.h"
#include "klink_output.h"
#include "klink_program.h"
#include "klink_program_base.h"
#include "klink_schedule.h"

namespace esphome {
namespace klink {

class KLinkHub;
class KLinkGroup;
class KLinkSlotText;

// Adapts the UART to the host-testable bus.
class UartIo : public kessil::klink::Io {
 public:
  explicit UartIo(KLinkHub *hub) : hub_(hub) {}
  uint32_t now_ms() override;
  bool read_byte(uint8_t &out) override;
  void write(const std::vector<uint8_t> &frame) override;

 protected:
  KLinkHub *hub_;
};

class KLinkHub : public Component, public uart::UARTDevice, public LocationProvider {
 public:
  KLinkHub() : io_(this), bus_(io_), scanner_(bus_) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Dry run: when writes are disabled every frame is logged and nothing is transmitted.
  // Monitor: listen for frames from other bus users and log them.
  void set_monitor(bool monitor) { monitor_ = monitor; }
  // Returns true once this leader controls the chain. Enabling starts a claim at once; it returns
  // false while that claim is still announcing or waiting for another controller's writes to stop,
  // and the request stays pending in loop(). A follower always returns false.
  bool set_writes_enabled(bool enabled);
  bool writes_enabled() const { return writes_enabled_; }
  // The operator's intent (Send to lights), which stays true while a claim is pending or paused.
  bool want_writes() const { return want_writes_; }
  // For the web page: same as the Send to lights switch, and keeps the switch entity in step.
  // Returns false while the claim is pending (the request stays pending).
  bool set_writes_from_ui(bool enabled) {
    bool ok = this->set_writes_enabled(enabled);
    this->writes_cb_.call(this->writes_enabled_);
    return ok;
  }
  // Auto mode: ask for writes at boot and let the chain guard decide. A leader announces itself
  // and takes over when no other controller is writing; a controller that shows up later stops it.
  // Turning the Send to lights switch off ends auto mode until the switch is turned on again or the
  // device restarts.
  void set_auto_writes(bool on) { auto_writes_ = on; }
  bool auto_writes() const { return auto_writes_; }
  // One line for the dashboard: what the hub is doing with the bus right now.
  std::string state_text() const;
  // Leader or follower. A follower never transmits; it only listens to group writes and follows them.
  void set_role(kessil::klink::ChainRole role) { guard_.set_role(role); }
  kessil::klink::ChainRole role() const { return guard_.role(); }
  bool is_follower() const { return guard_.is_follower(); }
  bool controlling() const { return guard_.controlling(); }
  void set_beacon_interval_ms(uint32_t ms) { guard_.set_beacon_ms(ms); }
  void set_claim_confirm_ms(uint32_t ms) { guard_.set_claim_confirm_ms(ms); }
  void set_conflict_release_ms(uint32_t ms) { guard_.set_conflict_release_ms(ms); }
  void set_guard_quiet_ms(uint32_t ms) { guard_.set_foreign_holdoff_ms(ms); }
  // Spread the claim across equal devices; zero in tests.
  void set_claim_jitter_ms(uint32_t ms) { guard_.set_jitter_ms(ms); }
  kessil::klink::ChainState chain_state() const { return guard_.state(); }
  // Direct drive: no RS-485 transceiver. The C6's own D+/D- pads carry the differential pair: the
  // UART's TX signal goes to D+ and, inverted, to D-. Both pads are high-impedance except while a
  // frame is sent. The UART's tx_pin must be the D- pin (that is where the TX signal is read from).
  void set_direct_drive(int8_t dp_pin, int8_t dm_pin) {
    dp_pin_ = dp_pin;
    dm_pin_ = dm_pin;
  }
  uint32_t foreign_frames() const { return guard_.foreign_writes(); }
  bool other_controller_recent() const { return guard_.foreign_recent(millis(), guard_.foreign_holdoff_ms()); }
  bool conflict_latched() const { return guard_.conflict(); }
  uint32_t beacons_heard() const { return guard_.beacons_heard(); }
  uint32_t beacons_sent() const { return guard_.beacons_sent(); }
  uint32_t conflicts() const { return guard_.conflicts(); }
  // Commissioning gates assignment and switches itself off after a timeout.
  void set_commissioning(bool enabled);
  bool commissioning() const { return commissioning_; }

  void register_group(KLinkGroup *group) { groups_.push_back(group); }
  // Followers (a follower's K-Link light, or a native ESPHome light bound to a bus group) are told
  // about every group write this hub sends or hears. `channels` points at five bytes and is only
  // valid for the callback.
  void add_on_group_frame_callback(std::function<void(uint8_t, const uint8_t *)> &&cb) {
    group_frame_cbs_.push_back(std::move(cb));
  }
  // The last five-channel value seen for `gid`, and how old it is. False when nothing was seen.
  bool group_frame(uint8_t gid, uint8_t out[5], uint32_t &age_ms) const;
  void register_program(ProgramHost *program) {
    programs_.push_back(program);
    program->set_location_provider(this);
  }

  // Device-wide location for the sun and moon programs. Per coordinate: a value entered in the latitude
  // and longitude entities wins over the one imported from Home Assistant (zone.home), which wins over the
  // YAML default. NaN clears a source.
  struct Coordinate {
    double yaml{NAN};
    double ha{NAN};
    double entered{NAN};
    double restored{NAN};  // the last value Home Assistant gave, kept across reboots until it reports again
    double zone{NAN};      // the time zone's representative city: approximate, the last resort
  };
  void set_yaml_location(double latitude, double longitude) {
    location_[0].yaml = latitude;
    location_[1].yaml = longitude;
  }
  void set_timezone_location(double latitude, double longitude) {
    location_[0].zone = latitude;
    location_[1].zone = longitude;
  }
  void set_ha_coordinate(int index, float value);
  void set_entered_coordinate(int index, float value);
  bool location(double &latitude, double &longitude) const override;
  // Weather from Home Assistant: the condition name (sunny, cloudy, lightning ...). The sun program can follow
  // it (clouds dim it) and a thunderstorm can start the Storm effect.
  void set_weather_condition(const std::string &condition);
  void set_follow_weather(bool follow) { follow_weather_ = follow; }
  void set_storms_from_weather(bool storms) { storms_from_weather_ = storms; }
  void set_max_cloud_dimming(float percent) { max_cloud_dimming_ = percent < 0 ? 0 : (percent > 90 ? 90 : percent); }
  float max_cloud_dimming() const { return max_cloud_dimming_; }
  bool follow_weather() const { return follow_weather_; }
  bool storms_from_weather() const { return storms_from_weather_; }
  float weather_dimming() const override {
    return follow_weather_ ? max_cloud_dimming_ / 100.0f * kessil::klink::weather_cloud_factor(weather_condition_) : 0.0f;
  }
  const char *location_source() const override;
  void add_on_location_callback(std::function<void()> &&cb) { location_cb_.add(std::move(cb)); }
  void write_group(uint8_t gid, const uint8_t channels[5]);
  void start_scan();
  void identify(uint32_t kid);
  // True while the group's writes are paused so a lamp that is flashing to identify itself is not
  // overwritten by the group's once-a-second refresh.
  bool group_held(uint8_t gid);
  void hold_group(uint8_t gid, uint32_t ms);
  void identify_next();
  // One push of the Retry button: what turning Send to lights off and on again does today.
  void retry();
  bool stage_assignment(uint32_t kid, uint8_t gid);
  void confirm_assignment();
  // Commission, validate, stage and confirm an assignment in one call. The confirm itself is
  // asynchronous; this returns true once it has been sent.
  bool assign(uint32_t kid, uint8_t gid, std::string &error);
  // The checks assign() and set_name() would fail on, without changing anything: the web handler runs
  // them on the HTTP task to answer at once, then does the work on the main loop.
  bool check_assign(uint32_t kid, uint8_t gid, std::string &error) const;
  bool check_name(uint32_t kid, const std::string &name, std::string &error) const;

  size_t lamp_count() const { return inventory_.lamps().size(); }
  // Views for the web API.
  const std::vector<KLinkGroup *> &groups() const { return groups_; }
  const std::vector<ProgramHost *> &programs() const { return programs_; }
  const std::vector<kessil::klink::LampRecord> &lamps() const { return inventory_.lamps(); }
  const kessil::klink::NameTable &names() const { return names_; }
  // Set or clear a lamp nickname (empty clears); false and `error` filled on invalid input.
  bool set_name(uint32_t kid, const std::string &name, std::string &error);
  // Group nicknames, by gid: what the page calls a group. The Home Assistant entity keeps its YAML name.
  const kessil::klink::NameTable &group_names() const { return group_names_; }
  bool check_group_name(uint32_t gid, const std::string &name, std::string &error) const;
  bool set_group_name(uint32_t gid, const std::string &name, std::string &error);
  // Identify every known lamp in `gid`. Returns false when the group has no lamps.
  bool identify_gid(uint8_t gid);
  uint32_t bus_timeouts() const { return bus_.timeouts(); }
  uint32_t frames_seen() const { return bus_.monitor().frames(); }
  uint32_t bad_frames() const { return bus_.monitor().bad_frames(); }
  uint32_t shadow_matches() const { return shadow_matches_; }
  uint32_t shadow_mismatches() const { return shadow_mismatches_; }

  void add_on_lamp_found_callback(std::function<void(uint32_t, uint8_t, uint8_t)> &&cb) {
    this->lamp_found_cb_.add(std::move(cb));
  }
  void add_on_scan_done_callback(std::function<void(bool, uint32_t)> &&cb) { this->scan_done_cb_.add(std::move(cb)); }
  void add_on_writes_callback(std::function<void(bool)> &&cb) { this->writes_cb_.add(std::move(cb)); }
  void add_on_commissioning_callback(std::function<void(bool)> &&cb) { this->commissioning_cb_.add(std::move(cb)); }

  // Used by UartIo.
  bool rx_byte(uint8_t &out) {
    if (this->available() > 0 && this->read_byte(&out)) {
      this->rx_bytes_++;
      return true;
    }
    return false;
  }
  void tx_frame(const std::vector<uint8_t> &frame);

 protected:
  struct StoredInventory {
    uint8_t length;
    uint8_t data[4 + 6 * kessil::klink::MAX_LAMPS];
  };
  struct StoredNames {
    uint16_t length;
    uint8_t data[kessil::klink::NAME_BLOB_MAX];
  };

  void direct_setup_();
  void direct_drive_(bool on);
  void observe_(const std::vector<uint8_t> &frame);
  void note_group_frame_(uint8_t gid, const uint8_t channels[5]);
  void save_inventory_();
  void save_names_();
  void save_group_names_();
  void read_next_identity_();

  UartIo io_;
  kessil::klink::Bus bus_;
  kessil::klink::Scanner scanner_;
  kessil::klink::Inventory inventory_;
  ESPPreferenceObject pref_;
  kessil::klink::NameTable names_;
  ESPPreferenceObject names_pref_;
  kessil::klink::NameTable group_names_;
  ESPPreferenceObject group_names_pref_;
  std::vector<KLinkGroup *> groups_;
  std::vector<ProgramHost *> programs_;
  Coordinate location_[2];  // latitude, longitude
  CallbackManager<void()> location_cb_;
  ESPPreferenceObject location_pref_;
  std::string weather_condition_;
  bool have_condition_{false};
  bool follow_weather_{false};
  bool storms_from_weather_{false};
  float max_cloud_dimming_{50.0f};
  uint32_t last_storm_ms_{0};
  bool storm_started_once_{false};
  void save_location_();

  bool monitor_{false};
  int8_t dp_pin_{-1};
  int8_t dm_pin_{-1};
  bool direct_ok_{false};
  uint32_t rx_bytes_{0};
  uint32_t tx_bytes_{0};
  uint32_t last_byte_log_ms_{0};
  uint32_t last_byte_log_rx_{0};
  uint32_t shadow_matches_{0};
  uint32_t shadow_mismatches_{0};
  bool writes_enabled_{false};
  bool want_writes_{false};
  bool auto_writes_{false};
  uint32_t last_guard_log_ms_{0};
  kessil::klink::ChainGuard guard_;
  struct CachedGroup {
    bool valid{false};
    uint8_t channels[5]{0, 0, 0, 0, 0};
    uint32_t seen_ms{0};
  };
  static const uint8_t GROUP_CACHE_SIZE = kessil::klink::MAX_GID + 1;
  CachedGroup group_cache_[GROUP_CACHE_SIZE];
  std::vector<std::function<void(uint8_t, const uint8_t *)>> group_frame_cbs_;
  bool commissioning_{false};
  uint32_t commissioning_deadline_{0};

  bool scanning_{false};
  bool scan_complete_{false};
  bool auto_scanned_{false};
  std::string scan_note_;
  std::vector<uint32_t> scan_kids_;
  size_t scan_pos_{0};
  uint32_t scan_found_{0};

  bool staged_{false};
  uint32_t staged_kid_{0};
  uint8_t staged_gid_{0};
  size_t identify_pos_{0};
  struct Hold {
    uint8_t gid;
    uint32_t until_ms;
  };
  std::vector<Hold> holds_;

  CallbackManager<void(uint32_t, uint8_t, uint8_t)> lamp_found_cb_;
  CallbackManager<void(bool, uint32_t)> scan_done_cb_;
  CallbackManager<void(bool)> commissioning_cb_;
  CallbackManager<void(bool)> writes_cb_;
};

// One K-Link group: the manual-override light and the schedule that drives it.
// What a group's lamps are. The Tuna Sun is tested on lamps; the Tuna Blue and the Refugium follow Kessil's
// published channel layouts and are untested.
enum class GroupModel : uint8_t {
  TUNA_SUN,   // UI1 intensity, UI2 color temperature, UI4 red, UI5 amber (UI3 unused)
  TUNA_BLUE,  // UI1 intensity, UI2 blue to white, UI3 violet, UI4 red, UI5 green; the AP9X is a Tuna Blue
  REFUGIUM,   // UI1 intensity, UI2 blue to red (0-240), UI4 white, UI5 green (UI3 unused)
};

// The color model behind a K-Link group's CLR/overlay channels.
const kessil::klink::ColorSpec *group_color_spec(GroupModel model);

// The color models of the Tuna lights (tables generated at compile time from klink_colors.py).
extern const kessil::klink::ColorSpec COLOR_SUN;
extern const kessil::klink::ColorSpec COLOR_BLUE;
extern const kessil::klink::ColorSpec COLOR_REFUGIUM;

// What a group last put on its light entity (the schedule's value or a snapped color), so the light's
// own write_state() call for that value is recognized as ours and not taken for a user command.
struct LightShown {
  bool have{false};
  bool on{false};
  light::ColorMode mode{light::ColorMode::UNKNOWN};
  float brightness{0.0f};
  float mireds{0.0f};
  float rgb[3]{0.0f, 0.0f, 0.0f};  // the color direction, largest component 1
};

class KLinkGroup : public ProgramBase, public light::LightOutput, public Component {
 public:
  // Groups register when they are constructed: a saved effect is restored while the light sets up, before this
  // group's own setup() runs, and must already find its group.
  KLinkGroup();
  // The group behind a light's output, or nullptr when the light is not one of ours (an ordinary ESPHome light).
  static KLinkGroup *from_output(const light::LightOutput *output);
  const char *kind() const override { return "bus"; }
  void set_model(GroupModel model) { model_ = model; }
  GroupModel model() const { return model_; }
  // Only the Tuna Sun's UI2 is a color temperature; elsewhere the light is brightness-only and UI2
  // is a plain value set from a number entity.
  bool has_color_temperature() const { return model_ == GroupModel::TUNA_SUN; }
  // Names used for this model's channels in the schedule slot text (Kessil's own labels).
  const kessil::klink::SlotLabels *slot_labels() const override {
    switch (model_) {
      case GroupModel::TUNA_SUN:
        return &kessil::klink::LABELS_TUNA_SUN;
      case GroupModel::TUNA_BLUE:
        return &kessil::klink::LABELS_TUNA_BLUE;
      case GroupModel::REFUGIUM:
        return &kessil::klink::LABELS_REFUGIUM;
    }
    return &kessil::klink::LABELS_TUNA_SUN;
  }
  const uint8_t *channel_max() const override {
    return this->model_ == GroupModel::REFUGIUM ? kessil::klink::CHANNEL_MAX_REFUGIUM : kessil::klink::CHANNEL_MAX;
  }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_gid(uint8_t gid) { gid_ = gid; }
  void set_manual_color(uint8_t color) { manual_color_ = color; }
  // Mired range the light advertises (cold end, warm end) and which end UI2 = 0 is.
  void set_color_range(float cold_mireds, float warm_mireds, bool invert) {
    cold_mireds_ = cold_mireds;
    warm_mireds_ = warm_mireds;
    invert_color_ = invert;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  // Web API: the light's name, or "Group <gid>" before the light has reported one.
  std::string name() const override {
    if (this->state_ != nullptr) {
      std::string n = this->state_->get_name().str();
      if (!n.empty())
        return n;
    }
    return "Group " + std::to_string(this->gid_);
  }
  // Web API: the model key used in YAML ("tuna_sun", "tuna_blue", "refugium"). An AP9X uses the
  // Tuna Blue model and reports "tuna_blue".
  std::string model_name() const override {
    switch (this->model_) {
      case GroupModel::TUNA_SUN:
        return "tuna_sun";
      case GroupModel::TUNA_BLUE:
        return "tuna_blue";
      case GroupModel::REFUGIUM:
        break;
    }
    return "refugium";
  }
  uint8_t gid() const override { return gid_; }
  // UI2 (0-255, models without a color temperature) and UI3-UI5 (percent) from the number entities.
  // A change takes manual control, like the light.
  void set_extra(uint8_t channel, uint8_t value) override;
  uint8_t extra(uint8_t channel) const override { return channel == 2 ? manual_color_ : extras_[channel - 3]; }
  void add_on_extras_callback(std::function<void()> &&cb) override { extras_cb_.add(std::move(cb)); }
  void set_extra_silent(uint8_t channel, uint8_t value) override {
    if (channel == 2)
      manual_color_ = value > channel_max()[1] ? channel_max()[1] : value;
    else if (channel >= 3 && channel <= 5)
      extras_[channel - 3] = value > 100 ? 100 : value;
  }
  // The brightness (UI1) and color (UI2) the light's current state asks for.
  void manual_levels(light::LightState *state, uint8_t &level, uint8_t &color);
  // One frame of an effect. Effects are a temporary overlay on the schedule: this does not take
  // manual control, and while one runs ProgramBase::update() stays out of the way.
  void send_effect_frame(const uint8_t channels[5]) { this->send_(channels); }
  // Record how long the running effect lasts (0 = until stopped), for the status line.
  // `smooth`: the first frame fades in (Fade time), the others follow; Storm's flashes stay hard.
  void note_effect_started(uint32_t duration_ms, bool smooth) {
    this->klink_effect_running_ = true;
    this->effect_smooth_ = smooth;
    this->effect_fresh_ = smooth;
    this->effect_end_ms_ = duration_ms == 0 ? 0 : millis() + duration_ms;
  }
  // Output options (brightness curve, transition time and style), set by the entities.
  // The sun and moon programs are written for the Tuna lamps' channels; a Refugium runs My points.
  bool has_sources() const override { return model_ != GroupModel::REFUGIUM; }
  bool can_moon() const override { return model_ == GroupModel::TUNA_BLUE; }
  bool has_output_options() const override { return true; }
  OutputSettings output_settings() const override { return out_settings_; }
  void set_output_settings(const OutputSettings &settings) override;
  void add_on_output_settings_callback(std::function<void()> &&cb) override { out_cb_.add(std::move(cb)); }
  // Called by an effect's stop(): resume whatever the group was doing before the effect began.
  void effect_ended_();

  // ProgramHost effect interface (called from the main loop, through defer).
  std::vector<std::string> effect_names() const override;
  bool start_effect(const std::string &name) override;
  void stop_effect() override;
  int32_t effect_remaining_seconds() const override;
  std::string effect_label() const override;
  uint8_t effect_speed() const override { return this->effect_speed_; }
  uint32_t effect_duration_ms() const override { return this->effect_duration_ms_; }
  void set_effect_options(uint8_t speed, uint32_t duration_ms) override;

 protected:
  void output_(const uint8_t channels[5]) override;
  bool repeat_held_value_() const override { return true; }
  bool output_busy_() const override { return stage_busy_; }
  kessil::klink::DynModel dyn_model_() const override {
    switch (model_) {
      case GroupModel::TUNA_SUN:
        return kessil::klink::DYN_SUN;
      case GroupModel::TUNA_BLUE:
        return kessil::klink::DYN_BLUE;
      case GroupModel::REFUGIUM:
        break;
    }
    return kessil::klink::DYN_GENERIC;
  }
  bool effect_active() const override { return this->state_ != nullptr && this->state_->get_current_effect_index() != 0; }
  std::string effect_name() const override {
    if (this->state_ == nullptr)
      return "None";
    return std::string(this->state_->get_effect_name());
  }
  uint32_t preference_key() const override {
    return fnv1_hash("klink_mode_" + std::to_string(this->gid_));
  }

  KLinkHub *hub_{nullptr};
  uint8_t gid_{0};
  uint8_t manual_color_{127};
  float cold_mireds_{153.0f};
  float warm_mireds_{370.0f};
  bool invert_color_{false};
  uint8_t extras_[3]{0, 0, 0};
  GroupModel model_{GroupModel::TUNA_SUN};
  light::LightState *state_{nullptr};  // the light, remembered from write_state
  bool ignored_initial_{false};
  // Runtime effect settings, persisted with preferences.
  uint8_t effect_speed_{0};           // 0 fast, 1 moderate, 2 slow
  uint32_t effect_duration_ms_{120000};  // 0 = until stopped
  ESPPreferenceObject effect_speed_pref_;
  ESPPreferenceObject effect_duration_pref_;
  // millis() when the running effect ends, 0 while it runs until stopped.
  uint32_t effect_end_ms_{0};
  // The light call that stops an effect repeats the values the light already has. In PROGRAM and
  // Off that must not look like a user command, so the next write_state() compares against what
  // was last shown on the light.
  bool effect_ended_pending_{false};
  LightShown ended_shown_;
  // Program/Off output (and a snapped color) shown on the light entity, so Home Assistant sees the
  // group as on at the level and color it really has (an off-looking light hides its controls and
  // effects in Home Assistant). On the Tuna lights an RGB color from Home Assistant is snapped to the
  // nearest color the lamp can make (klink_colors.h) and the snapped color is shown here.
  void publish_to_light_(const uint8_t channels[5]);
  // A group write heard on the bus. A follower follows it; a leader that is not controlling (for
  // example while yielding to another controller) shows it too.
  void on_bus_frame_(const uint8_t channels[5]);
  void show_on_light_(bool on, float brightness, light::ColorMode mode, float mireds, const float rgb[3]);
  bool shown_matches_(const LightShown &shown, const light::LightColorValues &values) const;
  const kessil::klink::ColorSpec *color_spec_() const;
  LightShown shown_;
  CallbackManager<void()> extras_cb_;
  // The brightness (0-1, linear light) the light asks for, after the chosen curve.
  float light_brightness_(light::LightState *state) const;
  // Output stage: fades between the target and the bus. With the defaults it
  // is a pass-through. Frames of effects bypass it.
  void write_bus_(const uint8_t channels[5]);
  void tick_stage_();
  void apply_output_settings_();
  void apply_transition_();
  OutputSettings out_settings_;
  ESPPreferenceObject out_pref_;
  CallbackManager<void()> out_cb_;
  kessil::klink::OutputStage stage_;
  float stage_target_[5]{0, 0, 0, 0, 0};
  bool stage_have_target_{false};
  bool stage_busy_{false};
  bool soft_start_pending_{false};  // the first output after every start fades in from dark
  uint32_t stage_ms_{0};
  uint32_t stage_due_ms_{0};  // when the next fade frame is due (a fixed 40 ms cadence)
  // Standard ESPHome effects and transitions (pulse, flicker, light.turn_on with a transition ...) change
  // the light's values many times a second; they are shown on the lamps at no more than ten frames a
  // second, the last value always getting through. Our own effects stream their own frames instead.
  void send_throttled_(const uint8_t channels[5]);
  void forward_values_(light::LightState *state);
  void channels_from_light_(light::LightState *state, uint8_t channels[5], bool remember);
  bool effect_smooth_{false};  // the running effect fades in
  bool effect_fresh_{false};   // its first frame is still to come
  bool klink_effect_running_{false};  // one of the KLinkEffect effects runs
  bool fwd_effect_active_{false};     // a standard ESPHome effect is being forwarded
  LightShown fwd_shown_;              // the last values forwarded, to tell the ending call from a user command
  uint8_t pending_[5]{0, 0, 0, 0, 0};
  uint32_t last_throttled_ms_{0};
};

// A 0-100% control for UI3, UI4 or UI5 of one group.
class KLinkChannelNumber : public number::Number, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void set_channel(uint8_t channel) { channel_ = channel; }
  // Point editor mode: shows and edits channel `index` (0-4 = UI1-UI5) of the selected program point.
  void set_point_field(uint8_t index) {
    point_mode_ = true;
    channel_ = index + 1;
  }
  void setup() override;
  void dump_config() override;

 protected:
  void control(float value) override;
  void refresh_();

  ProgramHost *group_{nullptr};
  uint8_t channel_{4};
  bool point_mode_{false};
  ESPPreferenceObject pref_;
};

// The group's operating mode: Program, Manual or Off (option index = kessil::klink::Mode).
class KLinkModeSelect : public select::Select, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  // After the group, so the mode it restored from flash is what the select first shows.
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(size_t index) override { this->group_->set_mode(static_cast<kessil::klink::Mode>(index)); }
  ProgramHost *group_{nullptr};
};

// Speed for the running/next effect: 0 Fast, 1 Moderate, 2 Slow.
class KLinkEffectSpeedSelect : public select::Select, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  // After the group, so the values it restored from flash are what the entity first shows.
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(size_t index) override {
    this->group_->set_effect_options(static_cast<uint8_t>(index > 2 ? 2 : index), this->group_->effect_duration_ms());
  }
  ProgramHost *group_{nullptr};
};

// How long an effect runs, in minutes (0 = until stopped).
class KLinkEffectDurationNumber : public number::Number, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(float value) override;
  ProgramHost *group_{nullptr};
};

// Where the schedule comes from: My points, the sun at the device's location, or the moon.
class KLinkSourceSelect : public select::Select, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(size_t index) override { this->group_->set_source(static_cast<ProgramSource>(index > 2 ? 2 : index)); }
  ProgramHost *group_{nullptr};
};

// How Home Assistant's brightness maps to the lamp: a straight line or CIE 1931.
class KLinkCurveSelect : public select::Select, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  // After the group, so the options it restored from flash are what the entity first shows.
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(size_t index) override;
  ProgramHost *group_{nullptr};
};

// How long the lamps take to change, in seconds, in seconds (0 = immediate), 0 to 10 in tenth steps.
class KLinkFadeTimeNumber : public number::Number, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(float value) override;
  ProgramHost *group_{nullptr};
};

// Which program point the editor entities show and edit.
class KLinkPointSelect : public select::Select, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(size_t index) override { this->group_->select_point(index); }
  ProgramHost *group_{nullptr};
};

#ifdef USE_DATETIME_TIME
// The selected program point's time of day.
class KLinkPointTime : public datetime::TimeEntity, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(const datetime::TimeCall &call) override;
  void refresh_();
  ProgramHost *group_{nullptr};
};
#endif

// The lighting effects, built on 25 group writes a second (see klink_effects.h). Standard ESPHome
// effects (pulse, flicker, ...) need no class of ours: the group forwards their values to the lamps.
enum class FxKind : uint8_t { STORM, CLOUD, RAINBOW, AURORA, MOON_RED, MOON_BLUE };

class KLinkLightProgram;

// On a K-Link group the effect streams group writes; on an ordinary ESPHome light it applies the same frames
// as light calls (see klink_fx_map.h), without publishing each one so Home Assistant is not flooded.
class KLinkEffect : public light::LightEffect {
 public:
  explicit KLinkEffect(const char *name) : light::LightEffect(name) { instances().push_back(this); }
  static std::vector<KLinkEffect *> &instances();
  // nullptr when `effect` is not one of ours.
  static KLinkEffect *from(const light::LightEffect *effect);
  FxKind kind() const { return kind_; }
  // The same seed on every light of a program keeps their random flashes in step.
  void set_seed(uint32_t seed) { seed_ = seed; }
  void set_kind(FxKind kind) { kind_ = kind; }
  void set_speed(uint8_t variant) { variant_ = variant; }  // 0 fast, 1 moderate, 2 slow
  void set_duration(uint32_t ms) { duration_ms_ = ms; }    // 0 = until stopped

  void start() override;
  void apply() override;
  // Called when the effect ends for any reason: lets the group resume what it was doing.
  void stop() override;

 protected:
  void finish_();
  uint32_t next_random_();
  void apply_native_(uint32_t elapsed);

  uint32_t seed_{0};
  bool native_{false};       // an ordinary ESPHome light, not a K-Link group
  bool unsupported_{false};  // this kind of light would show nothing for this effect
  kessil::klink::FxLight fx_light_{kessil::klink::FxLight::BRIGHTNESS};
  kessil::klink::CtRange range_{0.0f, 0.0f};
  KLinkLightProgram *program_{nullptr};
  FxKind kind_{FxKind::STORM};
  uint8_t variant_{0};
  uint32_t duration_ms_{120000};
  uint32_t start_ms_{0};
  uint32_t last_tick_ms_{0};
  kessil::klink::EffectEngine engine_{[this]() { return this->next_random_(); }};
};

// One schedule slot, edited as text: "HH:MM u1 u2 u3 u4 u5" or "off".
class KLinkSlotText : public text::Text, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void set_index(uint8_t index) { index_ = index; }
  void set_initial_value(const std::string &value) { initial_ = value; }
  // The shipped value, for the page's "Default" preset (independent of what was stored since).
  const std::string &initial_value() const { return initial_; }

  void setup() override;
  void dump_config() override;
  // Show a point edited elsewhere (the point editor) and save it once edits have stopped.
  void store(const kessil::klink::SchedulePoint &point);

 protected:
  struct Stored {
    char text[64];
  };
  void control(const std::string &value) override;
  void apply_(const std::string &value, bool save);

  ProgramHost *group_{nullptr};
  uint8_t index_{0};
  std::string initial_{"off"};
  ESPPreferenceObject pref_;
};

// Numbers for a program's acclimation ramp (start share, days) and a bus group's soft start (seconds).
enum class TankNumberKind : uint8_t { ACC_START, ACC_DAYS, SOFT_START };

class KLinkTankNumber : public number::Number, public Component {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void set_kind(TankNumberKind kind) { kind_ = kind; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(float value) override;
  void refresh_();
  ProgramHost *group_{nullptr};
  TankNumberKind kind_{TankNumberKind::ACC_START};
};

// The most the weather may dim the sun, in percent, when it is fully overcast.
class KLinkCloudDimmingNumber : public number::Number, public Component {
 public:
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(float value) override;
  KLinkHub *hub_{nullptr};
  ESPPreferenceObject pref_;
};

// The hub-wide latitude or longitude the user enters; it overrides the Home Assistant and YAML values.
class KLinkLocationNumber : public number::Number, public Component {
 public:
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_index(int index) { index_ = index; }  // 0 latitude, 1 longitude
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(float value) override;
  KLinkHub *hub_{nullptr};
  int index_{0};
  ESPPreferenceObject pref_;
};

enum class SwitchKind : uint8_t { WRITES, COMMISSIONING, POINT_ENABLED, FOLLOW_WEATHER, STORMS_FROM_WEATHER, ACCLIMATION };

class KLinkSwitch : public switch_::Switch, public Component {
 public:
  void set_kind(SwitchKind kind) { kind_ = kind; }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_group(ProgramHost *group) { group_ = group; }

  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
  void apply_(bool state);

  SwitchKind kind_{SwitchKind::WRITES};
  KLinkHub *hub_{nullptr};
  ProgramHost *group_{nullptr};
};

enum class ButtonKind : uint8_t { SCAN, IDENTIFY_NEXT, RETRY };

class KLinkButton : public button::Button, public Component {
 public:
  void set_kind(ButtonKind kind) { kind_ = kind; }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void dump_config() override;

 protected:
  void press_action() override;

  ButtonKind kind_{ButtonKind::SCAN};
  KLinkHub *hub_{nullptr};
};

enum class SensorKind : uint8_t {
  BUS_TIMEOUTS,
  LAMPS,
  FRAMES,
  BAD_FRAMES,
  SHADOW_MATCHES,
  SHADOW_MISMATCHES,
  FOREIGN_FRAMES,
  OTHER_CONTROLLER,
  CONFLICT_LATCHED
};

class KLinkSensor : public sensor::Sensor, public PollingComponent {
 public:
  void set_kind(SensorKind kind) { kind_ = kind; }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void update() override;
  void dump_config() override;

 protected:
  SensorKind kind_{SensorKind::BUS_TIMEOUTS};
  KLinkHub *hub_{nullptr};
};

// Dashboard line describing the bus state (see KLinkHub::state_text).
class KLinkStateSensor : public text_sensor::TextSensor, public PollingComponent {
 public:
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void update() override;
  void dump_config() override;

 protected:
  KLinkHub *hub_{nullptr};
};

// One line describing what this group is doing right now (see KLinkGroup::status_text).
class KLinkGroupStatus : public text_sensor::TextSensor, public PollingComponent {
 public:
  void set_group(ProgramHost *group) { group_ = group; }
  void update() override;
  void dump_config() override;

 protected:
  ProgramHost *group_{nullptr};
  std::string last_;
  bool have_last_{false};
};

// Automation hooks.
class LampFoundTrigger : public Trigger<uint32_t, uint8_t, uint8_t> {
 public:
  explicit LampFoundTrigger(KLinkHub *hub) {
    hub->add_on_lamp_found_callback([this](uint32_t kid, uint8_t gid, uint8_t type) { this->trigger(kid, gid, type); });
  }
};

class ScanDoneTrigger : public Trigger<bool, uint32_t> {
 public:
  explicit ScanDoneTrigger(KLinkHub *hub) {
    hub->add_on_scan_done_callback([this](bool complete, uint32_t found) { this->trigger(complete, found); });
  }
};

template<typename... Ts> class ScanAction : public Action<Ts...>, public Parented<KLinkHub> {
 public:
  using Parented<KLinkHub>::Parented;
  void play(const Ts &...x) override { this->parent_->start_scan(); }
};

template<typename... Ts> class IdentifyAction : public Action<Ts...>, public Parented<KLinkHub> {
 public:
  using Parented<KLinkHub>::Parented;
  TEMPLATABLE_VALUE(uint32_t, kid)
  void play(const Ts &...x) override { this->parent_->identify(this->kid_.value(x...)); }
};

template<typename... Ts> class StageAssignmentAction : public Action<Ts...>, public Parented<KLinkHub> {
 public:
  using Parented<KLinkHub>::Parented;
  TEMPLATABLE_VALUE(uint32_t, kid)
  TEMPLATABLE_VALUE(uint8_t, gid)
  void play(const Ts &...x) override { this->parent_->stage_assignment(this->kid_.value(x...), this->gid_.value(x...)); }
};

template<typename... Ts> class ConfirmAssignmentAction : public Action<Ts...>, public Parented<KLinkHub> {
 public:
  using Parented<KLinkHub>::Parented;
  void play(const Ts &...x) override { this->parent_->confirm_assignment(); }
};

// A follower binds a K-Link group to one or more ordinary ESPHome lights. It never writes to the
// bus: it listens for the group's command-1F frames (from the leader, or from another controller
// while a leader is yielding) and shows them with the class the lights actually support. This is
// how a follower exposes a native light as RGB, color temperature or brightness-only without the
// user having to pick a model by hand.
class KLinkLightFollower : public Component {
 public:
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_gid(uint8_t gid) { gid_ = gid; }
  void set_model(GroupModel model) { model_ = model; }
  void add_light(light::LightState *light) { lights_.push_back(light); }
  void set_transition_ms(uint32_t ms) { transition_ms_ = ms; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void apply_(const uint8_t channels[5]);
  KLinkHub *hub_{nullptr};
  uint8_t gid_{0};
  GroupModel model_{GroupModel::TUNA_SUN};
  std::vector<light::LightState *> lights_;
  std::vector<uint8_t> caps_;
  std::vector<kessil::klink::CtRange> ranges_;
  kessil::klink::LightModel output_model_{kessil::klink::LightModel::BRIGHTNESS};
  uint32_t transition_ms_{0};
};

}  // namespace klink
}  // namespace esphome
