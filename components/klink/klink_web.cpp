#include "klink_web.h"
#ifdef USE_KLINK_WEB

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

#include "klink_http.h"
#include "klink_light_map.h"

#if defined(USE_ESP32)
#ifndef HTTPD_400
#define HTTPD_400 "400 Bad Request"
#endif
#ifndef HTTPD_405
#define HTTPD_405 "405 Method Not Allowed"
#endif
#endif

namespace esphome {
namespace klink {

static const char *const TAG = "klink.web";

namespace kk = kessil::klink;

static bool klink_path_prefix(const char *path) {
  return std::strncmp(path, "/klink", 6) == 0 && (path[6] == '\0' || path[6] == '/');
}

static const char *mode_json_name(kk::Mode mode) {
  switch (mode) {
    case kk::Mode::PROGRAM:
      return "program";
    case kk::Mode::MANUAL:
      return "manual";
    case kk::Mode::OFF:
      return "off";
    case kk::Mode::PREVIEW:
      return "preview";
  }
  return "";
}

static bool is_post_route(const char *path) {
  return std::strcmp(path, "/klink/mode") == 0 || std::strcmp(path, "/klink/identify") == 0 ||
         std::strcmp(path, "/klink/point") == 0 || std::strcmp(path, "/klink/preview") == 0 ||
         std::strcmp(path, "/klink/preview/done") == 0 || std::strcmp(path, "/klink/assign") == 0 ||
         std::strcmp(path, "/klink/name") == 0 || std::strcmp(path, "/klink/groupname") == 0 ||
         std::strcmp(path, "/klink/copy") == 0 ||
         std::strcmp(path, "/klink/retry") == 0 || std::strcmp(path, "/klink/writes") == 0 || std::strcmp(path, "/klink/scan") == 0 ||
         std::strcmp(path, "/klink/effect") == 0 || std::strcmp(path, "/klink/effect/stop") == 0 ||
         std::strcmp(path, "/klink/output") == 0 || std::strcmp(path, "/klink/source") == 0 ||
         std::strcmp(path, "/klink/sky") == 0 || std::strcmp(path, "/klink/source/copy") == 0 ||
         std::strcmp(path, "/klink/acclimation") == 0;
}

// GET-only routes. A POST to one of these answers 405, matching how a GET on a POST route does.
static bool is_get_route(const char *path) {
  return std::strcmp(path, "/klink") == 0 || std::strcmp(path, "/klink/state") == 0 ||
         std::strcmp(path, "/klink/snap") == 0 || std::strcmp(path, "/klink/generated") == 0;
}

// `request->arg()` returns std::string on the IDF server and an Arduino String on the Arduino one;
// normalize to std::string for the JSON building below.
static std::string arg_string(AsyncWebServerRequest *request, const char *name) {
#if defined(USE_ESP32)
  return request->arg(name);
#else
  const auto &value = request->arg(name);
  return std::string(value.c_str(), value.length());
#endif
}

static void append_quoted(std::string &out, const std::string &value) {
  out += '"';
  out += json_escape(value);
  out += '"';
}

static void send_json(AsyncWebServerRequest *request, int code, const std::string &body) {
#if defined(USE_ESP32)
  if (code == 400 || code == 405) {
    // The IDF wrapper only passes 200/404/409 through verbatim and maps every other status to 500,
    // so emit 400/405 through the raw API.
    httpd_resp_set_status(*request, code == 405 ? HTTPD_405 : HTTPD_400);
    httpd_resp_set_type(*request, "application/json");
    httpd_resp_set_hdr(*request, "Accept-Ranges", "none");
    httpd_resp_send(*request, body.c_str(), HTTPD_RESP_USE_STRLEN);
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(code, "application/json", body);
  response->addHeader("cache-control", "no-store");
  request->send(response);
#else
  // The Arduino server accepts arbitrary status codes directly and has no std::string overload.
  request->send(code, "application/json", body.c_str());
#endif
}

static void send_error(AsyncWebServerRequest *request, int code, const std::string &message) {
  std::string body;
  body.reserve(message.size() + 16);
  body += "{\"error\":\"";
  body += json_escape(message);
  body += "\"}";
  send_json(request, code, body);
}

void KLinkWeb::setup() {
  // Make sure the shared server exists even without the full web_server: component; init() is
  // ref-counted, so a web_server that also calls it only adds a reference.
  this->base_->init();
  this->base_->add_handler(this);
}

void KLinkWeb::dump_config() {
  ESP_LOGCONFIG(TAG, "K-Link web:");
  ESP_LOGCONFIG(TAG, "  Page and API: /klink");
}

bool KLinkWeb::canHandle(AsyncWebServerRequest *request) const {
#ifdef USE_ESP32
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buf);
#else
  const auto &url = request->url();
#endif
  return klink_path_prefix(url.c_str());
}

void KLinkWeb::handleRequest(AsyncWebServerRequest *request) {
#ifdef USE_ESP32
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buf);
#else
  const auto &url = request->url();
#endif
  const char *path = url.c_str();

  if (request->method() == HTTP_GET) {
    if (std::strcmp(path, "/klink") == 0) {
      this->handle_index(request);
      return;
    }
    if (std::strcmp(path, "/klink/state") == 0) {
      this->handle_state(request);
      return;
    }
    if (std::strcmp(path, "/klink/snap") == 0) {
      this->handle_snap(request);
      return;
    }
    if (std::strcmp(path, "/klink/generated") == 0) {
      this->handle_generated(request);
      return;
    }
    if (is_post_route(path)) {
      send_error(request, 405, "method not allowed");
      return;
    }
    send_error(request, 404, "not found");
    return;
  }

