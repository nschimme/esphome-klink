#include "klink.h"
#include "klink_light_program.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <soc/gpio_struct.h>
#include <soc/soc_caps.h>
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include <hal/usb_serial_jtag_ll.h>
#endif

// The GPIO matrix registers are named differently across ESP32 families.
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)
#define KL_OUT_SEL(p) GPIO.func_out_sel_cfg[p].func_sel
#define KL_OUT_INV(p) GPIO.func_out_sel_cfg[p].inv_sel
#define KL_ENABLE_W1TS GPIO.enable_w1ts
#define KL_ENABLE_W1TC GPIO.enable_w1tc
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define KL_OUT_SEL(p) GPIO.func_out_sel_cfg[p].out_sel
#define KL_OUT_INV(p) GPIO.func_out_sel_cfg[p].inv_sel
#define KL_ENABLE_W1TS GPIO.enable_w1ts.val
#define KL_ENABLE_W1TC GPIO.enable_w1tc.val
#else  // ESP32-C6 and later
#define KL_OUT_SEL(p) GPIO.func_out_sel_cfg[p].out_sel
#define KL_OUT_INV(p) GPIO.func_out_sel_cfg[p].out_inv_sel
#define KL_ENABLE_W1TS GPIO.enable_w1ts.val
#define KL_ENABLE_W1TC GPIO.enable_w1tc.val
#endif
#endif

