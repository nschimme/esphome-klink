#pragma once

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
#include "klink_guard.h"
#include "klink_inventory.h"
#include "klink_mode.h"
#include "klink_names.h"
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

class KLinkHub : public Component, public uart::UARTDevice {
 public:
  KLinkHub() : io_(this), bus_(io_), scanner_(bus_) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Dry run: when writes are disabled every frame is logged and nothing is transmitted.
  // Monitor: listen for frames from other bus users and log them.
  void set_monitor(bool monitor) { monitor_ = monitor; }
  // Returns false when the bus guard refuses (another controller heard, or still listening). A
  // refused enable request stays pending and is retried from loop() until the bus has been quiet.
  bool set_writes_enabled(bool enabled);
  bool writes_enabled() const { return writes_enabled_; }
  // For the web page: same as the Send to lights switch, and keeps the switch entity in step. Returns
  // false when enabling is still waiting for a quiet bus (the request stays pending).
  bool set_writes_from_ui(bool enabled) {
    bool ok = this->set_writes_enabled(enabled);
    this->writes_cb_.call(this->writes_enabled_);
    return ok;
  }
  // Auto mode: ask for writes at boot and let the bus guard decide. While another controller (for
  // example the WiFi dongle) is heard the hub only shadows it; once the bus has been quiet it takes over by itself,
  // and a controller that shows up later stops it. Turning the Send to lights switch off ends auto
  // mode until the switch is turned on again or the device restarts.
  void set_auto_writes(bool on) { auto_writes_ = on; }
  bool auto_writes() const { return auto_writes_; }
  // One line for the dashboard: what the hub is doing with the bus right now.
  std::string state_text() const;
  void set_guard_quiet_ms(uint32_t ms) { guard_.set_quiet_ms(ms); }
  // Direct drive: no RS-485 transceiver. The C6's own D+/D- pads carry the differential pair: the
  // UART's TX signal goes to D+ and, inverted, to D-. Both pads are high-impedance except while a
  // frame is sent. The UART's tx_pin must be the D- pin (that is where the TX signal is read from).
  void set_direct_drive(int8_t dp_pin, int8_t dm_pin) {
    dp_pin_ = dp_pin;
    dm_pin_ = dm_pin;
  }
  uint32_t foreign_frames() const { return guard_.foreign_frames(); }
  bool other_controller_recent() const { return guard_.other_controller_recent(millis()); }
  bool conflict_latched() const { return guard_.conflict_latched(); }
  // Commissioning gates assignment and switches itself off after a timeout.
  void set_commissioning(bool enabled);
  bool commissioning() const { return commissioning_; }

  void register_group(KLinkGroup *group) { groups_.push_back(group); }
  void write_group(uint8_t gid, const uint8_t channels[5]);
  void start_scan();
  void identify(uint32_t kid);
  void identify_next();
  // One push of the Retry button: what turning Send to lights off and on again does today.
  void retry();
  bool stage_assignment(uint32_t kid, uint8_t gid);
  void confirm_assignment();
  // Commission, validate, stage and confirm an assignment in one call. The confirm itself is
  // asynchronous; this returns true once it has been sent.
  bool assign(uint32_t kid, uint8_t gid, std::string &error);

  size_t lamp_count() const { return inventory_.lamps().size(); }
  // Views for the web API.
  const std::vector<KLinkGroup *> &groups() const { return groups_; }
  const std::vector<kessil::klink::LampRecord> &lamps() const { return inventory_.lamps(); }
  const kessil::klink::NameTable &names() const { return names_; }
  // Set or clear a lamp nickname (empty clears); false and `error` filled on invalid input.
  bool set_name(uint32_t kid, const std::string &name, std::string &error);
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
  void save_inventory_();
  void save_names_();
  void read_next_identity_();