  if (request->method() == HTTP_POST) {
    if (std::strcmp(path, "/klink/mode") == 0) {
      this->handle_mode(request);
      return;
    }
    if (std::strcmp(path, "/klink/effect") == 0) {
      this->handle_effect(request);
      return;
    }
    if (std::strcmp(path, "/klink/output") == 0) {
      this->handle_output(request);
      return;
    }
    if (std::strcmp(path, "/klink/acclimation") == 0) {
      this->handle_acclimation(request);
      return;
    }
    if (std::strcmp(path, "/klink/source") == 0) {
      this->handle_source(request);
      return;
    }
    if (std::strcmp(path, "/klink/sky") == 0) {
      this->handle_sky(request);
      return;
    }
    if (std::strcmp(path, "/klink/source/copy") == 0) {
      this->handle_source_copy(request);
      return;
    }
    if (std::strcmp(path, "/klink/effect/stop") == 0) {
      this->handle_effect_stop(request);
      return;
    }
    if (std::strcmp(path, "/klink/identify") == 0) {
      this->handle_identify(request);
      return;
    }
    if (std::strcmp(path, "/klink/point") == 0) {
      this->handle_point(request);
      return;
    }
    if (std::strcmp(path, "/klink/preview") == 0) {
      this->handle_preview(request);
      return;
    }
    if (std::strcmp(path, "/klink/preview/done") == 0) {
      this->handle_preview_done(request);
      return;
    }
    if (std::strcmp(path, "/klink/assign") == 0) {
      this->handle_assign(request);
      return;
    }
    if (std::strcmp(path, "/klink/name") == 0) {
      this->handle_name(request);
      return;
    }
    if (std::strcmp(path, "/klink/groupname") == 0) {
      this->handle_group_name(request);
      return;
    }
    if (std::strcmp(path, "/klink/copy") == 0) {
      this->handle_copy(request);
      return;
    }
    if (std::strcmp(path, "/klink/retry") == 0) {
      this->handle_retry(request);
      return;
    }
    if (std::strcmp(path, "/klink/writes") == 0) {
      this->handle_writes(request);
      return;
    }
    if (std::strcmp(path, "/klink/scan") == 0) {
      this->handle_scan(request);
      return;
    }
    if (is_get_route(path)) {
      send_error(request, 405, "method not allowed");
      return;
    }
    send_error(request, 404, "not found");
    return;
  }

  send_error(request, 405, "method not allowed");
}

void KLinkWeb::handle_index(AsyncWebServerRequest *request) {
#ifndef USE_ESP8266
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", KLINK_PAGE, KLINK_PAGE_LEN);
#else
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", KLINK_PAGE, KLINK_PAGE_LEN);
#endif
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
}

ProgramHost *KLinkWeb::find_group(uint32_t gid) {
  if (this->hub_ == nullptr)
    return nullptr;
  for (auto *group : this->hub_->programs())
    if (group->gid() == gid)
      return group;
  return nullptr;
}

std::string KLinkWeb::lamp_name(uint32_t kid) {
  const char *name = this->hub_ != nullptr ? this->hub_->names().get(kid) : nullptr;
  return name != nullptr ? name : "";
}

bool KLinkWeb::read_uint_arg(AsyncWebServerRequest *request, const char *name, uint32_t max, uint32_t &out) {
  if (!request->hasArg(name)) {
    send_error(request, 400, std::string("missing parameter: ") + name);
    return false;
  }
  if (!parse_uint(arg_string(request, name), max, out)) {
    send_error(request, 400, std::string("invalid parameter: ") + name);
    return false;
  }
  return true;
}

// A program route accepts both id kinds: a bus group (1-40) or a light program (101-164).
bool KLinkWeb::read_program_gid(AsyncWebServerRequest *request, const char *name, uint32_t &out) {
  if (!this->read_uint_arg(request, name, kk::PROGRAM_ID_MAX, out))
    return false;
  if (!kk::is_bus_gid(out) && !kk::is_program_id(out)) {
    send_error(request, 400, std::string(name) + " must be 1-40 or 101-164");
    return false;
  }
  return true;
}

// A route that needs real lamps on the bus: only bus group ids qualify, never a program id.
bool KLinkWeb::read_lamp_gid(AsyncWebServerRequest *request, const char *name, uint32_t &out) {
  if (!this->read_uint_arg(request, name, kk::PROGRAM_ID_MAX, out))
    return false;
  if (kk::is_program_id(out)) {
    send_error(request, 400, "not a lamp group");
    return false;
  }
  if (!kk::is_bus_gid(out)) {
    send_error(request, 400, std::string(name) + " must be 1-40");
    return false;
  }
  return true;
}

bool KLinkWeb::read_bool_arg(AsyncWebServerRequest *request, const char *name, bool &out) {
  if (!request->hasArg(name)) {
    send_error(request, 400, std::string("missing parameter: ") + name);
    return false;
  }
  const std::string value = arg_string(request, name);
  if (value == "1" || value == "true") {
    out = true;
    return true;
  }
  if (value == "0" || value == "false") {
    out = false;
    return true;
  }
  send_error(request, 400, std::string("invalid parameter: ") + name);
  return false;
}

void KLinkWeb::send_ok(AsyncWebServerRequest *request) { send_json(request, 200, "{\"ok\":true}"); }

// One slot in the state JSON: the same shape for "slots" and "defaults".
static void append_slot_json(std::string &out, const kk::SchedulePoint &p) {
  out += "{\"minute\":";
  out += std::to_string(p.minute);
  out += ",\"ch\":[";
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
    if (c)
      out += ',';
    out += std::to_string(p.channels[c]);
  }
  out += "],\"enabled\":";
  out += p.enabled ? "true" : "false";
  out += '}';
}