namespace esphome {
namespace klink {

static const char *const TAG = "klink";

namespace kk = kessil::klink;

static const uint32_t DEFAULT_MIN_KID = 18000000;
static const uint32_t DEFAULT_MAX_KID = 36000000;  // inclusive
static const uint32_t COMMISSIONING_TIMEOUT_MS = 5 * 60 * 1000;

// ---- UartIo ---------------------------------------------------------------------------------

uint32_t UartIo::now_ms() { return millis(); }
bool UartIo::read_byte(uint8_t &out) { return this->hub_->rx_byte(out); }
void UartIo::write(const std::vector<uint8_t> &frame) { this->hub_->tx_frame(frame); }

// ---- Hub ------------------------------------------------------------------------------------

void KLinkHub::setup() {
  this->guard_.start(millis());
  this->want_writes_ = this->auto_writes_;  // the loop below enables writes once the bus is quiet
  if (this->dp_pin_ >= 0)
    this->direct_setup_();
  // Always listen: frames from other controllers feed the bus guard, and the monitor logs them.
  this->bus_.set_frame_callback([this](const std::vector<uint8_t> &frame) {
    if (this->monitor_) {
      ESP_LOGI(TAG, "bus @%u: %s  [%s]", static_cast<unsigned>(millis()), format_hex_pretty(frame.data(), frame.size()).c_str(),
               kk::describe_frame(frame).c_str());
    }
    this->observe_(frame);
    if (kk::looks_like_controller_frame(frame) && this->guard_.on_foreign_frame(millis())) {
      // Another controller started talking while we were writing: stop at once and latch.
      this->writes_enabled_ = false;
      this->want_writes_ = false;
      ESP_LOGE(TAG, "BUS CONFLICT: another controller was heard while writing (%s). Send to lights is OFF.",
               kk::describe_frame(frame).c_str());
      this->writes_cb_.call(false);
    }
  });
  this->location_pref_ = global_preferences->make_preference<float[2]>(fnv1_hash("klink_location"));
  float last[2] = {NAN, NAN};
  if (this->location_pref_.load(&last)) {
    for (int i = 0; i < 2; i++)
      if (!std::isnan(last[i]) && std::fabs(last[i]) <= (i == 0 ? 90.0f : 180.0f))
        this->location_[i].restored = last[i];
  }
  this->pref_ = global_preferences->make_preference<StoredInventory>(fnv1_hash("klink_inventory"));
  StoredInventory stored;
  if (this->pref_.load(&stored) && stored.length <= sizeof(stored.data)) {
    std::vector<uint8_t> blob(stored.data, stored.data + stored.length);
    if (!this->inventory_.deserialize(blob))
      ESP_LOGW(TAG, "Stored lamp inventory is invalid or from another version; starting empty");
  }
  this->names_pref_ = global_preferences->make_preference<StoredNames>(fnv1_hash("klink_names"));
  StoredNames stored_names;
  if (this->names_pref_.load(&stored_names) && stored_names.length <= sizeof(stored_names.data)) {
    std::vector<uint8_t> blob(stored_names.data, stored_names.data + stored_names.length);
    if (!this->names_.deserialize(blob))
      ESP_LOGW(TAG, "Stored lamp names are invalid or from another version; starting empty");
  }
  this->group_names_pref_ = global_preferences->make_preference<StoredNames>(fnv1_hash("klink_group_names"));
  StoredNames stored_group_names;
  if (this->group_names_pref_.load(&stored_group_names) && stored_group_names.length <= sizeof(stored_group_names.data)) {
    std::vector<uint8_t> blob(stored_group_names.data, stored_group_names.data + stored_group_names.length);
    if (!this->group_names_.deserialize(blob))
      ESP_LOGW(TAG, "Stored group names are invalid or from another version; starting empty");
  }
}

void KLinkHub::loop() {
  this->bus_.loop();
  if (this->direct_ok_ && millis() - this->last_byte_log_ms_ >= 5000) {
    this->last_byte_log_ms_ = millis();
    if (this->rx_bytes_ != this->last_byte_log_rx_) {
      this->last_byte_log_rx_ = this->rx_bytes_;
      // Our own transmissions read back on D+, so a healthy bus shows rx close to tx; extra bytes
      // are lamp replies (or noise).
      ESP_LOGD(TAG, "bus bytes: sent %u, received %u", static_cast<unsigned>(this->tx_bytes_),
               static_cast<unsigned>(this->rx_bytes_));
    }
  }
  if (this->want_writes_ && !this->writes_enabled_ && !this->guard_.conflict_latched()) {
    std::string why;
    if (this->guard_.request_transmit(millis(), why)) {
      this->writes_enabled_ = true;
      ESP_LOGW(TAG, "Send to lights ENABLED: the bus is quiet, frames are now transmitted");
      this->writes_cb_.call(true);
    } else if (millis() - this->last_guard_log_ms_ > 10000) {
      this->last_guard_log_ms_ = millis();
      ESP_LOGW(TAG, "Send to lights pending: %s", why.c_str());
    }
  }
  if (this->writes_enabled_)
    this->scan_note_.clear();  // the refusal it describes no longer applies
  // No stored lamps: scan once by itself as soon as the hub controls the bus, so a fresh device
  // finds its lamps without any button press. A manual scan can always be run later.
  if (this->writes_enabled_ && !this->scanning_ && this->inventory_.lamps().empty() && !this->auto_scanned_) {
    this->auto_scanned_ = true;
    this->start_scan();
  }
  if (this->commissioning_ && static_cast<int32_t>(millis() - this->commissioning_deadline_) >= 0) {
    ESP_LOGI(TAG, "Commissioning mode timed out");
    this->set_commissioning(false);
  }
}

void KLinkHub::dump_config() {
  ESP_LOGCONFIG(TAG, "K-Link:");
  ESP_LOGCONFIG(TAG, "  Writes enabled: %s", YESNO(this->writes_enabled_));
  ESP_LOGCONFIG(TAG, "  Bus monitor: %s", YESNO(this->monitor_));
  ESP_LOGCONFIG(TAG, "  Auto writes: %s", YESNO(this->auto_writes_));
  if (this->dp_pin_ >= 0)
    ESP_LOGCONFIG(TAG, "  Direct drive: D+ GPIO%d, D- GPIO%d (active: %s)", this->dp_pin_, this->dm_pin_,
                  YESNO(this->direct_ok_));
  ESP_LOGCONFIG(TAG, "  Bus guard: writes wait for a quiet bus and stop if another controller is heard");
  ESP_LOGCONFIG(TAG, "  Known lamps: %u", static_cast<unsigned>(this->inventory_.lamps().size()));
  this->check_uart_settings(115200);
}

bool KLinkHub::set_writes_enabled(bool enabled) {
  if (!enabled) {
    if (this->writes_enabled_)
      ESP_LOGW(TAG, "Send to lights disabled: dry run, frames are only logged");
    this->want_writes_ = false;
    this->writes_enabled_ = false;
    this->guard_.stop_transmit();
    return true;
  }
  if (this->writes_enabled_)
    return true;
  this->want_writes_ = true;  // keep trying from loop() until the bus has been quiet long enough
  std::string why;
  if (!this->guard_.request_transmit(millis(), why)) {
    ESP_LOGE(TAG, "Refusing to enable bus writes yet: %s", why.c_str());
    return false;
  }
  this->writes_enabled_ = true;
  ESP_LOGW(TAG, "Send to lights ENABLED: frames are now transmitted");
  return true;
}

std::string KLinkHub::state_text() const {
  std::string text;
  if (this->guard_.conflict_latched())
    text = "Conflict: another controller wrote while this one was controlling. Press Retry to try again";
  else if (this->scanning_)
    text = "Scanning for lamps";
  else if (this->writes_enabled_)
    text = "Controlling";
  else if (this->want_writes_)
    text = this->other_controller_recent() ? "Shadowing another controller" : "Waiting for a quiet bus";
  else
    text = "Writes off (dry run)";
  if (!this->scan_note_.empty())
    text += " (" + this->scan_note_ + ")";
  return text;
}

void KLinkHub::set_commissioning(bool enabled) {
  this->commissioning_ = enabled;
  if (enabled)
    this->commissioning_deadline_ = millis() + COMMISSIONING_TIMEOUT_MS;
  else
    this->staged_ = false;
  this->commissioning_cb_.call(enabled);
}

void KLinkHub::tx_frame(const std::vector<uint8_t> &frame) {
  if (!this->writes_enabled_) {
    ESP_LOGI(TAG, "dry run, not sent: %s", format_hex_pretty(frame.data(), frame.size()).c_str());
    return;
  }
  ESP_LOGD(TAG, "tx: %s", format_hex_pretty(frame.data(), frame.size()).c_str());
  if (this->dp_pin_ >= 0 && !this->direct_ok_) {
    ESP_LOGE(TAG, "Direct drive is not active; not sending");
    return;
  }
  this->direct_drive_(true);
  this->tx_bytes_ += frame.size();
  this->write_array(frame.data(), frame.size());
  this->flush();
  if (this->direct_ok_) {
    // D+ is also the UART's RX pin, so a working drive stage reads the frame back.
    ESP_LOGD(TAG, "direct drive: %d bytes sent, %d bytes read back (out_sel D+ %u D- %u, D- inv %u, oen_sel D+ %u D- %u)",
             static_cast<int>(frame.size()), this->available(),
             static_cast<unsigned>(KL_OUT_SEL(this->dp_pin_)),
             static_cast<unsigned>(KL_OUT_SEL(this->dm_pin_)),
             static_cast<unsigned>(KL_OUT_INV(this->dm_pin_)),
             static_cast<unsigned>(GPIO.func_out_sel_cfg[this->dp_pin_].oen_sel),
             static_cast<unsigned>(GPIO.func_out_sel_cfg[this->dm_pin_].oen_sel));
  }
  this->direct_drive_(false);
}

// ---- Direct drive (ESP32 only) --------------------------------------------------------------

#if defined(USE_ESP32)
// The pads are driven a little before the first byte and released a little after the last.
static const uint32_t DRIVE_LEAD_US = 100;
static const uint32_t DRIVE_HOLD_US = 50;
static const uint32_t SIG_GPIO_OUT = 128;  // the matrix's "plain GPIO output", i.e. nothing routed

void KLinkHub::direct_setup_() {
  const gpio_num_t dp = static_cast<gpio_num_t>(this->dp_pin_);
  const gpio_num_t dm = static_cast<gpio_num_t>(this->dm_pin_);
  // ESPHome's UART has routed its TX signal to the D- pin (the uart tx_pin). Find which signal it
  // is, then send it to D+ as is and to D- inverted, so the pair is always complementary.
  uint32_t signal = KL_OUT_SEL(this->dm_pin_);
  if (signal == SIG_GPIO_OUT) {
    ESP_LOGE(TAG, "Direct drive: no UART TX signal on GPIO%d. Set the uart tx_pin to the D- pin. Disabled.",
             this->dm_pin_);
    return;
  }
  // Let go of both pads before anything else: ESPHome's UART setup left D- driving the idle level.
  // gpio_set_direction() must not be used after the routing below: enabling an output through it
  // re-routes the pad to a plain GPIO, which would undo the UART signal and the inversion.
  KL_ENABLE_W1TC = (1U << this->dm_pin_) | (1U << this->dp_pin_);
  esp_rom_gpio_pad_select_gpio(this->dp_pin_);
  gpio_input_enable(dp);
  esp_rom_gpio_connect_out_signal(this->dp_pin_, signal, false, false);
  esp_rom_gpio_connect_out_signal(this->dm_pin_, signal, true, false);
  // Output enable comes from the GPIO enable register (not the UART), so that is the only thing
  // direct_drive_() has to switch.
  GPIO.func_out_sel_cfg[this->dp_pin_].oen_sel = 1;
  GPIO.func_out_sel_cfg[this->dm_pin_].oen_sel = 1;
  KL_ENABLE_W1TC = (1U << this->dm_pin_) | (1U << this->dp_pin_);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  // The pads default to the USB PHY. Hand them to the GPIO matrix (the native USB console is gone
  // until the next reset; flash again by holding BOOT, or use OTA).
  usb_serial_jtag_ll_phy_enable_pad(false);
#endif
  this->direct_ok_ = true;
  ESP_LOGW(TAG, "Direct drive ready: D+ GPIO%d and D- GPIO%d are high-impedance until a frame is sent", this->dp_pin_,
           this->dm_pin_);
}

void KLinkHub::direct_drive_(bool on) {
  if (!this->direct_ok_)
    return;
  const uint32_t mask = (1U << this->dp_pin_) | (1U << this->dm_pin_);
  if (on) {
    KL_ENABLE_W1TS = mask;
    esp_rom_delay_us(DRIVE_LEAD_US);
  } else {
    esp_rom_delay_us(DRIVE_HOLD_US);
    KL_ENABLE_W1TC = mask;
  }
}
#else
void KLinkHub::direct_setup_() { ESP_LOGE(TAG, "Direct drive needs an ESP32"); }
void KLinkHub::direct_drive_(bool) {}
#endif

void KLinkHub::write_group(uint8_t gid, const uint8_t channels[5]) {
  if (gid < kk::MIN_GID || gid > kk::MAX_GID) {
    ESP_LOGE(TAG, "Refusing group write to GID %u", gid);
    return;
  }
  this->bus_.enqueue(kk::group_channels(gid, channels), 0, 0, nullptr);
}

void KLinkHub::start_scan() {
  if (!this->writes_enabled_) {
    this->scan_note_ = "Scan needs Send to lights on";
    ESP_LOGW(TAG, "Scan refused: the hub is not controlling the bus (%s)", this->state_text().c_str());
    return;
  }
  if (this->scanning_)
    return;
  this->scan_note_.clear();
  ESP_LOGI(TAG, "Scanning for lamps (KIDs %u-%u)", DEFAULT_MIN_KID, DEFAULT_MAX_KID);
  this->scanning_ = true;
  this->scan_found_ = 0;
  this->bus_.enqueue(kk::unmute(kk::ALL_KIDS), 0, 0, nullptr);
  this->scanner_.start(DEFAULT_MIN_KID, DEFAULT_MAX_KID + 1, [this](bool complete, const std::vector<uint32_t> &kids) {
    this->scan_complete_ = complete;
    this->scan_kids_ = kids;
    this->scan_pos_ = 0;
    if (!complete)
      ESP_LOGW(TAG, "Scan ended early: an unresolvable collision on a single KID");
    this->read_next_identity_();
  });
}

void KLinkHub::read_next_identity_() {
  if (this->scan_pos_ >= this->scan_kids_.size()) {
    // Every lamp was muted as it was found and re-muted after its identity read: the idle state
    // other controllers leave the bus in, so nothing more to send.
    this->save_inventory_();
    this->scanning_ = false;
    ESP_LOGI(TAG, "Scan done: %u lamp(s) answered, %u known", static_cast<unsigned>(this->scan_found_),
             static_cast<unsigned>(this->inventory_.lamps().size()));
    this->scan_done_cb_.call(this->scan_complete_, this->scan_found_);
    return;
  }
  uint32_t kid = this->scan_kids_[this->scan_pos_++];
  // The scan left this lamp muted, so unmute it by its own KID, read it, and mute it again.
  this->bus_.enqueue(kk::unmute(kid), 0, 0, nullptr);
  this->bus_.enqueue(kk::identity(kid), kk::CMD_GET_GID_TYPE, kk::REPLY_TIMEOUT_MS, [this, kid](bool ok, const std::vector<uint8_t> &reply) {
    kk::Identity id = {0, 0};
    const bool parsed = ok && kk::parse_identity(reply, id);
    const bool known = parsed && this->inventory_.upsert(kid, id.gid, id.type);
    if (known) {
      this->scan_found_++;
      ESP_LOGI(TAG, "Lamp KID %u: GID %u, type %u", kid, id.gid, id.type);
      this->lamp_found_cb_.call(kid, id.gid, id.type);
    } else if (parsed) {
      // A type this controller has no model for: it is not added, but what it reports is logged so it can be
      // reported (the read-only model and firmware queries below run for it too).
      ESP_LOGW(TAG, "Lamp KID %u reports GID %u, type %u: not a lamp type this controller drives", kid, id.gid, id.type);
    } else {
      ESP_LOGW(TAG, "Lamp KID %u did not return a valid identity", kid);
    }
    if (!parsed) {
      this->bus_.enqueue(kk::mute(kid), kk::CMD_MUTE, kk::REPLY_TIMEOUT_MS,
                         [this](bool, const std::vector<uint8_t> &) { this->read_next_identity_(); });
      return;
    }
    // Model and firmware carry no KID: the one unmuted lamp answers them.
    this->bus_.enqueue(kk::model_query(), kk::CMD_MODEL, kk::REPLY_TIMEOUT_MS, [this, kid, known](bool ok, const std::vector<uint8_t> &reply) {
      std::string model;
      const bool have_model = ok && kk::parse_model(reply, model);
      if (have_model && known) {
        const kk::LampRecord *lamp = this->inventory_.find(kid);
        this->inventory_.set_info(kid, model, lamp != nullptr && lamp->has_firmware, lamp ? lamp->fw_major : 0,
                                  lamp ? lamp->fw_minor : 0, lamp ? lamp->fw_patch : 0);
      }
      this->bus_.enqueue(kk::firmware_query(), kk::CMD_FIRMWARE_VERSION, kk::REPLY_TIMEOUT_MS, [this, kid, known, model, have_model](bool fw_ok, const std::vector<uint8_t> &fw_reply) {
        kk::FirmwareVersion fw = {0, 0, 0};
        const bool have_fw = fw_ok && kk::parse_firmware(fw_reply, fw);
        if (have_fw && known) {
          const kk::LampRecord *lamp = this->inventory_.find(kid);
          this->inventory_.set_info(kid, lamp != nullptr ? lamp->model : model, true, fw.major, fw.minor, fw.patch);
        }
        ESP_LOGI(TAG, "Lamp KID %u reports model \"%s\", firmware %s", kid, have_model ? model.c_str() : "(unknown)",
                 have_fw ? (std::to_string(fw.major) + "." + std::to_string(fw.minor) + "." + std::to_string(fw.patch)).c_str()
                         : "(unknown)");
        this->bus_.enqueue(kk::mute(kid), kk::CMD_MUTE, kk::REPLY_TIMEOUT_MS,
                           [this](bool, const std::vector<uint8_t> &) { this->read_next_identity_(); });
      });
    });
  });
}

// A lamp only answers the find command when a controller probe (34) came just before it: that is what the
// original controller does, with a probe every second. Found on hardware: a bare find does nothing, a probe followed 300 ms
// later by the find makes that one lamp flicker (a short dip). The group's own writes pause meanwhile so
// its once-a-second refresh does not cover the flicker.
void KLinkHub::identify(uint32_t kid) {
  ESP_LOGI(TAG, "Identify KID %u", kid);
  if (const kk::LampRecord *lamp = this->inventory_.find(kid))
    this->hold_group(lamp->gid, 2000);
  this->bus_.enqueue(kk::build(kk::CMD_CONTROLLER_PROBE, {}), 0, 0, nullptr);
  this->set_timeout(kid, 300, [this, kid]() { this->bus_.enqueue(kk::find(kid), 0, 0, nullptr); });
}

void KLinkHub::hold_group(uint8_t gid, uint32_t ms) {
  const uint32_t until = millis() + ms;
  for (auto &hold : this->holds_)
    if (hold.gid == gid) {
      hold.until_ms = until;
      return;
    }
  this->holds_.push_back({gid, until});
}

void KLinkHub::set_ha_coordinate(int index, float value) {
  if (std::isnan(value))
    return;
  if (this->location_[index].ha == value)
    return;
  this->location_[index].ha = value;
  this->save_location_();
  this->location_cb_.call();
}

// Both Home Assistant coordinates, kept so a reboot can use them before Home Assistant has reported.
void KLinkHub::save_location_() {
  if (std::isnan(this->location_[0].ha) || std::isnan(this->location_[1].ha))
    return;
  float last[2] = {static_cast<float>(this->location_[0].ha), static_cast<float>(this->location_[1].ha)};
  this->location_pref_.save(&last);
}

void KLinkHub::set_weather_condition(const std::string &condition) {
  const bool was_thunder = this->have_condition_ && kessil::klink::weather_is_thunder(this->weather_condition_);
  const bool first = !this->have_condition_;
  this->weather_condition_ = condition;
  this->have_condition_ = true;
  ESP_LOGD(TAG, "Weather: %s", condition.c_str());
  // A thunderstorm that begins starts the Storm effect, at most once every half hour. Never at boot: the
  // first value only tells us what the weather already is.
  if (!first && this->storms_from_weather_ && !was_thunder && kessil::klink::weather_is_thunder(condition) &&
      (!this->storm_started_once_ || millis() - this->last_storm_ms_ > 30u * 60u * 1000u)) {
    for (ProgramHost *program : this->programs_) {
      // Real weather flashes the lamps on the bus; a light program's own lights (a hallway lamp) are not asked to.
      if (std::strcmp(program->kind(), "bus") != 0 || program->mode() != kk::Mode::PROGRAM)
        continue;
      bool has_storm = false;
      for (const auto &name : program->effect_names())
        if (name == "Storm")
          has_storm = true;
      if (has_storm && program->effect_label().empty()) {
        ESP_LOGI(TAG, "Thunderstorm reported: starting Storm on %s", program->name().c_str());
        program->start_effect("Storm");
        this->storm_started_once_ = true;
        this->last_storm_ms_ = millis();
      }
    }
  }
}

void KLinkHub::set_entered_coordinate(int index, float value) {
  this->location_[index].entered = value;
  this->location_cb_.call();
}

static bool coordinate_value(const KLinkHub::Coordinate &c, double &out) {
  if (!std::isnan(c.entered))
    out = c.entered;
  else if (!std::isnan(c.ha))
    out = c.ha;
  else if (!std::isnan(c.restored))
    out = c.restored;
  else if (!std::isnan(c.yaml))
    out = c.yaml;
  else if (!std::isnan(c.zone))
    out = c.zone;
  else
    return false;
  return true;
}

bool KLinkHub::location(double &latitude, double &longitude) const {
  return coordinate_value(this->location_[0], latitude) && coordinate_value(this->location_[1], longitude);
}

const char *KLinkHub::location_source() const {
  double lat, lon;
  if (!this->location(lat, lon))
    return "";
  if (!std::isnan(this->location_[0].entered) || !std::isnan(this->location_[1].entered))
    return "entered here";
  if (!std::isnan(this->location_[0].ha) && !std::isnan(this->location_[1].ha))
    return "Home Assistant";
  if (!std::isnan(this->location_[0].restored) && !std::isnan(this->location_[1].restored))
    return "last known";
  if (std::isnan(this->location_[0].yaml) || std::isnan(this->location_[1].yaml))
    return "time zone (approximate)";
  return "YAML";
}

bool KLinkHub::group_held(uint8_t gid) {
  const uint32_t now = millis();
  for (const auto &hold : this->holds_)
    if (hold.gid == gid && static_cast<int32_t>(hold.until_ms - now) > 0)
      return true;
  return false;
}

void KLinkHub::identify_next() {
  const std::vector<kk::LampRecord> &lamps = this->inventory_.lamps();
  if (lamps.empty()) {
    ESP_LOGW(TAG, "No known lamps: run a scan first");
    return;
  }
  this->identify_pos_ %= lamps.size();
  this->identify(lamps[this->identify_pos_++].kid);
}

void KLinkHub::retry() {
  // Never turns sending on by itself: only a conflict or a pending request is retried.
  if (!this->guard_.conflict_latched() && !this->want_writes_ && !this->writes_enabled_)
    return;
  // Exactly what turning Send to lights off and on again did before: drop write control and ask
  // for it again through the same path. request_transmit() lifts the conflict latch once the bus
  // has been quiet long enough; until then the request stays pending in loop().
  this->set_writes_enabled(false);
  this->set_writes_enabled(true);
}

bool KLinkHub::stage_assignment(uint32_t kid, uint8_t gid) {
  if (!this->commissioning_) {
    ESP_LOGW(TAG, "Assignment refused: turn on commissioning mode first");
    return false;
  }
  std::string why = this->inventory_.check_assignment(kid, gid);
  if (!why.empty()) {
    ESP_LOGW(TAG, "Assignment of KID %u to GID %u refused: %s", kid, gid, why.c_str());
    return false;
  }
  this->staged_ = true;
  this->staged_kid_ = kid;
  this->staged_gid_ = gid;
  ESP_LOGW(TAG, "Staged: KID %u -> GID %u. Run the confirm action to send it.", kid, gid);
  return true;
}

void KLinkHub::confirm_assignment() {
  if (!this->commissioning_ || !this->staged_) {
    ESP_LOGW(TAG, "Nothing staged, or commissioning mode is off");
    return;
  }
  uint32_t kid = this->staged_kid_;
  uint8_t gid = this->staged_gid_;
  this->staged_ = false;
  this->bus_.enqueue(kk::unmute(kid), 0, 0, nullptr);
  this->bus_.enqueue(kk::set_gid(kid, gid), 0, 0, nullptr);
  kk::read_identity_async(this->bus_, kid, [this, kid, gid](bool ok, kk::Identity id) {
    if (ok && id.gid == gid && this->inventory_.upsert(kid, id.gid, id.type)) {
      this->save_inventory_();
      ESP_LOGI(TAG, "KID %u is now in GID %u (verified)", kid, gid);
    } else {
      ESP_LOGE(TAG, "KID %u: assignment to GID %u NOT verified; rescan before trusting the inventory", kid, gid);
    }
  });
}

bool KLinkHub::check_assign(uint32_t kid, uint8_t gid, std::string &error) const {
  error.clear();
  if (!this->writes_enabled_) {
    error = "Send to lights is off";
    return false;
  }
  kk::AssignmentError why = this->inventory_.check_assignment_error(kid, gid);
  if (why != kk::AssignmentError::NONE) {
    error = kk::assignment_error_text(why);
    return false;
  }
  return true;
}

bool KLinkHub::assign(uint32_t kid, uint8_t gid, std::string &error) {
  error.clear();
  if (!this->writes_enabled_) {
    error = "Send to lights is off";
    return false;
  }
  this->set_commissioning(true);
  if (!this->stage_assignment(kid, gid)) {
    kk::AssignmentError why = this->inventory_.check_assignment_error(kid, gid);
    error = why == kk::AssignmentError::NONE ? "Assignment refused" : kk::assignment_error_text(why);
    return false;
  }
  this->confirm_assignment();
  return true;
}

// Shadow mode: when another controller writes a group we also schedule, compare its value with ours.
void KLinkHub::observe_(const std::vector<uint8_t> &frame) {
  if (frame.size() != 14 || frame[4] != kk::CMD_SET_GROUP_CHANNELS)
    return;
  uint8_t gid = frame[10];
  for (size_t i = 0; i < this->groups_.size(); i++) {
    if (this->groups_[i]->gid() != gid)
      continue;
    uint8_t ours[5];
    if (!this->groups_[i]->compute_now(ours))
      return;  // no clock yet, or no enabled slot: nothing to compare against
    uint8_t theirs[5] = {frame[5], frame[6], frame[7], frame[8], frame[9]};
    if (kk::channels_close(ours, theirs, 1)) {
      this->shadow_matches_++;
      ESP_LOGD(TAG, "shadow GID %u: matches (ours %u %u, theirs %u %u)", gid, ours[0], ours[1], theirs[0], theirs[1]);
    } else {
      this->shadow_mismatches_++;
      ESP_LOGW(TAG, "shadow GID %u: DIFFERS. ours %u %u %u %u %u, other controller %u %u %u %u %u", gid, ours[0],
               ours[1], ours[2], ours[3], ours[4], theirs[0], theirs[1], theirs[2], theirs[3], theirs[4]);
    }
    return;
  }
}

void KLinkHub::save_inventory_() {
  std::vector<uint8_t> blob = this->inventory_.serialize();
  StoredInventory stored;
  std::memset(&stored, 0, sizeof(stored));
  if (blob.size() > sizeof(stored.data))
    return;
  stored.length = static_cast<uint8_t>(blob.size());
  std::memcpy(stored.data, blob.data(), blob.size());
  this->pref_.save(&stored);
}

void KLinkHub::save_names_() {
  std::vector<uint8_t> blob = this->names_.serialize();
  StoredNames stored;
  std::memset(&stored, 0, sizeof(stored));
  if (blob.size() > sizeof(stored.data))
    return;
  stored.length = static_cast<uint16_t>(blob.size());
  std::memcpy(stored.data, blob.data(), blob.size());
  this->names_pref_.save(&stored);
}

void KLinkHub::save_group_names_() {
  std::vector<uint8_t> blob = this->group_names_.serialize();
  StoredNames stored;
  std::memset(&stored, 0, sizeof(stored));
  if (blob.size() > sizeof(stored.data))
    return;
  stored.length = static_cast<uint16_t>(blob.size());
  std::memcpy(stored.data, blob.data(), blob.size());
  this->group_names_pref_.save(&stored);
}

bool KLinkHub::check_group_name(uint32_t gid, const std::string &name, std::string &error) const {
  error.clear();
  kk::NameTable probe = this->group_names_;
  if (probe.set(gid, name))
    return true;
  if (name.size() > kk::NAME_MAX_LEN)
    error = "name is too long (24 bytes max)";
  else if (!this->group_names_.contains(gid) && this->group_names_.size() >= kk::MAX_NAMES)
    error = "no room for another name";
  else
    error = "name is not valid UTF-8";
  return false;
}

bool KLinkHub::set_group_name(uint32_t gid, const std::string &name, std::string &error) {
  if (!this->check_group_name(gid, name, error))
    return false;
  this->group_names_.set(gid, name);
  this->save_group_names_();
  return true;
}

bool KLinkHub::check_name(uint32_t kid, const std::string &name, std::string &error) const {
  error.clear();
  kk::NameTable probe = this->names_;
  if (probe.set(kid, name))
    return true;
  if (name.size() > kk::NAME_MAX_LEN)
    error = "name is too long (24 bytes max)";
  else if (!this->names_.contains(kid) && this->names_.size() >= kk::MAX_NAMES)
    error = "no room for another name";
  else
    error = "name is not valid UTF-8";
  return false;
}

bool KLinkHub::set_name(uint32_t kid, const std::string &name, std::string &error) {
  error.clear();
  if (this->names_.set(kid, name)) {
    this->save_names_();
    return true;
  }
  if (name.size() > kk::NAME_MAX_LEN)
    error = "name is too long (24 bytes max)";
  else if (!this->names_.contains(kid) && this->names_.size() >= kk::MAX_NAMES)
    error = "no room for another name";
  else
    error = "name is not valid UTF-8";
  return false;
}

// Every lamp of the group flickers together, as for a single lamp.
bool KLinkHub::identify_gid(uint8_t gid) {
  bool any = false;
  for (const auto &lamp : this->inventory_.lamps()) {
    if (lamp.gid == gid) {
      this->identify(lamp.kid);
      any = true;
    }
  }
  return any;
}

// ---- Group ----------------------------------------------------------------------------------

static std::vector<KLinkGroup *> &all_groups() {
  static std::vector<KLinkGroup *> groups;
  return groups;
}

KLinkGroup *KLinkGroup::from_output(const light::LightOutput *output) {
  for (KLinkGroup *group : all_groups())
    if (static_cast<const light::LightOutput *>(group) == output)
      return group;
  return nullptr;
}

KLinkGroup::KLinkGroup() { all_groups().push_back(this); }

void KLinkGroup::setup() {
  this->hub_->register_group(this);
  this->hub_->register_program(this);
  this->setup_program_();
  this->setup_source_();
  // Every start fades the lamps in from dark (power loss, restart or update alike).
  this->soft_start_pending_ = true;
  // Effect speed and duration: runtime settings.
  this->effect_speed_pref_ =
      global_preferences->make_preference<uint8_t>(fnv1_hash("klink_fxspeed_" + std::to_string(this->gid_)));
  uint8_t speed = 0;
  if (this->effect_speed_pref_.load(&speed) && speed <= 2)
    this->effect_speed_ = speed;
  this->effect_duration_pref_ =
      global_preferences->make_preference<uint32_t>(fnv1_hash("klink_fxdur_" + std::to_string(this->gid_)));
  uint32_t duration = 120000;
  if (this->effect_duration_pref_.load(&duration) &&
      (duration == 0 || (duration >= 30000 && duration <= 300000)))
    this->effect_duration_ms_ = duration;
  this->out_pref_ = global_preferences->make_preference<OutputSettings>(fnv1_hash("klink_out_" + std::to_string(this->gid_)));
  OutputSettings stored;
  if (this->out_pref_.load(&stored) && stored.curve <= 1 &&
      stored.fade_ds <= 100 && stored.soft_start <= 120)
    this->out_settings_ = stored;
  this->apply_output_settings_();
  this->set_interval("schedule", 1000, [this]() { this->update(); });
}

void KLinkGroup::apply_output_settings_() {
  kk::OutputConfig config;
  config.fade_style = kk::FADE_EASED;  // one soft start and end; 0 seconds is instant
  config.fade_ms = static_cast<uint32_t>(this->out_settings_.fade_ds) * 100u;
  this->stage_.configure(config);
}

void KLinkGroup::set_output_settings(const OutputSettings &settings) {
  OutputSettings next = settings;
  if (next.curve > 1)
    next.curve = 1;
  if (next.fade_ds > 100)
    next.fade_ds = 100;
  if (next.soft_start > 120)
    next.soft_start = 120;
  this->out_settings_ = next;
  this->out_pref_.save(&this->out_settings_);
  this->apply_output_settings_();
  this->shown_.have = false;  // the light's brightness maps differently now: show it again
  this->apply_transition_();
  this->out_cb_.call();
}

// The transition time is also the light's default transition length, so a command from Home
// Assistant or an automation that gives none fades the same way as the group's own changes.
void KLinkGroup::apply_transition_() {
  if (this->state_ != nullptr)
    this->state_->set_default_transition_length(static_cast<uint32_t>(this->out_settings_.fade_ds) * 100u);
}

// The light's brightness after the chosen curve: a straight line, or CIE 1931. The light's own gamma
// is bypassed, so Home Assistant's % matches the program's INT / 255.
float KLinkGroup::light_brightness_(light::LightState *state) const {
  const light::LightColorValues &v = state->current_values;
  const float raw = v.get_state() * v.get_brightness();
  return static_cast<BrightnessCurve>(this->out_settings_.curve) == BrightnessCurve::CIE ? kk::perceptual_to_linear(raw) : raw;
}

void KLinkGroup::dump_config() {
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_ == GroupModel::TUNA_SUN ? "Tuna Sun" : (this->model_ == GroupModel::TUNA_BLUE ? "Tuna Blue" : "Refugium"));
  ESP_LOGCONFIG(TAG, "K-Link group:");
  ESP_LOGCONFIG(TAG, "  GID: %u", this->gid_);
  ESP_LOGCONFIG(TAG, "  Slots: %u", static_cast<unsigned>(this->slots_.size()));
  ESP_LOGCONFIG(TAG, "  Mode: %s", kk::mode_name(this->mode_.mode));
  ESP_LOGCONFIG(TAG, "  Manual color (UI2): %u", this->manual_color_);
}

light::LightTraits KLinkGroup::get_traits() {
  light::LightTraits traits;
  if (this->has_color_temperature()) {
    // The Tuna Sun: exact color temperature, or any color snapped to what the lamp can make.
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE, light::ColorMode::RGB});
    traits.set_min_mireds(this->cold_mireds_);
    traits.set_max_mireds(this->warm_mireds_);
  } else if (this->color_spec_() != nullptr) {
    traits.set_supported_color_modes({light::ColorMode::RGB});  // the Tuna Blue: snapped to the nearest color
  } else {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  }
  return traits;
}