  UartIo io_;
  kessil::klink::Bus bus_;
  kessil::klink::Scanner scanner_;
  kessil::klink::Inventory inventory_;
  ESPPreferenceObject pref_;
  kessil::klink::NameTable names_;
  ESPPreferenceObject names_pref_;
  std::vector<KLinkGroup *> groups_;

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
  kessil::klink::BusGuard guard_;
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

  CallbackManager<void(uint32_t, uint8_t, uint8_t)> lamp_found_cb_;
  CallbackManager<void(bool, uint32_t)> scan_done_cb_;
  CallbackManager<void(bool)> commissioning_cb_;
  CallbackManager<void(bool)> writes_cb_;
};

// One K-Link group: the manual-override light and the schedule that drives it.
// What a group's lamps are. The Tuna Sun is tested on lamps; the others are untested or raw.
enum class GroupModel : uint8_t {
  TUNA_SUN,   // UI1 intensity, UI2 color temperature, UI4 red, UI5 amber (UI3 unused)
  TUNA_BLUE,  // provisional: channel meanings not established, so every channel is a raw value
  GENERIC,    // any other model: raw channels
};

class KLinkGroup : public light::LightOutput, public Component {
 public:
  void set_model(GroupModel model) { model_ = model; }
  GroupModel model() const { return model_; }
  // Only the Tuna Sun's UI2 is a color temperature; elsewhere the light is brightness-only and UI2
  // is a plain value set from a number entity.
  bool has_color_temperature() const { return model_ == GroupModel::TUNA_SUN; }
  // Names used for this model's channels in the schedule slot text (Kessil's own labels).
  const kessil::klink::SlotLabels *slot_labels() const {
    switch (model_) {
      case GroupModel::TUNA_SUN:
        return &kessil::klink::LABELS_TUNA_SUN;
      case GroupModel::TUNA_BLUE:
        return &kessil::klink::LABELS_TUNA_BLUE;
      default:
        return &kessil::klink::LABELS_GENERIC;
    }
  }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_gid(uint8_t gid) { gid_ = gid; }
  void set_time(time::RealTimeClock *time) { time_ = time; }
  void set_manual_color(uint8_t color) { manual_color_ = color; }
  // Mired range the light advertises (cold end, warm end) and which end UI2 = 0 is.
  void set_color_range(float cold_mireds, float warm_mireds, bool invert) {
    cold_mireds_ = cold_mireds;
    warm_mireds_ = warm_mireds;
    invert_color_ = invert;
  }
  void set_refresh_interval(uint32_t ms) { refresh_ms_ = ms; }
  void set_num_slots(size_t n) {
    kessil::klink::SchedulePoint blank = {0, {0, 0, 0, 0, 0}, false};
    slots_.assign(n, blank);
  }