void KLinkWeb::handle_state(AsyncWebServerRequest *request) {
  std::string out;
  out.reserve(512);
  out += "{\"groups\":[";
  bool first_group = true;
  for (auto *group : this->hub_->programs()) {
    if (!first_group)
      out += ',';
    first_group = false;

    out += "{\"gid\":";
    out += std::to_string(group->gid());
    out += ",\"kind\":";
    append_quoted(out, group->kind());
    out += ",\"name\":";
    const char *nickname = this->hub_->group_names().get(group->gid());
    append_quoted(out, nickname != nullptr ? std::string(nickname) : group->name());
    out += ",\"default_name\":";
    append_quoted(out, group->name());
    out += ",\"model\":";
    append_quoted(out, group->model_name());
    out += ",\"mode\":";
    append_quoted(out, mode_json_name(group->mode()));
    out += ",\"status\":";
    append_quoted(out, group->status_text());
    // Effects are a temporary overlay: the names the light offers, the one running (if any) and the
    // runtime Speed/Time settings.
    out += ",\"effects\":[";
    const std::vector<std::string> effect_names = group->effect_names();
    for (size_t i = 0; i < effect_names.size(); i++) {
      if (i)
        out += ',';
      append_quoted(out, effect_names[i]);
    }
    out += "],\"effect\":";
    const std::string effect_label = group->effect_label();
    if (effect_label.empty()) {
      out += "null";
    } else {
      out += "{\"name\":";
      append_quoted(out, effect_label);
      out += ",\"remaining\":";
      out += std::to_string(group->effect_remaining_seconds());
      out += '}';
    }
    out += ",\"effect_speed\":";
    out += std::to_string(group->effect_speed());
    out += ",\"effect_minutes\":";
    char minutes[16];
    std::snprintf(minutes, sizeof(minutes), "%.1f", group->effect_duration_ms() / 60000.0f);
    out += minutes;
    if (group->has_output_options()) {
      const OutputSettings o = group->output_settings();
      char seconds[16];
      std::snprintf(seconds, sizeof(seconds), "%.1f", o.fade_ds / 10.0f);
      out += ",\"output\":{\"curve\":";
      out += std::to_string(o.curve);
      out += ",\"seconds\":";
      out += seconds;
      out += ",\"soft_start\":";
      out += std::to_string(o.soft_start);
      out += '}';
    }
    {
      // The acclimation ramp, for every program: on or off, its start share and length, and the share right now.
      const kk::AcclimationPlan a = group->acclimation();
      out += ",\"acclimation\":{\"on\":";
      out += a.on ? "true" : "false";
      out += ",\"start\":" + std::to_string(a.start_pct) + ",\"days\":" + std::to_string(a.days) + ",\"share\":" +
             std::to_string(static_cast<int>(std::lround(group->acclimation_share() * 100.0f))) + "}";
    }
    if (group->has_sources()) {
      const SkySettings k = group->sky_settings();
      out += ",\"source\":";
      out += std::to_string(static_cast<int>(group->source()));
      out += group->can_moon() ? ",\"can_moon\":true" : ",\"can_moon\":false";
      out += ",\"source_note\":";
      append_quoted(out, group->source_note());
      out += ",\"sky\":{\"layers\":" + std::to_string(k.layers) + ",\"peak\":" + std::to_string(k.peak) +
             ",\"hours\":" + std::to_string(k.hours) + ",\"glow\":" + std::to_string(k.glow) +
             ",\"amber\":" + std::to_string(k.amber) + ",\"ramp\":" + std::to_string(k.ramp * 10) +
             ",\"shift\":" + std::to_string(k.shift) + ",\"moon\":" + std::to_string(k.moon) +
             ",\"phase\":" + std::to_string(k.phase) + "}";
      // Today's generated day, read-only; absent while My points runs the lamps.
      const std::vector<kk::SchedulePoint> &gen = group->generated_points();
      const size_t gen_count = gen.size() > 16 ? 16 : gen.size();
      if (gen_count > 0) {
        out += ",\"generated\":[";
        for (size_t i = 0; i < gen_count; i++) {
          if (i)
            out += ',';
          append_slot_json(out, gen[i]);
        }
        out += ']';
      }
    }
    // Channel names and limits for the program editor; null marks a channel the model does not have.
    out += ",\"labels\":[";
    for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
      if (c)
        out += ',';
      const char *label = group->slot_labels()->name[c];
      if (label == nullptr)
        out += "null";
      else
        append_quoted(out, label);
    }
    out += "],\"max\":[";
    for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
      if (c)
        out += ',';
      out += std::to_string(group->channel_max()[c]);
    }
    out += ']';
    out += ",\"slots\":[";
    for (size_t i = 0; i < group->num_slots(); i++) {
      if (i)
        out += ',';
      append_slot_json(out, group->point(i));
    }
    // The shipped program, so the page can offer "Default" after any edit.
    out += "],\"defaults\":[";
    for (size_t i = 0; i < group->num_slots(); i++) {
      if (i)
        out += ',';
      kk::SchedulePoint p;
      group->default_point(i, p);
      append_slot_json(out, p);
    }
    out += "],\"lamps\":[";
    // A light program drives ESPHome lights, not lamps on the bus, so its lamp list is empty.
    if (!kk::is_program_id(group->gid())) {
      bool first_lamp = true;
      for (const auto &lamp : this->hub_->lamps()) {
        if (lamp.gid != group->gid())
          continue;
        if (!first_lamp)
          out += ',';
        first_lamp = false;
        out += std::to_string(lamp.kid);
      }
    }
    out += "],\"now_minute\":";
    out += std::to_string(group->now_minute());
    out += ",\"selected\":";
    out += std::to_string(group->selected_point());
    out += '}';
  }
  out += "],\"lamps\":[";
  bool first_lamp = true;
  for (const auto &lamp : this->hub_->lamps()) {
    if (!first_lamp)
      out += ',';
    first_lamp = false;
    out += "{\"kid\":";
    out += std::to_string(lamp.kid);
    out += ",\"type\":";
    out += std::to_string(lamp.type);
    out += ",\"gid\":";
    out += std::to_string(lamp.gid);
    out += ",\"default_gid\":";
    out += std::to_string(kk::default_gid(lamp.type));
    out += ",\"name\":";
    append_quoted(out, lamp_name(lamp.kid));
    out += ",\"model\":";
    append_quoted(out, lamp.model);
    out += ",\"firmware\":";
    if (lamp.has_firmware)
      append_quoted(out, std::to_string(lamp.fw_major) + "." + std::to_string(lamp.fw_minor) + "." +
                             std::to_string(lamp.fw_patch));
    else
      out += "null";
    out += '}';
  }
  out += "],\"followers\":[";
  bool first_local = true;
  for (auto *follower : this->hub_->followers()) {
    if (follower == nullptr)
      continue;
    if (!first_local)
      out += ',';
    first_local = false;
    out += "{\"id\":";
    append_quoted(out, follower->key());
    out += ",\"kid\":";
    out += std::to_string(this->hub_->follower_kid(follower->key()));
    out += ",\"gid\":";
    out += std::to_string(follower->gid());
    out += ",\"name\":";
    append_quoted(out, follower->name());
    out += ",\"model\":";
    append_quoted(out, follower->model_name());
    out += ",\"target\":";
    append_quoted(out, follower->target_model_name());
    out += ",\"lights\":[";
    const std::vector<std::string> light_names = follower->light_names();
    for (size_t i = 0; i < light_names.size(); i++) {
      if (i)
        out += ',';
      append_quoted(out, light_names[i]);
    }
    out += "]}";
  }
  {
    // The device's location, rounded to 0.01 degrees (about a kilometer): enough for the sky, and the page is
    // open to the local network.
    double lat = 0.0, lon = 0.0;
    out += "],\"location\":";
    if (this->hub_->location(lat, lon)) {
      char loc[96];
      std::snprintf(loc, sizeof(loc), "{\"lat\":%.2f,\"lon\":%.2f,\"source\":", std::round(lat * 100.0) / 100.0,
                    std::round(lon * 100.0) / 100.0);
      out += loc;
      append_quoted(out, this->hub_->location_source());
      out += '}';
    } else {
      out += "null";
    }
  }
  out += ",\"bus\":{\"state\":";
  append_quoted(out, this->hub_->state_text());
  out += ",\"role\":";
  append_quoted(out, this->hub_->is_follower() ? "follower" : "leader");
  out += ",\"writes\":";
  out += this->hub_->writes_enabled() ? "true" : "false";
  out += ",\"want_writes\":";
  out += this->hub_->want_writes() ? "true" : "false";
  out += ",\"known_lamps\":";
  out += std::to_string(this->hub_->lamps().size());
  out += "}}";
  send_json(request, 200, out);
}

