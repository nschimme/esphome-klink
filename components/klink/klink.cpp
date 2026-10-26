#include "klink.h"

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
      ESP_LOGI(TAG, "bus: %s  [%s]", format_hex_pretty(frame.data(), frame.size()).c_str(),
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
    if (ok && kk::parse_identity(reply, id) && this->inventory_.upsert(kid, id.gid, id.type)) {
      this->scan_found_++;
      ESP_LOGI(TAG, "Lamp KID %u: GID %u, type %u", kid, id.gid, id.type);
      this->lamp_found_cb_.call(kid, id.gid, id.type);
    } else {
      ESP_LOGW(TAG, "Lamp KID %u did not return a valid identity", kid);
    }
    this->bus_.enqueue(kk::mute(kid), kk::CMD_MUTE, kk::REPLY_TIMEOUT_MS,
                       [this](bool, const std::vector<uint8_t> &) { this->read_next_identity_(); });
  });
}

void KLinkHub::identify(uint32_t kid) {
  ESP_LOGI(TAG, "Identify KID %u", kid);
  this->bus_.enqueue(kk::find(kid), 0, 0, nullptr);
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

void KLinkGroup::setup() {
  this->hub_->register_group(this);
  this->hub_->register_program(this);
  this->setup_program_();
  // Effect speed and duration: runtime settings, like the Kessil app's sliders.
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
  this->set_interval("schedule", 1000, [this]() { this->update(); });
}

void KLinkGroup::dump_config() {
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_ == GroupModel::TUNA_SUN ? "Tuna Sun" : (this->model_ == GroupModel::TUNA_BLUE ? "Tuna Blue (provisional)" : "generic"));
  ESP_LOGCONFIG(TAG, "K-Link group:");
  ESP_LOGCONFIG(TAG, "  GID: %u", this->gid_);
  ESP_LOGCONFIG(TAG, "  Slots: %u", static_cast<unsigned>(this->slots_.size()));
  ESP_LOGCONFIG(TAG, "  Mode: %s", kk::mode_name(this->mode_.mode));
  ESP_LOGCONFIG(TAG, "  Manual color (UI2): %u", this->manual_color_);
}

light::LightTraits KLinkGroup::get_traits() {
  light::LightTraits traits;
  if (this->has_color_temperature()) {
    // The Tuna Sun: exact color temperature, or any colour snapped to what the lamp can make.
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE, light::ColorMode::RGB});
    traits.set_min_mireds(this->cold_mireds_);
    traits.set_max_mireds(this->warm_mireds_);
  } else if (this->color_spec_() != nullptr) {
    traits.set_supported_color_modes({light::ColorMode::RGB});  // the Tuna Blue: snapped to the nearest colour
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
    default:
      return nullptr;
  }
}