const kk::ColorSpec *KLinkGroup::color_spec_() const {
  switch (this->model_) {
    case GroupModel::TUNA_SUN:
      return &COLOR_SUN;
    case GroupModel::TUNA_BLUE:
      return &COLOR_BLUE;
    case GroupModel::REFUGIUM:
      return &COLOR_REFUGIUM;
  }
  return nullptr;
}

void KLinkGroup::set_extra(uint8_t channel, uint8_t value) {
  if (channel == 2) {
    if (this->has_color_temperature())
      return;  // UI2 is the light's color temperature on this model
    this->manual_color_ = value > this->channel_max()[1] ? this->channel_max()[1] : value;
  } else if (channel >= 3 && channel <= 5) {
    this->extras_[channel - 3] = value > 100 ? 100 : value;
  } else {
    return;
  }
  if (this->state_ == nullptr || this->state_->get_current_effect_index() != 0)
    return;  // no light state yet, or an effect owns the output
  this->on_user_command_();
  uint8_t level, color;
  this->manual_levels(this->state_, level, color);
  uint8_t channels[5] = {level, color, this->extras_[0], this->extras_[1], this->extras_[2]};
  this->direct_next_ = true;
  this->send_(channels);
  this->publish_to_light_(channels);  // the color the light reports follows the numbers
}