void KLinkWeb::handle_mode(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (!request->hasArg("mode")) {
    send_error(request, 400, "missing parameter: mode");
    return;
  }
  kk::Mode mode;
  if (!kk::mode_from_name(arg_string(request, "mode").c_str(), mode)) {
    send_error(request, 400, "invalid mode (program, manual or off)");
    return;
  }
  // Every change runs on the main loop (as ESPHome's own web handlers do with defer()): this handler
  // runs on the HTTP server's task, and state changes publish to the API and the event stream and
  // write bus frames, none of which is safe from another task.
  this->defer([group, mode]() { group->set_mode(mode); });
  this->send_ok(request);
}

// "0".."5" in half-minute steps.
static bool parse_effect_minutes(const std::string &text, uint32_t &duration_ms) {
  if (text.empty())
    return false;
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == nullptr || *end != '\0' || value < 0.0 || value > 5.0)
    return false;
  const double halves = value * 2.0;
  const double rounded = std::floor(halves + 0.5);
  if (std::fabs(halves - rounded) > 1e-6)
    return false;  // not a half-minute step
  duration_ms = static_cast<uint32_t>(rounded) * 30000u;
  return true;
}

static bool effect_speed_from_name(const std::string &text, uint8_t &speed) {
  if (text == "fast") {
    speed = 0;
  } else if (text == "moderate") {
    speed = 1;
  } else if (text == "slow") {
    speed = 2;
  } else {
    return false;
  }
  return true;
}