void KLinkGroup::set_extra(uint8_t channel, uint8_t value) {
  if (channel == 2) {
    if (this->has_color_temperature())
      return;  // UI2 is the light's color temperature on this model
    this->manual_color_ = value;
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
  this->send_(channels);
  this->publish_to_light_(channels);  // the colour the light reports follows the numbers
}

void KLinkGroup::manual_levels(light::LightState *state, uint8_t &level, uint8_t &color) {
  if (!this->has_color_temperature() || state->remote_values.get_color_mode() == light::ColorMode::RGB) {
    // Brightness only: UI1 follows the light and UI2 is whatever its number entity (or manual_color) says.
    float brightness;
    state->current_values_as_brightness(&brightness);
    level = static_cast<uint8_t>(std::lround(brightness * 255.0f));
    color = this->manual_color_;
    return;
  }
  // Brightness is UI1. The light reports color temperature already normalised between the
  // advertised ends (0 = cold, 1 = warm); that maps linearly onto UI2.
  float t, brightness;
  state->current_values_as_ct(&t, &brightness);
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

void KLinkGroup::output_(const uint8_t channels[5]) {
  this->hub_->write_group(this->gid_, channels);
  // Only what the schedule or Off mode sends is mirrored onto the light entity: a held manual value
  // is already what the light says, and previews and effects are transient.
  if (this->mode_.mode == kk::Mode::PROGRAM || this->mode_.mode == kk::Mode::OFF)
    this->publish_to_light_(channels);
}

// Normalise an RGB colour to its direction: the largest component becomes 1 (white for black).
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
  call.perform();  // write_state() runs from the light's loop and recognises this as our own value
}

void KLinkGroup::publish_to_light_(const uint8_t channels[5]) {
  if (this->state_ == nullptr || this->effect_active())
    return;
  const bool on = channels[0] > 0;
  const float brightness = channels[0] / 255.0f;
  const kk::ColorSpec *spec = this->color_spec_();
  // The Tuna Blue is always shown as a colour; the Tuna Sun in whichever mode the light is in.
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

void KLinkGroup::write_state(light::LightState *state) {
  this->state_ = state;
  // The first call is the state restored at boot, not a user command: never let it override the
  // schedule or push a level to the lamps.
  if (!this->ignored_initial_) {
    this->ignored_initial_ = true;
    return;
  }
  // While an effect runs it owns the output: the light's own brightness/color writes (which fire as
  // the effect starts) would show up as a one-frame flash between two effect frames.
  if (state->get_current_effect_index() != 0)
    return;
  const light::LightColorValues &values = state->remote_values;
  // The light call that ends an effect repeats the values the light already had. Treat that as our
  // own write; a real user command in the same call (turn-off, new brightness) differs and falls
  // through to take manual control below.
  if (this->effect_ended_pending_) {
    this->effect_ended_pending_ = false;
    if (this->mode_.mode != kk::Mode::MANUAL && this->shown_matches_(this->ended_shown_, values))
      return;
  }
  // The value this group itself put on the light (the schedule, or a snapped colour) is not a user
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
    // Any colour is accepted; the lamp gets the closest one it can make (CLR plus the overlay channels).
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
  this->send_(channels);
  if (rgb_command) {
    this->publish_to_light_(channels);  // show the colour the lamp will really make
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
  if (!kk::parse_slot(value, point, labels)) {
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
  // Half-minute steps, like the Kessil app's Time slider; 0 = until stopped.
  uint32_t halves = static_cast<uint32_t>(std::lround(value * 2.0f));
  this->publish_state(static_cast<float>(halves) / 2.0f);
  this->group_->set_effect_options(this->group_->effect_speed(), halves * 30000u);
}

void KLinkEffectDurationNumber::dump_config() { LOG_NUMBER("", "K-Link effect duration", this); }

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
  }
}

// ---- Channel numbers ------------------------------------------------------------------------

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
  float max = this->channel_ == 2 ? 255.0f : 100.0f;
  uint8_t pct = static_cast<uint8_t>(value < 0 ? 0 : (value > max ? max : value));
  this->group_->set_extra_silent(this->channel_, pct);
  this->publish_state(pct);
  // A colour snapped from Home Assistant changes these numbers too.
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

void KLinkEffect::start() {
  light::LightEffect::start();
  this->start_ms_ = millis();
  this->last_tick_ms_ = this->start_ms_ - kk::EFFECT_TICK_MS;  // first tick right away
  light::LightState *state = this->get_light_state();
  KLinkGroup *group = state != nullptr ? static_cast<KLinkGroup *>(state->get_output()) : nullptr;
  if (this->kind_ <= FxKind::MOON_BLUE) {
    // The Dongle effects take the group's runtime Speed and Time settings, like the Kessil app's
    // sliders; the YAML `speed`/`duration` values only apply while the entities have not been used.
    if (group != nullptr) {
      this->variant_ = group->effect_speed();
      this->duration_ms_ = group->effect_duration_ms();
    }
    this->engine_.start(static_cast<kk::EffectKind>(this->kind_), this->variant_);
  }
  if (group != nullptr)
    group->note_effect_started(this->duration_ms_);
}

void KLinkEffect::stop() {
  light::LightEffect::stop();
  light::LightState *state = this->get_light_state();
  if (state == nullptr)
    return;
  KLinkGroup *group = static_cast<KLinkGroup *>(state->get_output());
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
  uint32_t now = millis();
  if (now - this->last_tick_ms_ < kk::EFFECT_TICK_MS)
    return;
  this->last_tick_ms_ = now;
  uint32_t elapsed = now - this->start_ms_;
  bool timed = this->duration_ms_ != 0;
  if (timed && elapsed >= this->duration_ms_) {
    this->finish_();
    return;
  }
  // The light output is a K-Link group: effects are only registered for klink lights.
  KLinkGroup *group = static_cast<KLinkGroup *>(this->get_light_state()->get_output());
  uint8_t level, color;
  group->manual_levels(this->get_light_state(), level, color);
  uint8_t out[5] = {0, 0, 0, 0, 0};

  if (this->kind_ <= FxKind::MOON_BLUE) {
    // The Dongle's own effects; the engine drives UI1-UI5 itself.
    this->engine_.step(elapsed, out);
  } else {
    uint32_t period = this->period_ms_ == 0 ? 1 : this->period_ms_;
    // triangle 0..1..0 over one period
    float phase = static_cast<float>(elapsed % period) / static_cast<float>(period);
    float tri = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
    switch (this->kind_) {
      case FxKind::PULSE: {
        float f = this->min_fraction_ + (1.0f - this->min_fraction_) * tri;
        out[0] = static_cast<uint8_t>(std::lround(level * f));
        out[1] = color;
        break;
      }
      case FxKind::COLOR_SWEEP:
        out[0] = level;
        out[1] = static_cast<uint8_t>(std::lround(tri * 255.0f));
        break;
      case FxKind::SUNRISE:
      case FxKind::SUNSET: {
        float done = timed ? static_cast<float>(elapsed) / static_cast<float>(this->duration_ms_) : 1.0f;
        float f = this->kind_ == FxKind::SUNRISE ? done : 1.0f - done;
        out[0] = static_cast<uint8_t>(std::lround(level * f));
        out[1] = color;
        break;
      }
      default:
        break;
    }
  }
  // The light-driven effects leave UI3-UI5 to the number entities; a Dongle effect owns them.
  if (this->kind_ >= FxKind::PULSE) {
    out[2] = group->extra(3);
    out[3] = group->extra(4);
    out[4] = group->extra(5);
  }
  group->send_effect_frame(out);
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