void KLinkGroup::manual_levels(light::LightState *state, uint8_t &level, uint8_t &color) {
  if (!this->has_color_temperature() || state->remote_values.get_color_mode() == light::ColorMode::RGB) {
    // Brightness only: UI1 follows the light and UI2 is whatever its number entity (or manual_color) says.
    level = static_cast<uint8_t>(std::lround(this->light_brightness_(state) * 255.0f));
    color = this->manual_color_;
    return;
  }
  // Brightness is UI1. The light reports color temperature already normalized between the
  // advertised ends (0 = cold, 1 = warm); that maps linearly onto UI2.
  float t, brightness;
  state->current_values_as_ct(&t, &brightness);
  brightness = this->light_brightness_(state);
  level = static_cast<uint8_t>(std::lround(brightness * 255.0f));
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  if (this->invert_color_)
    t = 1.0f - t;
  color = static_cast<uint8_t>(std::lround(t * 255.0f));
}

// ---- Effects on the group (ProgramHost) ------------------------------------------------------

std::vector<std::string> KLinkGroup::effect_names() const {
  std::vector<std::string> names;
  if (this->state_ == nullptr)
    return names;
  for (auto *effect : this->state_->get_effects()) {
    if (effect == nullptr)
      continue;
    std::string name = effect->get_name().str();
    if (name == "None")
      continue;
    names.push_back(name);
  }
  return names;
}

bool KLinkGroup::start_effect(const std::string &name) {
  if (this->state_ == nullptr)
    return false;
  bool known = false;
  for (const auto &candidate : this->effect_names())
    if (candidate == name)
      known = true;
  if (!known)
    return false;
  auto call = this->state_->make_call();
  call.set_effect(name);
  call.set_save(false);
  call.perform();
  return true;
}

void KLinkGroup::stop_effect() {
  if (this->state_ == nullptr || !this->effect_active())
    return;
  auto call = this->state_->make_call();
  call.set_effect(static_cast<uint32_t>(0));
  call.set_save(false);
  call.perform();
}