// Start an effect as a temporary overlay on the schedule; the group's mode does not change.
void KLinkWeb::handle_effect(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (!request->hasArg("name")) {
    send_error(request, 400, "missing parameter: name");
    return;
  }
  const std::string name = arg_string(request, "name");
  bool known = false;
  for (const auto &candidate : group->effect_names())
    if (candidate == name)
      known = true;
  if (!known) {
    send_error(request, 404, "unknown effect");
    return;
  }
  uint8_t speed = group->effect_speed();
  if (request->hasArg("speed")) {
    if (!effect_speed_from_name(arg_string(request, "speed"), speed)) {
      send_error(request, 400, "invalid parameter: speed");
      return;
    }
  }
  uint32_t duration_ms = group->effect_duration_ms();
  if (request->hasArg("minutes")) {
    if (!parse_effect_minutes(arg_string(request, "minutes"), duration_ms)) {
      send_error(request, 400, "invalid parameter: minutes");
      return;
    }
  }
  this->defer([group, name, speed, duration_ms]() {
    group->set_effect_options(speed, duration_ms);
    group->start_effect(name);
  });
  this->send_ok(request);
}

// POST /klink/output?gid=&curve=0|1&seconds=0..10: any subset of the group's
// brightness curve and transition time.
void KLinkWeb::handle_output(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (!group->has_output_options()) {
    send_error(request, 400, "this program has no output options");
    return;
  }
  OutputSettings settings = group->output_settings();
  uint32_t value;
  if (request->hasArg("curve")) {
    if (!this->read_uint_arg(request, "curve", 1, value))
      return;
    settings.curve = static_cast<uint8_t>(value);
  }
  if (request->hasArg("soft_start")) {
    if (!this->read_uint_arg(request, "soft_start", 120, value))
      return;
    settings.soft_start = static_cast<uint8_t>(value);
  }
  if (request->hasArg("seconds")) {
    const std::string text = arg_string(request, "seconds");
    char *end = nullptr;
    const double seconds = std::strtod(text.c_str(), &end);
    if (text.empty() || end == nullptr || *end != '\0' || seconds < 0.0 || seconds > 10.0) {
      send_error(request, 400, "invalid parameter: seconds");
      return;
    }
    settings.fade_ds = static_cast<uint16_t>(std::lround(seconds * 10.0));
  }
  this->defer([group, settings]() { group->set_output_settings(settings); });
  this->send_ok(request);
}

static bool source_from_name(const std::string &text, ProgramSource &out) {
  if (text == "points" || text == "0") out = ProgramSource::POINTS;
  else if (text == "sky" || text == "1") out = ProgramSource::SKY;
  else return false;
  return true;
}

// The group whose request names a program with the Sky source; sends the error otherwise.
ProgramHost *KLinkWeb::find_sky_group(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return nullptr;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return nullptr;
  }
  if (!group->has_sources()) {
    send_error(request, 400, "this program has no Sky source");
    return nullptr;
  }
  return group;
}

// POST /klink/source?gid=&source=points|sky
void KLinkWeb::handle_source(AsyncWebServerRequest *request) {
  ProgramHost *group = this->find_sky_group(request);
  if (group == nullptr)
    return;
  ProgramSource source;
  if (!request->hasArg("source") || !source_from_name(arg_string(request, "source"), source)) {
    send_error(request, 400, "invalid parameter: source");
    return;
  }
  this->defer([group, source]() { group->set_source(source); });
  this->send_ok(request);
}

// POST /klink/sky?gid=&layers=&peak=&hours=&glow=&amber=&ramp=&shift=&moon=&phase=: any subset of the
// Sky settings.
void KLinkWeb::handle_sky(AsyncWebServerRequest *request) {
  ProgramHost *group = this->find_sky_group(request);
  if (group == nullptr)
    return;
  SkySettings s = group->sky_settings();
  uint32_t value;
  if (request->hasArg("layers")) {
    if (!this->read_uint_arg(request, "layers", 3, value)) return;
    if (value == 0) {
      send_error(request, 400, "invalid parameter: layers");
      return;
    }
    if (value == 2 && !group->can_moon()) {
      send_error(request, 400, "the moon needs a light that makes blue");
      return;
    }
    s.layers = static_cast<uint8_t>(value);
  }
  if (request->hasArg("peak")) {
    if (!this->read_uint_arg(request, "peak", 100, value)) return;
    s.peak = static_cast<uint8_t>(value);
  }
  if (request->hasArg("hours")) {
    if (!this->read_uint_arg(request, "hours", 16, value)) return;
    if (value != 0 && value < 4) {
      send_error(request, 400, "invalid parameter: hours");
      return;
    }
    s.hours = static_cast<uint8_t>(value);
  }
  if (request->hasArg("glow")) {
    if (!this->read_uint_arg(request, "glow", 100, value)) return;
    s.glow = static_cast<uint8_t>(value);
  }
  if (request->hasArg("amber")) {
    if (!this->read_uint_arg(request, "amber", 10, value)) return;
    s.amber = static_cast<uint8_t>(value);
  }
  if (request->hasArg("ramp")) {
    if (!this->read_uint_arg(request, "ramp", 240, value)) return;
    s.ramp = static_cast<uint8_t>(value / 10);
  }
  if (request->hasArg("moon")) {
    if (!this->read_uint_arg(request, "moon", 100, value)) return;
    s.moon = static_cast<uint8_t>(value);
  }
  if (request->hasArg("phase")) {
    if (!this->read_uint_arg(request, "phase", 1, value)) return;
    s.phase = static_cast<uint8_t>(value);
  }
  if (request->hasArg("shift")) {
    const std::string text = arg_string(request, "shift");
    char *end = nullptr;
    const long shift = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end == nullptr || *end != '\0' || shift < -120 || shift > 120) {
      send_error(request, 400, "invalid parameter: shift");
      return;
    }
    s.shift = static_cast<int8_t>(shift);
  }
  this->defer([group, s]() { group->set_sky_settings(s); });
  this->send_ok(request);
}