  void setup() override;
  void update();  // schedule tick, run every few seconds from a timer
  void dump_config() override;
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  size_t num_slots() const { return slots_.size(); }
  // Point editor: one point at a time, like the dots on Kessil's own Quick Set screen. Editing a point
  // enables it, stores it through the slot's text entity (saved to flash shortly after the last edit)
  // and tells the editor entities to refresh.
  size_t selected_point() const { return selected_; }
  void select_point(size_t index);
  const kessil::klink::SchedulePoint &point(size_t index) const { return slots_[index]; }
  void edit_point_time(uint8_t hour, uint8_t minute);
  void edit_point_channel(uint8_t index, uint8_t value);  // index 0-4 = UI1-UI5
  void edit_point_enabled(bool enabled);
  void add_on_editor_callback(std::function<void()> &&cb) { editor_cb_.add(std::move(cb)); }
  void register_slot_text(size_t index, KLinkSlotText *text);
  void set_slot(size_t index, const kessil::klink::SchedulePoint &point);
  // Web API: edit one slot (1-based in the API is passed as a 0-based index here), persist it
  // through its slot text and preview it. False when the index is out of range.
  bool set_slot_from_values(size_t index, uint16_t minute,
                            const uint8_t channels[kessil::klink::NUM_CHANNELS], bool enabled);
  // Web API: copy another group's whole program. Groups must have the same model and slot count.
  bool copy_program_from(const KLinkGroup &other);
  // Web API: the light's name, or "Group <gid>" before the light has reported one.
  std::string name() const {
    if (this->state_ != nullptr) {
      std::string n = this->state_->get_name().str();
      if (!n.empty())
        return n;
    }
    return "Group " + std::to_string(this->gid_);
  }
  // Web API: the model key used in YAML ("tuna_sun", "tuna_blue", "generic").
  std::string model_name() const {
    switch (this->model_) {
      case GroupModel::TUNA_SUN:
        return "tuna_sun";
      case GroupModel::TUNA_BLUE:
        return "tuna_blue";
      default:
        return "generic";
    }
  }
  // Web API: the current minute of day (0-1439) or -1 without a valid clock.
  int now_minute() const {
    if (this->time_ == nullptr)
      return -1;
    ESPTime now = this->time_->now();
    if (!now.is_valid())
      return -1;
    return static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute);
  }
  kessil::klink::Mode mode() const { return this->mode_.mode; }
  void set_mode(kessil::klink::Mode mode);
  void add_on_mode_callback(std::function<void()> &&cb) { this->mode_cb_.add(std::move(cb)); }
  void preview_point(size_t slot);
  void preview_minute(uint16_t minute_of_day);
  void end_preview();
  // One line for the dashboard: what this group's schedule is doing right now.
  std::string status_text();
  uint8_t gid() const { return gid_; }
  // UI2 (0-255, models without a color temperature) and UI3-UI5 (percent) from the number entities.
  // A change takes manual control, like the light.
  void set_extra(uint8_t channel, uint8_t value);
  uint8_t extra(uint8_t channel) const { return extras_[channel - 3]; }
  void set_extra_silent(uint8_t channel, uint8_t value) {
    if (channel == 2)
      manual_color_ = value;
    else if (channel >= 3 && channel <= 5)
      extras_[channel - 3] = value > 100 ? 100 : value;
  }
  // The brightness (UI1) and color (UI2) the light's current state asks for.
  void manual_levels(light::LightState *state, uint8_t &level, uint8_t &color);
  // One frame of an effect. Holds the schedule back, like a manual command.
  void send_effect_frame(const uint8_t channels[5]) {
    this->on_user_command_();
    this->send_(channels);
  }
  // Our scheduled value right now. False when there is no clock or no enabled slot.
  bool compute_now(uint8_t out[5]);

 protected:
  void send_(const uint8_t channels[5]);
  void point_changed_();
  // Persist one specific slot through its text entity (unlike point_changed_, which stores the
  // selected point). Used by the web API's per-slot editor.
  void slot_changed_(size_t index);
  // A user command takes manual control and notifies/persists when the mode changes.
  void on_user_command_();
  void mode_changed_();
  void save_mode_();

  KLinkHub *hub_{nullptr};
  time::RealTimeClock *time_{nullptr};
  size_t selected_{0};
  std::vector<KLinkSlotText *> slot_texts_;
  CallbackManager<void()> editor_cb_;
  CallbackManager<void()> mode_cb_;
  ESPPreferenceObject mode_pref_;
  uint8_t gid_{0};
  uint8_t manual_color_{127};
  float cold_mireds_{153.0f};
  float warm_mireds_{370.0f};
  bool invert_color_{false};
  uint8_t extras_[3]{0, 0, 0};
  GroupModel model_{GroupModel::TUNA_SUN};
  light::LightState *state_{nullptr};  // the light, remembered from write_state
  uint32_t refresh_ms_{1000};
  std::vector<kessil::klink::SchedulePoint> slots_;
  kessil::klink::ModeState mode_;
  bool ignored_initial_{false};
  bool have_sent_{false};
  uint8_t last_sent_[5]{0, 0, 0, 0, 0};
  uint32_t last_sent_ms_{0};
};