int32_t KLinkGroup::effect_remaining_seconds() const {
  if (!this->effect_active())
    return 0;
  if (this->effect_end_ms_ == 0)
    return -1;  // until stopped
  uint32_t now = millis();
  if (static_cast<int32_t>(now - this->effect_end_ms_) >= 0)
    return 0;
  // Round up: the status shows how long is left, not how much has elapsed.
  return static_cast<int32_t>((this->effect_end_ms_ - now + 999) / 1000);
}

std::string KLinkGroup::effect_label() const {
  return this->effect_active() ? this->effect_name() : std::string();
}

void KLinkGroup::set_effect_options(uint8_t speed, uint32_t duration_ms) {
  if (speed > 2)
    speed = 2;
  // 0 means "until stopped"; otherwise the app's range is 0.5 to 5 minutes in half-minute steps.
  if (duration_ms != 0) {
    if (duration_ms < 30000)
      duration_ms = 30000;
    else if (duration_ms > 300000)
      duration_ms = 300000;
  }
  this->effect_speed_ = speed;
  this->effect_duration_ms_ = duration_ms;
  this->effect_speed_pref_.save(&this->effect_speed_);
  this->effect_duration_pref_.save(&this->effect_duration_ms_);
}

void KLinkGroup::effect_ended_() {
  // Remember what the light showed before the effect, so the light call that ends the effect (it
  // repeats those values) is not mistaken for a user command.
  this->ended_shown_ = this->shown_;
  this->effect_ended_pending_ = true;
  this->klink_effect_running_ = false;
  this->effect_end_ms_ = 0;
  // The next tick sends whatever the mode asks for again, and shows it on the light.
  this->have_sent_ = false;
  this->shown_.have = false;
  // Manual has no tick of its own once the mode is left alone here: put the held level back at once.
  if (this->mode_.mode == kk::Mode::MANUAL && this->state_ != nullptr) {
    uint8_t level, color;
    this->manual_levels(this->state_, level, color);
    uint8_t channels[5] = {level, color, this->extras_[0], this->extras_[1], this->extras_[2]};
    this->send_(channels);
  }
}

void KLinkGroup::write_bus_(const uint8_t channels[5]) {
  if (this->hub_->group_held(this->gid_))
    return;  // a lamp of this group is flashing to identify itself
  // A Refugium takes a CLR of at most 240, so no source (effects included) may send more. Only UI2
  // is limited here: effect frames put values above 100 on UI3-UI5.
  uint8_t limited[5] = {channels[0], channels[1], channels[2], channels[3], channels[4]};
  if (limited[1] > this->channel_max()[1])
    limited[1] = this->channel_max()[1];
  this->hub_->write_group(this->gid_, limited);
}

// Frames of effects go straight to the bus; the stage follows them so a later fade starts where the lamp is.
// Everything else goes through the stage, which with the default options writes the target unchanged.
void KLinkGroup::output_(const uint8_t channels[5]) {
  float target[5];
  for (int c = 0; c < 5; c++)
    target[c] = static_cast<float>(channels[c]);
  // What fades: the schedule, Off, resuming after a preview or an effect, and the first frame of an effect
  // (Storm excepted: its flashes stay hard). Direct values (commands, previews, edits, Home Assistant
  // transitions) and forwarded standard effects are already shaped and show at once.
  const bool direct = this->direct_next_ || this->fwd_effect_active_ ||
                      (this->klink_effect_running_ && !this->effect_smooth_);
  if (direct) {
    this->stage_.reset(target);
    this->stage_busy_ = false;
    this->write_bus_(channels);
  } else {
    // A slow ramp moves a few units between ticks; anything bigger is a jump and may fade.
    bool jump = !this->stage_have_target_ || this->effect_fresh_;
    this->effect_fresh_ = false;
    for (int c = 0; c < 5 && !jump; c++)
      jump = std::fabs(target[c] - this->stage_target_[c]) > 6.0f;
    if (this->soft_start_pending_) {
      this->soft_start_pending_ = false;
      if (this->out_settings_.soft_start > 0) {  // first light after a start: up from dark
        static const float dark[5] = {0, 0, 0, 0, 0};
        this->stage_.reset(dark);
        this->stage_.fade_to(target, static_cast<uint32_t>(this->out_settings_.soft_start) * 1000u);
      } else {
        this->stage_.set_target(target, jump);
      }
    } else {
      this->stage_.set_target(target, jump);
    }
    const uint32_t now = millis();
    uint8_t out[5];
    this->stage_busy_ = this->stage_.step(this->stage_busy_ ? now - this->stage_ms_ : 0, out);
    this->stage_ms_ = now;
    this->stage_due_ms_ = now + kk::FADE_TICK_MS;
    this->write_bus_(out);
  }
  std::memcpy(this->stage_target_, target, sizeof(target));
  this->stage_have_target_ = true;
  // Only what the schedule or Off mode sends is mirrored onto the light entity: a held manual value
  // is already what the light says, and previews and effects are transient.
  if (this->mode_.mode == kk::Mode::PROGRAM || this->mode_.mode == kk::Mode::OFF)
    this->publish_to_light_(channels);
}

// While a fade is running the stage writes 25 frames a second.
void KLinkGroup::tick_stage_() {
  if (!this->stage_busy_)
    return;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->stage_due_ms_) < 0)
    return;
  uint8_t out[5];
  this->stage_busy_ = this->stage_.step(now - this->stage_ms_, out);
  this->stage_ms_ = now;
  // A fixed 40 ms cadence (the loop wakes every 16 ms); after a long stall start a fresh one.
  this->stage_due_ms_ += kk::FADE_TICK_MS;
  if (static_cast<int32_t>(now - this->stage_due_ms_) > 0)
    this->stage_due_ms_ = now + kk::FADE_TICK_MS;
  this->write_bus_(out);
}

// Normalize an RGB color to its direction: the largest component becomes 1 (white for black).
static void rgb_direction(const uint8_t rgb[3], float out[3]) {
  uint8_t peak = std::max(rgb[0], std::max(rgb[1], rgb[2]));
  for (int c = 0; c < 3; c++)
    out[c] = peak == 0 ? 1.0f : rgb[c] / static_cast<float>(peak);
}

void KLinkGroup::show_on_light_(bool on, float brightness, light::ColorMode mode, float mireds, const float rgb[3]) {
  if (this->state_ == nullptr)
    return;
  LightShown now;
  now.have = true;
  now.on = on;
  now.mode = mode;
  now.brightness = brightness;
  now.mireds = mireds;
  for (int c = 0; c < 3; c++)
    now.rgb[c] = rgb != nullptr ? rgb[c] : 0.0f;
  const LightShown &was = this->shown_;
  if (was.have && was.on == on && (!on || (std::fabs(was.brightness - brightness) < 0.002f && was.mode == mode &&
                                          (mode != light::ColorMode::COLOR_TEMPERATURE ||
                                           std::fabs(was.mireds - mireds) < 0.2f) &&
                                          (mode != light::ColorMode::RGB ||
                                           (std::fabs(was.rgb[0] - now.rgb[0]) < 0.005f &&
                                            std::fabs(was.rgb[1] - now.rgb[1]) < 0.005f &&
                                            std::fabs(was.rgb[2] - now.rgb[2]) < 0.005f)))))
    return;
  this->shown_ = now;
  auto call = this->state_->make_call();
  call.set_state(on);
  if (on) {
    call.set_brightness(brightness);
    if (mode == light::ColorMode::COLOR_TEMPERATURE)
      call.set_color_temperature(mireds);
    else if (mode == light::ColorMode::RGB)
      call.set_rgb(rgb[0], rgb[1], rgb[2]);
  }
  call.set_transition_length(0);
  call.set_save(false);
  call.perform();  // write_state() runs from the light's loop and recognizes this as our own value
}

void KLinkGroup::publish_to_light_(const uint8_t channels[5]) {
  if (this->state_ == nullptr || this->effect_active())
    return;
  const bool on = channels[0] > 0;
  // The inverse of light_brightness_(): what the light must read for the lamp to be at this level.
  const float linear = channels[0] / 255.0f;
  const float brightness =
      static_cast<BrightnessCurve>(this->out_settings_.curve) == BrightnessCurve::CIE ? kk::linear_to_perceptual(linear) : linear;
  const kk::ColorSpec *spec = this->color_spec_();
  // The Tuna Blue is always shown as a color; the Tuna Sun in whichever mode the light is in.
  const bool rgb_display = spec != nullptr && (!this->has_color_temperature() ||
                                                this->state_->remote_values.get_color_mode() == light::ColorMode::RGB);
  if (rgb_display) {
    uint8_t overlays[3] = {0, 0, 0};
    for (uint8_t i = 0; i < spec->n_overlays && i < 3; i++)
      overlays[i] = channels[spec->overlay_channel[i]];
    uint8_t rgb[3];
    kk::swatch_i(*spec, channels[1], overlays, rgb);
    float direction[3];
    rgb_direction(rgb, direction);
    this->show_on_light_(on, brightness, light::ColorMode::RGB, 0.0f, direction);
  } else if (this->has_color_temperature()) {
    // The inverse of manual_levels(): UI2 = 0 is the warm end when invert_color_ is set.
    float t = channels[1] / 255.0f;
    if (this->invert_color_)
      t = 1.0f - t;
    this->show_on_light_(on, brightness, light::ColorMode::COLOR_TEMPERATURE,
                         this->cold_mireds_ + t * (this->warm_mireds_ - this->cold_mireds_), nullptr);
  } else {
    this->show_on_light_(on, brightness, light::ColorMode::BRIGHTNESS, 0.0f, nullptr);
  }
}

// True when the light's values are the ones show_on_light_() just set, within rounding.
bool KLinkGroup::shown_matches_(const LightShown &shown, const light::LightColorValues &values) const {
  if (!shown.have || values.is_on() != shown.on)
    return false;
  if (!shown.on)
    return true;
  if (std::fabs(values.get_brightness() - shown.brightness) > 0.01f)
    return false;
  if (shown.mode == light::ColorMode::COLOR_TEMPERATURE)
    return values.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE &&
           std::fabs(values.get_color_temperature() - shown.mireds) <= 1.0f;
  if (shown.mode == light::ColorMode::RGB) {
    if (values.get_color_mode() != light::ColorMode::RGB)
      return false;
    float peak = std::max(values.get_red(), std::max(values.get_green(), values.get_blue()));
    if (peak <= 0.0f)
      return false;
    return std::fabs(values.get_red() / peak - shown.rgb[0]) <= 0.02f &&
           std::fabs(values.get_green() / peak - shown.rgb[1]) <= 0.02f &&
           std::fabs(values.get_blue() / peak - shown.rgb[2]) <= 0.02f;
  }
  return true;
}