// POST /klink/source/copy?gid=: today's generated day becomes the group's own points.
void KLinkWeb::handle_source_copy(AsyncWebServerRequest *request) {
  ProgramHost *group = this->find_sky_group(request);
  if (group == nullptr)
    return;
  if (group->generated_points().empty()) {
    send_error(request, 409, "no generated day to copy");
    return;
  }
  this->defer([group]() { group->copy_generated_to_points(); });
  this->send_ok(request);
}

// GET /klink/generated?gid=&source=sky&date=YYYY-MM-DD: that day's points and event summary, for the
// page's preview. Read-only.
void KLinkWeb::handle_generated(AsyncWebServerRequest *request) {
  ProgramHost *group = this->find_sky_group(request);
  if (group == nullptr)
    return;
  ProgramSource source;
  if (!request->hasArg("source") || !source_from_name(arg_string(request, "source"), source) ||
      source == ProgramSource::POINTS) {
    send_error(request, 400, "invalid parameter: source");
    return;
  }
  int y = 0, m = 0, d = 0;
  const std::string date = request->hasArg("date") ? arg_string(request, "date") : std::string();
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 ||
      d > 31) {
    send_error(request, 400, "invalid parameter: date");
    return;
  }
  // The settings are part of the key: a day computed with other settings is a different answer, so an
  // edited peak, glow, ramp or shift can never be served from the one-entry cache below.
  const SkySettings sky = group->sky_settings();
  char key[128];
  std::snprintf(key, sizeof(key), "%u/%d/%04d-%02d-%02d/%u/%u/%u/%u/%u/%u/%d/%u/%u",
                static_cast<unsigned>(group->gid()), static_cast<int>(source), y, m, d,
                static_cast<unsigned>(sky.layers), static_cast<unsigned>(sky.peak), static_cast<unsigned>(sky.hours),
                static_cast<unsigned>(sky.glow), static_cast<unsigned>(sky.amber), static_cast<unsigned>(sky.ramp),
                static_cast<int>(sky.shift), static_cast<unsigned>(sky.moon), static_cast<unsigned>(sky.phase));
  if (this->gen_key_ == key) {
    if (this->gen_state_ == 2) {
      send_json(request, 200, this->gen_json_);
      return;
    }
    if (this->gen_state_ == 3) {
      send_error(request, 409, "no location set");
      return;
    }
    send_json(request, 200, "{\"pending\":true}");  // still computing: ask again in a moment (200, because the server maps other codes to 500)
    return;
  }
  this->gen_state_ = 1;
  this->gen_key_ = key;
  this->defer([this, group, source, y, m, d]() {
    std::vector<kk::SchedulePoint> points;
    std::string summary;
    if (!group->generate_day(source, y, m, d, points, summary)) {
      this->gen_state_ = 3;
      return;
    }
    std::string out = "{\"points\":[";
    for (size_t i = 0; i < points.size(); i++) {
      if (i)
        out += ',';
      append_slot_json(out, points[i]);
    }
    out += "],\"summary\":" + summary + "}";
    this->gen_json_ = out;
    this->gen_state_ = 2;
  });
  send_json(request, 200, "{\"pending\":true}");
}

// POST /klink/acclimation?gid=&on=0|1&start=10..90&days=1..90: any subset. Turning it on starts the days now.
void KLinkWeb::handle_acclimation(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  kk::AcclimationPlan plan = group->acclimation();
  uint32_t value;
  if (request->hasArg("on")) {
    if (!this->read_uint_arg(request, "on", 1, value))
      return;
    plan.on = value != 0;
  }
  if (request->hasArg("start")) {
    if (!this->read_uint_arg(request, "start", 90, value))
      return;
    if (value < 10) {
      send_error(request, 400, "invalid parameter: start");
      return;
    }
    plan.start_pct = static_cast<uint8_t>(value);
  }
  if (request->hasArg("days")) {
    if (!this->read_uint_arg(request, "days", 90, value))
      return;
    if (value < 1) {
      send_error(request, 400, "invalid parameter: days");
      return;
    }
    plan.days = static_cast<uint8_t>(value);
  }
  this->defer([group, plan]() { group->set_acclimation(plan); });
  this->send_ok(request);
}

void KLinkWeb::handle_effect_stop(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  this->defer([group]() { group->stop_effect(); });
  this->send_ok(request);
}