// A 0-100% control for UI3, UI4 or UI5 of one group.
class KLinkChannelNumber : public number::Number, public Component {
 public:
  void set_group(KLinkGroup *group) { group_ = group; }
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

  KLinkGroup *group_{nullptr};
  uint8_t channel_{4};
  bool point_mode_{false};
  ESPPreferenceObject pref_;
};

// The group's operating mode: Program, Manual or Off (option index = kessil::klink::Mode).
class KLinkModeSelect : public select::Select, public Component {
 public:
  void set_group(KLinkGroup *group) { group_ = group; }
  void setup() override;
  void dump_config() override;
  // After the group, so the mode it restored from flash is what the select first shows.
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  void control(size_t index) override { this->group_->set_mode(static_cast<kessil::klink::Mode>(index)); }
  KLinkGroup *group_{nullptr};
};

// Which program point the editor entities show and edit.
class KLinkPointSelect : public select::Select, public Component {
 public:
  void set_group(KLinkGroup *group) { group_ = group; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(size_t index) override { this->group_->select_point(index); }
  KLinkGroup *group_{nullptr};
};

#ifdef USE_DATETIME_TIME
// The selected program point's time of day.
class KLinkPointTime : public datetime::TimeEntity, public Component {
 public:
  void set_group(KLinkGroup *group) { group_ = group; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(const datetime::TimeCall &call) override;
  void refresh_();
  KLinkGroup *group_{nullptr};
};
#endif

// Light effects, built on ten group writes a second. They follow the light's own brightness and color
// temperature.
enum class FxKind : uint8_t { PULSE, COLOR_SWEEP, SUNRISE, SUNSET };

class KLinkEffect : public light::LightEffect {
 public:
  explicit KLinkEffect(const char *name) : light::LightEffect(name) {}
  void set_kind(FxKind kind) { kind_ = kind; }
  void set_duration(uint32_t ms) { duration_ms_ = ms; }    // 0 = until stopped
  void set_period(uint32_t ms) { period_ms_ = ms; }        // Pulse and Color sweep
  void set_min_brightness(float fraction) { min_fraction_ = fraction; }

  void start() override;
  void apply() override;

 protected:
  void finish_(bool turn_off);

  FxKind kind_{FxKind::PULSE};
  uint32_t duration_ms_{120000};
  uint32_t period_ms_{6000};
  float min_fraction_{0.2f};
  uint32_t start_ms_{0};
  uint32_t last_tick_ms_{0};
};

// One schedule slot, edited as text: "HH:MM u1 u2 u3 u4 u5" or "off".
class KLinkSlotText : public text::Text, public Component {
 public:
  void set_group(KLinkGroup *group) { group_ = group; }
  void set_index(uint8_t index) { index_ = index; }
  void set_initial_value(const std::string &value) { initial_ = value; }

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

  KLinkGroup *group_{nullptr};
  uint8_t index_{0};
  std::string initial_{"off"};
  ESPPreferenceObject pref_;
};

enum class SwitchKind : uint8_t { WRITES, COMMISSIONING, POINT_ENABLED };

class KLinkSwitch : public switch_::Switch, public Component {
 public:
  void set_kind(SwitchKind kind) { kind_ = kind; }
  void set_hub(KLinkHub *hub) { hub_ = hub; }
  void set_group(KLinkGroup *group) { group_ = group; }

  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
  void apply_(bool state);

  SwitchKind kind_{SwitchKind::WRITES};
  KLinkHub *hub_{nullptr};
  KLinkGroup *group_{nullptr};
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
  void set_group(KLinkGroup *group) { group_ = group; }
  void update() override;
  void dump_config() override;

 protected:
  KLinkGroup *group_{nullptr};
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

}  // namespace klink
}  // namespace esphome