// A standard ESPHome effect ends without telling us: notice it here, before the schedule's next tick
// shows its own value on the light. Program and Off then resume; the call that stops the effect
// repeats its last values and is not a user command; in Manual the effect's last values stay.
void KLinkGroup::loop() {
  this->tick_stage_();
  if (!this->fwd_effect_active_ || this->state_ == nullptr || this->state_->get_current_effect_index() != 0)
    return;
  this->fwd_effect_active_ = false;
  this->ended_shown_ = this->fwd_shown_;
  this->effect_ended_pending_ = true;
  this->have_sent_ = false;
  this->shown_.have = false;
}

void KLinkGroup::send_throttled_(const uint8_t channels[5]) {
  std::memcpy(this->pending_, channels, sizeof(this->pending_));
  const uint32_t now = millis();
  const uint32_t since = now - this->last_throttled_ms_;
  if (since >= kk::EFFECT_FRAME_MS) {
    this->last_throttled_ms_ = now;
    this->direct_next_ = true;  // a command or a transition's frame: shown at once, never faded again
    this->send_(this->pending_);
    return;
  }
  // Too soon after the last frame: the latest value goes out when the interval is over.
  this->set_timeout("klink_trail", kk::EFFECT_FRAME_MS - since, [this]() {
    this->last_throttled_ms_ = millis();
    this->direct_next_ = true;
    this->send_(this->pending_);
  });
}

// The five channels the light's current values ask for. In RGB mode the color is snapped to the
// nearest one the lamp can make (and, when `remember` is set, kept for the number entities).
void KLinkGroup::channels_from_light_(light::LightState *state, uint8_t channels[5], bool remember) {
  const light::LightColorValues &v = state->current_values;
  const kk::ColorSpec *spec = this->color_spec_();
  uint8_t extras[3] = {this->extras_[0], this->extras_[1], this->extras_[2]};
  uint8_t level, color;
  if (spec != nullptr && v.get_color_mode() == light::ColorMode::RGB) {
    float peak = std::max(v.get_red(), std::max(v.get_green(), v.get_blue()));
    uint8_t target[3] = {255, 255, 255};
    if (peak > 0.0f) {
      target[0] = static_cast<uint8_t>(std::lround(v.get_red() / peak * 255.0f));
      target[1] = static_cast<uint8_t>(std::lround(v.get_green() / peak * 255.0f));
      target[2] = static_cast<uint8_t>(std::lround(v.get_blue() / peak * 255.0f));
    }
    kk::SnapResult snapped = kk::nearest_i(*spec, target);
    color = snapped.clr;
    for (uint8_t i = 0; i < spec->n_overlays && i < 3; i++) {
      const uint8_t channel = spec->overlay_channel[i];
      if (channel >= 2 && channel <= 4)
        extras[channel - 2] = snapped.overlays[i];
    }
    level = static_cast<uint8_t>(std::lround(this->light_brightness_(state) * 255.0f));
    if (remember) {
      this->manual_color_ = color;
      for (int i = 0; i < 3; i++)
        this->extras_[i] = extras[i];
    }
  } else {
    this->manual_levels(state, level, color);
  }
  channels[0] = level;
  channels[1] = color;
  channels[2] = extras[0];
  channels[3] = extras[1];
  channels[4] = extras[2];
}

void KLinkGroup::forward_values_(light::LightState *state) {
  this->fwd_effect_active_ = true;
  // Remember the effect's last TARGET values (remote_values: the call that ends an effect repeats
  // those), so that call can be told from a user command. The frames themselves follow the
  // interpolated current values.
  const light::LightColorValues &v = state->remote_values;
  LightShown now;
  now.have = true;
  now.on = v.is_on();
  now.mode = v.get_color_mode();
  now.brightness = v.get_brightness();
  now.mireds = v.get_color_temperature();
  float peak = std::max(v.get_red(), std::max(v.get_green(), v.get_blue()));
  for (int c = 0; c < 3; c++) {
    const float comp = c == 0 ? v.get_red() : (c == 1 ? v.get_green() : v.get_blue());
    now.rgb[c] = peak > 0.0f ? comp / peak : 1.0f;
  }
  this->fwd_shown_ = now;
  uint8_t channels[5];
  this->channels_from_light_(state, channels, false);  // from current_values
  this->send_throttled_(channels);
}

void KLinkGroup::write_state(light::LightState *state) {
  if (this->state_ == nullptr) {
    this->state_ = state;
    this->apply_transition_();
  }
  this->state_ = state;
  // The first call is the state restored at boot, not a user command: never let it override the
  // schedule or push a level to the lamps.
  if (!this->ignored_initial_) {
    this->ignored_initial_ = true;
    return;
  }
  // While an effect runs it owns the output: the light's own brightness/color writes (which fire as
  // the effect starts) would show up as a one-frame flash between two effect frames.
  if (state->get_current_effect_index() != 0) {
    if (!this->klink_effect_running_)
      this->forward_values_(state);  // a standard ESPHome effect: its values go to the lamps
    return;
  }
  const light::LightColorValues &values = state->remote_values;
  // The light call that ends an effect repeats the values the light already had. Treat that as our
  // own write; a real user command in the same call (turn-off, new brightness) differs and falls
  // through to take manual control below.
  if (this->effect_ended_pending_) {
    this->effect_ended_pending_ = false;
    if (this->mode_.mode != kk::Mode::MANUAL && this->shown_matches_(this->ended_shown_, values))
      return;
  }
  // The value this group itself put on the light (the schedule, or a snapped color) is not a user
  // command, in every mode.
  if (this->shown_matches_(this->shown_, values))
    return;
  this->on_user_command_();
  // The light now shows the user's value, so the schedule's next value must be shown again even if it
  // equals the one shown before.
  this->shown_.have = false;
  const kk::ColorSpec *spec = this->color_spec_();
  const bool rgb_command = spec != nullptr && values.is_on() && values.get_color_mode() == light::ColorMode::RGB;
  if (rgb_command) {
    // Any color is accepted; the lamp gets the closest one it can make (CLR plus the overlay channels).
    float peak = std::max(values.get_red(), std::max(values.get_green(), values.get_blue()));
    uint8_t target[3] = {255, 255, 255};
    if (peak > 0.0f) {
      target[0] = static_cast<uint8_t>(std::lround(values.get_red() / peak * 255.0f));
      target[1] = static_cast<uint8_t>(std::lround(values.get_green() / peak * 255.0f));
      target[2] = static_cast<uint8_t>(std::lround(values.get_blue() / peak * 255.0f));
    }
    kk::SnapResult snapped = kk::nearest_i(*spec, target);
    this->manual_color_ = snapped.clr;
    for (uint8_t i = 0; i < spec->n_overlays && i < 3; i++) {
      const uint8_t channel = spec->overlay_channel[i];  // 0-based UI index, 2..4
      if (channel >= 2 && channel <= 4)
        this->extras_[channel - 2] = snapped.overlays[i];
    }
  }
  uint8_t level, color;
  this->manual_levels(state, level, color);
  uint8_t channels[5] = {level, color, this->extras_[0], this->extras_[1], this->extras_[2]};
  this->send_throttled_(channels);
  if (rgb_command) {
    this->publish_to_light_(channels);  // show the color the lamp will really make
    this->extras_cb_.call();            // and let the number entities follow
  }
}

// ---- Slot text ------------------------------------------------------------------------------

void KLinkSlotText::store(const kk::SchedulePoint &point) {
  std::string normalized = kk::format_slot(point, this->group_->slot_labels());
  this->publish_state(normalized);
  // A slider drag sends many edits; write flash only once they stop.
  this->set_timeout("save", 2000, [this, normalized]() {
    Stored stored;
    std::memset(&stored, 0, sizeof(stored));
    std::strncpy(stored.text, normalized.c_str(), sizeof(stored.text) - 1);
    this->pref_.save(&stored);
  });
}

void KLinkSlotText::setup() {
  this->group_->register_slot_text(this->index_, this);
  this->pref_ = this->make_entity_preference<Stored>();
  Stored stored;
  std::string value = this->initial_;
  if (this->pref_.load(&stored)) {
    stored.text[sizeof(stored.text) - 1] = '\0';
    value = stored.text;
  }
  this->apply_(value, false);
}

void KLinkSlotText::dump_config() {
  LOG_TEXT("", "K-Link slot", this);
  ESP_LOGCONFIG(TAG, "  Slot: %u", this->index_ + 1);
}

void KLinkSlotText::control(const std::string &value) { this->apply_(value, true); }

void KLinkSlotText::apply_(const std::string &value, bool save) {
  kk::SchedulePoint point;
  const kk::SlotLabels *labels = this->group_->slot_labels();
  if (!kk::parse_slot(value, point, labels, this->group_->channel_max())) {
    ESP_LOGW(TAG, "Slot %u: '%s' is not valid. Use \"HH:MM u1 u2 u3 u4 u5\" (UI1-2 0-255, UI3-5 0-100), a labeled form like \"HH:MM int=153 clr=89 r=25 am=0\", or \"off\".",
             this->index_ + 1, value.c_str());
    this->publish_state(this->state);  // restore the previous text in the UI
    return;
  }
  this->group_->set_slot(this->index_, point);
  std::string normalized = kk::format_slot(point, labels);
  this->publish_state(normalized);
  if (save) {
    Stored stored;
    std::memset(&stored, 0, sizeof(stored));
    std::strncpy(stored.text, normalized.c_str(), sizeof(stored.text) - 1);
    this->pref_.save(&stored);
  }
}

// ---- Point editor entities ------------------------------------------------------------------