void KLinkWeb::handle_identify(AsyncWebServerRequest *request) {
  const bool has_kid = request->hasArg("kid");
  const bool has_gid = request->hasArg("gid");
  const bool has_follower_id = request->hasArg("follower");
  if (static_cast<int>(has_kid) + static_cast<int>(has_gid) + static_cast<int>(has_follower_id) != 1) {
    send_error(request, 400, "provide exactly one of kid, gid or follower");
    return;
  }
  if (has_follower_id) {
    // A `followers:` entry dips its own lights. This never touches the bus, so it also works on a
    // follower, where the page calls it "Find these lights".
    const std::string key = arg_string(request, "follower");
    KLinkHub *hub = this->hub_;
    if (!hub->has_follower(key)) {
      send_error(request, 404, "unknown follower");
      return;
    }
    this->defer([hub, key]() { hub->flash_local(key); });
    this->send_ok(request);
    return;
  }
  uint32_t value;
  if (has_kid) {
    if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, value))
      return;
  } else {
    // A light program flashes its own lights (also in dry run: it never touches the bus); a bus group is found
    // through its lamps.
    uint32_t id;
    if (!this->read_uint_arg(request, "gid", kk::PROGRAM_ID_MAX, id))
      return;
    if (kk::is_program_id(id)) {
      ProgramHost *program = this->find_group(id);
      if (program == nullptr) {
        send_error(request, 404, "unknown group");
        return;
      }
      this->defer([program]() { program->find(); });
      this->send_ok(request);
      return;
    }
    if (!this->read_lamp_gid(request, "gid", value))
      return;
  }
  KLinkHub *hub = this->hub_;
  if (this->hub_->is_follower()) {
    // Nothing goes on the bus: dip the board's own lights that follow this lamp or group.
    const uint8_t gid = static_cast<uint8_t>(value);
    const bool any = has_kid ? hub->has_local_kid(value) : hub->has_local_gid(gid);
    if (!any) {
      send_error(request, 404, has_kid ? "no such local lamp" : "no follower lights in that group");
      return;
    }
    this->defer([hub, has_kid, value, gid]() {
      if (has_kid)
        hub->flash_local_kid(value);
      else
        hub->flash_local_gid(gid);
    });
    this->send_ok(request);
    return;
  }
  if (!this->hub_->writes_enabled()) {
    send_error(request, 409, "Send to lights is off");
    return;
  }
  if (has_kid) {
    this->defer([hub, value]() { hub->identify(value); });
  } else {
    bool any = false;
    for (const auto &lamp : hub->lamps())
      if (lamp.gid == value)
        any = true;
    if (!any) {
      send_error(request, 404, "no known lamps in that group");
      return;
    }
    const uint8_t gid = static_cast<uint8_t>(value);
    this->defer([hub, gid]() { hub->identify_gid(gid); });
  }
  this->send_ok(request);
}

void KLinkWeb::handle_point(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  uint32_t slot;
  if (!this->read_uint_arg(request, "slot", static_cast<uint32_t>(group->num_slots()), slot))
    return;
  if (slot < 1) {
    send_error(request, 400, "slot must be 1-based");
    return;
  }
  size_t index = slot - 1;

  kk::SchedulePoint p = group->point(index);
  uint32_t value;
  if (request->hasArg("minute")) {
    if (!parse_uint(arg_string(request, "minute"), 1439, value)) {
      send_error(request, 400, "invalid parameter: minute");
      return;
    }
    p.minute = static_cast<uint16_t>(value);
  }
  if (request->hasArg("int")) {
    if (!parse_uint(arg_string(request, "int"), group->channel_max()[0], value)) {
      send_error(request, 400, "invalid parameter: int");
      return;
    }
    p.channels[0] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("clr")) {
    if (!parse_uint(arg_string(request, "clr"), group->channel_max()[1], value)) {
      send_error(request, 400, "invalid parameter: clr");
      return;
    }
    p.channels[1] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("r")) {
    if (!parse_uint(arg_string(request, "r"), group->channel_max()[3], value)) {
      send_error(request, 400, "invalid parameter: r");
      return;
    }
    p.channels[3] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("am")) {
    if (!parse_uint(arg_string(request, "am"), group->channel_max()[4], value)) {
      send_error(request, 400, "invalid parameter: am");
      return;
    }
    p.channels[4] = static_cast<uint8_t>(value);
  }
  // Index form for any model: c1..c5 are UI1..UI5 (the page uses the group's labels and limits).
  static const char *const CH_ARGS[kk::NUM_CHANNELS] = {"c1", "c2", "c3", "c4", "c5"};
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
    if (!request->hasArg(CH_ARGS[c]))
      continue;
    if (group->slot_labels()->name[c] == nullptr) {
      send_error(request, 400, std::string("this model has no channel ") + CH_ARGS[c]);
      return;
    }
    if (!parse_uint(arg_string(request, CH_ARGS[c]), group->channel_max()[c], value)) {
      send_error(request, 400, std::string("invalid parameter: ") + CH_ARGS[c]);
      return;
    }
    p.channels[c] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("enabled")) {
    bool enabled;
    if (!this->read_bool_arg(request, "enabled", enabled))
      return;
    p.enabled = enabled;
  }
  this->defer([group, index, p]() { group->set_slot_from_values(index, p.minute, p.channels, p.enabled); });
  this->send_ok(request);
}

void KLinkWeb::handle_preview(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  bool has_slot = request->hasArg("slot");
  bool has_minute = request->hasArg("minute");
  if (has_slot == has_minute) {
    send_error(request, 400, "provide exactly one of slot or minute");
    return;
  }
  uint32_t value;
  if (has_slot) {
    if (!parse_uint(arg_string(request, "slot"), static_cast<uint32_t>(group->num_slots()), value) || value < 1) {
      send_error(request, 400, "invalid parameter: slot");
      return;
    }
    const size_t slot_index = value - 1;
    this->defer([group, slot_index]() { group->preview_point(slot_index); });
  } else {
    if (!parse_uint(arg_string(request, "minute"), 1439, value)) {
      send_error(request, 400, "invalid parameter: minute");
      return;
    }
    const uint16_t minute = static_cast<uint16_t>(value);
    this->defer([group, minute]() { group->preview_minute(minute); });
  }
  this->send_ok(request);
}

void KLinkWeb::handle_preview_done(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  this->defer([group]() { group->end_preview(); });
  this->send_ok(request);
}