void KLinkModeSelect::setup() {
  this->group_->add_on_mode_callback([this]() {
    if (this->group_->mode() == kk::Mode::PREVIEW)
      return;  // keep showing the last user-selectable mode
    this->publish_state(static_cast<size_t>(this->group_->mode()));
  });
  // A preview right at boot has no user-selectable mode to show yet.
  if (this->group_->mode() != kk::Mode::PREVIEW)
    this->publish_state(static_cast<size_t>(this->group_->mode()));
}

void KLinkModeSelect::dump_config() { LOG_SELECT("", "K-Link mode", this); }

void KLinkEffectSpeedSelect::setup() {
  this->publish_state(static_cast<size_t>(this->group_->effect_speed()));
}

void KLinkEffectSpeedSelect::dump_config() { LOG_SELECT("", "K-Link effect speed", this); }

void KLinkEffectDurationNumber::setup() {
  this->publish_state(static_cast<float>(this->group_->effect_duration_ms()) / 60000.0f);
}

void KLinkEffectDurationNumber::control(float value) {
  if (value < 0.0f)
    value = 0.0f;
  if (value > 5.0f)
    value = 5.0f;
  // Half-minute steps up to 5 minutes; 0 = until stopped.
  uint32_t halves = static_cast<uint32_t>(std::lround(value * 2.0f));
  this->publish_state(static_cast<float>(halves) / 2.0f);
  this->group_->set_effect_options(this->group_->effect_speed(), halves * 30000u);
}

void KLinkEffectDurationNumber::dump_config() { LOG_NUMBER("", "K-Link effect duration", this); }

// ---- Acclimation and soft start entities ------------------------------------------------------

void KLinkTankNumber::setup() {
  switch (this->kind_) {
    case TankNumberKind::ACC_START:
    case TankNumberKind::ACC_DAYS:
      this->group_->add_on_acclimation_callback([this]() { this->refresh_(); });
      break;
    case TankNumberKind::SOFT_START:
      this->group_->add_on_output_settings_callback([this]() { this->refresh_(); });
      break;
  }
  this->refresh_();
}

void KLinkTankNumber::refresh_() {
  switch (this->kind_) {
    case TankNumberKind::ACC_START:
      this->publish_state(this->group_->acclimation().start_pct);
      break;
    case TankNumberKind::ACC_DAYS:
      this->publish_state(this->group_->acclimation().days);
      break;
    case TankNumberKind::SOFT_START:
      this->publish_state(this->group_->output_settings().soft_start);
      break;
  }
}

void KLinkTankNumber::control(float value) {
  const uint8_t v = static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : std::lround(value)));
  if (this->kind_ == TankNumberKind::SOFT_START) {
    OutputSettings settings = this->group_->output_settings();
    settings.soft_start = v;
    this->group_->set_output_settings(settings);
    return;
  }
  kk::AcclimationPlan plan = this->group_->acclimation();
  if (this->kind_ == TankNumberKind::ACC_START)
    plan.start_pct = v;
  else
    plan.days = v;
  this->group_->set_acclimation(plan);
}

void KLinkTankNumber::dump_config() { LOG_NUMBER("", "K-Link tank number", this); }

void KLinkSourceSelect::setup() {
  this->publish_state(static_cast<size_t>(this->group_->source()));
  this->group_->add_on_source_callback([this]() { this->publish_state(static_cast<size_t>(this->group_->source())); });
}

void KLinkSourceSelect::dump_config() { LOG_SELECT("", "K-Link program source", this); }

void KLinkCurveSelect::setup() {
  this->publish_state(this->group_->output_settings().curve);
  this->group_->add_on_output_settings_callback(
      [this]() { this->publish_state(this->group_->output_settings().curve); });
}

void KLinkCurveSelect::control(size_t index) {
  OutputSettings settings = this->group_->output_settings();
  settings.curve = static_cast<uint8_t>(index > 1 ? 1 : index);
  this->group_->set_output_settings(settings);
}

void KLinkCurveSelect::dump_config() { LOG_SELECT("", "K-Link brightness curve", this); }

void KLinkFadeTimeNumber::setup() {
  this->publish_state(static_cast<float>(this->group_->output_settings().fade_ds) / 10.0f);
  this->group_->add_on_output_settings_callback(
      [this]() { this->publish_state(static_cast<float>(this->group_->output_settings().fade_ds) / 10.0f); });
}

void KLinkFadeTimeNumber::control(float value) {
  if (value < 0.0f)
    value = 0.0f;
  if (value > 10.0f)
    value = 10.0f;
  // Tenths of a second, matching OutputSettings::fade_ds.
  uint16_t tenths = static_cast<uint16_t>(std::lround(value * 10.0f));
  OutputSettings settings = this->group_->output_settings();
  settings.fade_ds = tenths;
  this->group_->set_output_settings(settings);
}

void KLinkFadeTimeNumber::dump_config() { LOG_NUMBER("", "K-Link fade time", this); }

void KLinkPointSelect::setup() {
  this->group_->add_on_editor_callback([this]() { this->publish_state(this->group_->selected_point()); });
  this->publish_state(this->group_->selected_point());
}

void KLinkPointSelect::dump_config() { LOG_SELECT("", "K-Link point", this); }

#ifdef USE_DATETIME_TIME
void KLinkPointTime::refresh_() {
  const kk::SchedulePoint &p = this->group_->point(this->group_->selected_point());
  this->hour_ = p.minute / 60;
  this->minute_ = p.minute % 60;
  this->second_ = 0;
  this->publish_state();
}

void KLinkPointTime::setup() {
  this->group_->add_on_editor_callback([this]() { this->refresh_(); });
  this->refresh_();
}

void KLinkPointTime::dump_config() { LOG_DATETIME_TIME("", "K-Link point time", this); }

void KLinkPointTime::control(const datetime::TimeCall &call) {
  const kk::SchedulePoint &p = this->group_->point(this->group_->selected_point());
  uint8_t hour = call.get_hour().value_or(p.minute / 60);
  uint8_t minute = call.get_minute().value_or(p.minute % 60);
  this->group_->edit_point_time(hour, minute);
}
#endif

// ---- Switch ---------------------------------------------------------------------------------

void KLinkSwitch::setup() {
  optional<bool> restored = this->get_initial_state_with_restore_mode();
  bool state = false;
  if (this->kind_ == SwitchKind::WRITES && this->hub_->auto_writes()) {
    // Auto mode requests writes itself; a restored OFF must not cancel that.
    this->hub_->add_on_writes_callback([this](bool on) { this->publish_state(on); });
    this->publish_state(this->hub_->writes_enabled());
    return;
  }
  switch (this->kind_) {
    case SwitchKind::WRITES:
      state = restored.value_or(this->hub_->writes_enabled());
      this->hub_->add_on_writes_callback([this](bool on) { this->publish_state(on); });
      break;
    case SwitchKind::COMMISSIONING:
      state = false;  // never restored: it must be switched on deliberately
      this->hub_->add_on_commissioning_callback([this](bool on) { this->publish_state(on); });
      break;
    case SwitchKind::POINT_ENABLED:
      // A view of the selected point: nothing to restore or apply.
      this->group_->add_on_editor_callback(
          [this]() { this->publish_state(this->group_->point(this->group_->selected_point()).enabled); });
      this->publish_state(this->group_->point(this->group_->selected_point()).enabled);
      return;
    case SwitchKind::FOLLOW_WEATHER:
    case SwitchKind::STORMS_FROM_WEATHER:
      state = restored.value_or(false);
      break;
    case SwitchKind::ACCLIMATION:
      // A view of the program's own (saved) acclimation plan: nothing to restore or apply.
      this->group_->add_on_acclimation_callback([this]() { this->publish_state(this->group_->acclimation().on); });
      this->publish_state(this->group_->acclimation().on);
      return;
  }
  this->apply_(state);
  // The bus guard may refuse a request to turn writes on; show the real state.
  this->publish_state(this->kind_ == SwitchKind::WRITES ? this->hub_->writes_enabled() : state);
}

void KLinkSwitch::dump_config() { LOG_SWITCH("", "K-Link switch", this); }

void KLinkSwitch::write_state(bool state) {
  this->apply_(state);
  this->publish_state(this->kind_ == SwitchKind::WRITES ? this->hub_->writes_enabled() : state);
}

void KLinkSwitch::apply_(bool state) {
  switch (this->kind_) {
    case SwitchKind::WRITES:
      this->hub_->set_writes_enabled(state);
      break;
    case SwitchKind::COMMISSIONING:
      this->hub_->set_commissioning(state);
      break;
    case SwitchKind::POINT_ENABLED:
      this->group_->edit_point_enabled(state);
      break;
    case SwitchKind::ACCLIMATION: {
      kk::AcclimationPlan plan = this->group_->acclimation();
      plan.on = state;
      this->group_->set_acclimation(plan);
      break;
    }
    case SwitchKind::FOLLOW_WEATHER:
      this->hub_->set_follow_weather(state);
      break;
    case SwitchKind::STORMS_FROM_WEATHER:
      this->hub_->set_storms_from_weather(state);
      break;
  }
}

// ---- Channel numbers ------------------------------------------------------------------------

void KLinkCloudDimmingNumber::setup() {
  this->pref_ = this->make_entity_preference<float>();
  float value = 50.0f;
  if (!this->pref_.load(&value) || std::isnan(value))
    value = 50.0f;
  this->hub_->set_max_cloud_dimming(value);
  this->publish_state(this->hub_->max_cloud_dimming());
}

void KLinkCloudDimmingNumber::control(float value) {
  this->hub_->set_max_cloud_dimming(value);
  const float stored = this->hub_->max_cloud_dimming();
  this->pref_.save(&stored);
  this->publish_state(stored);
}

void KLinkCloudDimmingNumber::dump_config() { LOG_NUMBER("", "K-Link max cloud dimming", this); }

void KLinkLocationNumber::setup() {
  this->pref_ = this->make_entity_preference<float>();
  float value = NAN;
  if (this->pref_.load(&value) && !std::isnan(value))
    this->hub_->set_entered_coordinate(this->index_, value);
  else
    value = NAN;
  this->publish_state(value);
}

void KLinkLocationNumber::control(float value) {
  const float limit = this->index_ == 0 ? 90.0f : 180.0f;
  if (value < -limit)
    value = -limit;
  if (value > limit)
    value = limit;
  this->pref_.save(&value);
  this->hub_->set_entered_coordinate(this->index_, value);
  this->publish_state(value);
}