void KLinkWeb::handle_assign(AsyncWebServerRequest *request) {
  uint32_t kid;
  if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, kid))
    return;
  uint32_t gid;
  if (!this->read_lamp_gid(request, "gid", gid))
    return;
  std::string error;
  if (!this->hub_->check_assign(kid, static_cast<uint8_t>(gid), error)) {
    send_error(request, 409, error.empty() ? "assignment refused" : error);
    return;
  }
  KLinkHub *hub = this->hub_;
  const uint8_t target = static_cast<uint8_t>(gid);
  this->defer([hub, kid, target]() {
    std::string why;
    hub->assign(kid, target, why);
  });
  this->send_ok(request);
}

void KLinkWeb::handle_name(AsyncWebServerRequest *request) {
  uint32_t kid;
  if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, kid))
    return;
  if (!request->hasArg("name")) {
    send_error(request, 400, "missing parameter: name");
    return;
  }
  std::string error;
  const std::string name = arg_string(request, "name");
  if (!this->hub_->check_name(kid, name, error)) {
    send_error(request, 400, error);
    return;
  }
  KLinkHub *hub = this->hub_;
  this->defer([hub, kid, name]() {
    std::string why;
    hub->set_name(kid, name, why);
  });
  this->send_ok(request);
}

void KLinkWeb::handle_group_name(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  if (this->find_group(gid) == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (!request->hasArg("name")) {
    send_error(request, 400, "missing parameter: name");
    return;
  }
  std::string error;
  const std::string name = arg_string(request, "name");
  if (!this->hub_->check_group_name(gid, name, error)) {
    send_error(request, 400, error);
    return;
  }
  KLinkHub *hub = this->hub_;
  this->defer([hub, gid, name]() {
    std::string why;
    hub->set_group_name(gid, name, why);
  });
  this->send_ok(request);
}

void KLinkWeb::handle_copy(AsyncWebServerRequest *request) {
  uint32_t from;
  if (!this->read_program_gid(request, "from", from))
    return;
  uint32_t to;
  if (!this->read_program_gid(request, "to", to))
    return;
  ProgramHost *from_group = this->find_group(from);
  ProgramHost *to_group = this->find_group(to);
  if (from_group == nullptr || to_group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (to_group->model_name() != from_group->model_name() || to_group->num_slots() != from_group->num_slots()) {
    send_error(request, 409, "groups differ in model or slot count");
    return;
  }
  this->defer([to_group, from_group]() { to_group->copy_program_from(*from_group); });
  this->send_ok(request);
}

// Send to lights on or off, like the switch entity (which exists even without the web_server component).
void KLinkWeb::handle_writes(AsyncWebServerRequest *request) {
  bool on;
  if (!this->read_bool_arg(request, "on", on))
    return;
  KLinkHub *hub = this->hub_;
  this->defer([hub, on]() { hub->set_writes_from_ui(on); });
  this->send_ok(request);
}

void KLinkWeb::handle_retry(AsyncWebServerRequest *request) {
  KLinkHub *hub = this->hub_;
  this->defer([hub]() { hub->retry(); });
  this->send_ok(request);
}

void KLinkWeb::handle_scan(AsyncWebServerRequest *request) {
  KLinkHub *hub = this->hub_;
  if (hub->is_follower()) {
    send_error(request, 400, "this device is a follower; scan on the chain leader");
    return;
  }
  if (!hub->writes_enabled()) {
    // A scan needs the leader to control the bus. Say why instead of accepting a request the main
    // loop will refuse, which used to look like the button did nothing.
    send_error(request, 409, hub->state_text());
    return;
  }
  this->defer([hub]() { hub->start_scan(); });
  this->send_ok(request);
}

static void append_hex_rgb(std::string &out, const uint8_t rgb[3]) {
  static const char HEX[] = "0123456789abcdef";
  for (size_t c = 0; c < 3; c++) {
    out += HEX[rgb[c] >> 4];
    out += HEX[rgb[c] & 0x0f];
  }
}

// GET /klink/snap?gid=&rgb=RRGGBB: the closest settings this lamp can make for the target color.
// Read-only: the page applies the answer itself through /klink/point.
void KLinkWeb::handle_snap(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }

  const kk::ColorSpec *spec = nullptr;
  const std::string model = group->model_name();
  if (model == "tuna_sun")
    spec = &COLOR_SUN;
  else if (model == "tuna_blue")
    spec = &COLOR_BLUE;
  else if (model == "refugium")
    spec = &COLOR_REFUGIUM;
  if (spec == nullptr) {
    send_error(request, 400, "no color model for this group");
    return;
  }

  if (!request->hasArg("rgb")) {
    send_error(request, 400, "missing parameter: rgb");
    return;
  }
  uint8_t target[3];
  if (!parse_hex_rgb(arg_string(request, "rgb"), target)) {
    send_error(request, 400, "invalid parameter: rgb");
    return;
  }

  kk::SnapResult result = kk::nearest_i(*spec, target);

  const kk::SlotLabels *labels = group->slot_labels();
  std::string out;
  out.reserve(160);
  out += "{\"set\":{\"clr\":";
  out += std::to_string(result.clr);
  for (uint8_t o = 0; o < spec->n_overlays; o++) {
    uint8_t channel = spec->overlay_channel[o];
    const char *label = labels->name[channel];
    if (label == nullptr)
      continue;
    out += ',';
    append_quoted(out, label);
    out += ':';
    out += std::to_string(result.overlays[o]);
  }
  out += "},\"rgb\":\"";
  append_hex_rgb(out, result.rgb);
  out += "\",\"target\":\"";
  append_hex_rgb(out, target);
  out += "\",\"exact\":";
  out += result.dist == 0 ? "true" : "false";
  out += '}';
  send_json(request, 200, out);
}

}  // namespace klink
}  // namespace esphome

#endif  // USE_KLINK_WEB