void KLinkLocationNumber::dump_config() { LOG_NUMBER("", "K-Link location", this); }

void KLinkChannelNumber::refresh_() {
  const kk::SchedulePoint &p = this->group_->point(this->group_->selected_point());
  this->publish_state(p.channels[this->channel_ - 1]);
}

void KLinkChannelNumber::setup() {
  if (this->point_mode_) {
    this->group_->add_on_editor_callback([this]() { this->refresh_(); });
    this->refresh_();
    return;
  }
  this->pref_ = global_preferences->make_preference<float>(fnv1_hash("klink_ch_") + this->get_object_id_hash() + this->channel_);
  float value = 0;
  if (!this->pref_.load(&value))
    value = 0;
  // Restore the value without sending anything: the schedule keeps control until the user moves it.
  float max = this->channel_ == 2 ? static_cast<float>(this->group_->channel_max()[1]) : 100.0f;
  uint8_t pct = static_cast<uint8_t>(value < 0 ? 0 : (value > max ? max : value));
  this->group_->set_extra_silent(this->channel_, pct);
  this->publish_state(pct);
  // A color snapped from Home Assistant changes these numbers too.
  this->group_->add_on_extras_callback([this]() {
    float value = this->group_->extra(this->channel_);
    if (this->state != value) {
      this->publish_state(value);
      this->pref_.save(&value);
    }
  });
}

void KLinkChannelNumber::control(float value) {
  if (this->point_mode_) {
    this->group_->edit_point_channel(this->channel_ - 1, static_cast<uint8_t>(value + 0.5f));
    return;
  }
  this->publish_state(value);
  this->pref_.save(&value);
  this->group_->set_extra(this->channel_, static_cast<uint8_t>(value + 0.5f));
}

void KLinkChannelNumber::dump_config() {
  LOG_NUMBER("", "K-Link channel", this);
  if (this->point_mode_)
    ESP_LOGCONFIG(TAG, "  Selected point, channel UI%u", this->channel_);
  else
    ESP_LOGCONFIG(TAG, "  Channel: UI%u", this->channel_);
}

// ---- Effects -----------------------------------------------------------------------------------

std::vector<KLinkEffect *> &KLinkEffect::instances() {
  static std::vector<KLinkEffect *> list;
  return list;
}

KLinkEffect *KLinkEffect::from(const light::LightEffect *effect) {
  for (KLinkEffect *candidate : instances())
    if (static_cast<const light::LightEffect *>(candidate) == effect)
      return candidate;
  return nullptr;
}

uint32_t KLinkEffect::next_random_() {
  if (this->seed_ == 0)
    return random_uint32();
  // xorshift32, so lights given the same seed draw the same flashes
  this->seed_ ^= this->seed_ << 13;
  this->seed_ ^= this->seed_ >> 17;
  this->seed_ ^= this->seed_ << 5;
  return this->seed_;
}

void KLinkEffect::start() {
  light::LightEffect::start();
  this->start_ms_ = millis();
  this->last_tick_ms_ = this->start_ms_ - kk::EFFECT_FRAME_MS;  // first frame right away
  light::LightState *state = this->get_light_state();
  KLinkGroup *group = state != nullptr ? KLinkGroup::from_output(state->get_output()) : nullptr;
  this->native_ = group == nullptr;
  this->unsupported_ = false;
  // The effects take the group's runtime Speed and Time settings; the
  // YAML `speed`/`duration` values only apply while the entities have not been used.
  if (group != nullptr) {
    this->variant_ = group->effect_speed();
    this->duration_ms_ = group->effect_duration_ms();
  } else if (state != nullptr) {
    // An ordinary ESPHome light: shown through what it can do. A program that drives it brings its own Speed
    // and Time settings.
    const light::LightTraits traits = state->get_traits();
    this->fx_light_ = kk::fx_light_for_caps(light_caps_of(traits));
    this->range_.min_mireds = traits.get_min_mireds();
    this->range_.max_mireds = traits.get_max_mireds();
    this->program_ = KLinkLightProgram::for_light(state);
    if (this->program_ != nullptr) {
      this->variant_ = this->program_->effect_speed();
      this->duration_ms_ = this->program_->effect_duration_ms();
      this->program_->note_effect_started(this->duration_ms_);
    }
    if (!kk::fx_offered(static_cast<kk::EffectKind>(this->kind_), this->fx_light_)) {
      this->unsupported_ = true;
      ESP_LOGW(TAG, "Effect '%s' shows nothing on light '%s' (it has no color control for it)",
               this->get_name().c_str(), state->get_name().c_str());
    }
  }
  this->engine_.start(static_cast<kk::EffectKind>(this->kind_), this->variant_);
  if (group != nullptr)
    group->note_effect_started(this->duration_ms_, this->kind_ != FxKind::STORM);
}

void KLinkEffect::stop() {
  light::LightEffect::stop();
  light::LightState *state = this->get_light_state();
  if (state == nullptr)
    return;
  KLinkGroup *group = KLinkGroup::from_output(state->get_output());
  if (group != nullptr)
    group->effect_ended_();
}

void KLinkEffect::finish_() {
  // Every effect ends by clearing itself, whatever its kind: the group then resumes its mode
  // (Program shows the schedule again, Off stays off, Manual returns to the held level). No effect
  // turns the light off, not even Sunset.
  auto call = this->get_light_state()->make_call();
  call.set_effect(static_cast<uint32_t>(0));
  call.set_save(false);
  call.perform();
}

void KLinkEffect::apply() {
  const uint32_t now = millis();
  if (now - this->last_tick_ms_ < kk::EFFECT_FRAME_MS)
    return;
  // Frames are due every 40 ms on a fixed cadence. The loop wakes every 16 ms, so counting from "now" would drift
  // to about 20 a second; after a long stall start a fresh cadence instead of catching up.
  this->last_tick_ms_ += kk::EFFECT_FRAME_MS;
  if (now - this->last_tick_ms_ >= kk::EFFECT_FRAME_MS)
    this->last_tick_ms_ = now;
  const uint32_t elapsed = now - this->start_ms_;
  if (this->unsupported_ || (this->duration_ms_ != 0 && elapsed >= this->duration_ms_)) {
    this->finish_();
    return;
  }
  if (this->native_) {
    this->apply_native_(elapsed);
    return;
  }
  // The light output is a K-Link group. The engine drives all five channels itself.
  KLinkGroup *group = KLinkGroup::from_output(this->get_light_state()->get_output());
  uint8_t out[5] = {0, 0, 0, 0, 0};
  this->engine_.step(elapsed, out);
  group->send_effect_frame(out);
}

void KLinkEffect::apply_native_(uint32_t elapsed) {
  uint8_t out[5] = {0, 0, 0, 0, 0};
  this->engine_.step(elapsed, out);
  const kk::LightValues v =
      kk::fx_light_values(static_cast<kk::EffectKind>(this->kind_), this->fx_light_, out, this->range_);
  // Like ESPHome's own effects: no publish (Home Assistant sees the effect, not 25 states a second), no save, no
  // fade. The light stays on and a dark frame is brightness 0: a call that turns the light off would end the effect.
  auto call = this->get_light_state()->make_call();
  call.set_state(true);
  call.set_brightness(v.on ? v.brightness : 0.0f);
  if (this->fx_light_ == kk::FxLight::CT)
    call.set_color_temperature(v.mireds);
  else if (this->fx_light_ == kk::FxLight::RGB)
    call.set_rgb(v.r, v.g, v.b);
  call.set_transition_length(0);
  call.set_publish(false);
  call.set_save(false);
  call.perform();
}

// ---- State text sensor ------------------------------------------------------------------------

void KLinkStateSensor::update() {
  std::string text = this->hub_->state_text();
  if (!this->has_state() || this->state != text)
    this->publish_state(text);
}

void KLinkStateSensor::dump_config() { LOG_TEXT_SENSOR("", "K-Link state", this); }

// ---- Group status text sensor ---------------------------------------------------------------

void KLinkGroupStatus::update() {
  std::string text = this->group_->status_text();
  if (!this->have_last_ || this->last_ != text) {
    this->last_ = text;
    this->have_last_ = true;
    this->publish_state(text);
  }
}

void KLinkGroupStatus::dump_config() { LOG_TEXT_SENSOR("", "K-Link group status", this); }

// ---- Button ---------------------------------------------------------------------------------

void KLinkButton::dump_config() { LOG_BUTTON("", "K-Link button", this); }

void KLinkButton::press_action() {
  switch (this->kind_) {
    case ButtonKind::SCAN:
      this->hub_->start_scan();
      break;
    case ButtonKind::IDENTIFY_NEXT:
      this->hub_->identify_next();
      break;
    case ButtonKind::RETRY:
      this->hub_->retry();
      break;
  }
}

// ---- Sensor ---------------------------------------------------------------------------------

void KLinkSensor::dump_config() { LOG_SENSOR("", "K-Link sensor", this); }

void KLinkSensor::update() {
  float value = 0;
  switch (this->kind_) {
    case SensorKind::LAMPS:
      value = static_cast<float>(this->hub_->lamp_count());
      break;
    case SensorKind::BUS_TIMEOUTS:
      value = static_cast<float>(this->hub_->bus_timeouts());
      break;
    case SensorKind::FRAMES:
      value = static_cast<float>(this->hub_->frames_seen());
      break;
    case SensorKind::BAD_FRAMES:
      value = static_cast<float>(this->hub_->bad_frames());
      break;
    case SensorKind::SHADOW_MATCHES:
      value = static_cast<float>(this->hub_->shadow_matches());
      break;
    case SensorKind::SHADOW_MISMATCHES:
      value = static_cast<float>(this->hub_->shadow_mismatches());
      break;
    case SensorKind::FOREIGN_FRAMES:
      value = static_cast<float>(this->hub_->foreign_frames());
      break;
    case SensorKind::OTHER_CONTROLLER:
      value = this->hub_->other_controller_recent() ? 1.0f : 0.0f;
      break;
    case SensorKind::CONFLICT_LATCHED:
      value = this->hub_->conflict_latched() ? 1.0f : 0.0f;
      break;
  }
  this->publish_state(value);
}

}  // namespace klink
}  // namespace esphome
