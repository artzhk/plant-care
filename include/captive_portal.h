/**
 * The captive portal serves as the first entry point to build a state for each
 * node, it's main purpose to create an access point, prompt user to connect to
 * it and type the ssid, password, home assistant ip address and master ip
 * address. After saving the data, a sensor node is reloaded and the portal is
 * not invoked.
 *
 * NODE_SENSOR/NODE_MASTER macros is used to build a function definition based
 * on either sensor, either master node configuration. Sensor needs an
 * additional config "master_ip" field, whereas Master node doesn't need such a
 * configuration field
 */
#if defined(NODE_MASTER) || defined(NODE_SENSOR)
#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H
#include <LittleFS.h>

#include "DNSServer.h"
#include "ESP8266WiFi.h"
#include "globals.h"

static struct NodeConfig {
  char pass[64];
  char ssid[32];
#if defined(NODE_SENSOR)
  char master_ip[20];
  bool display_only;
#endif  // NODE_SENSOR
  char ha_ip[20];
} node_cfg;

// The buffer used to transfer configuration data
static const uint8_t CONFIG_BUF_SIZE = 250;

// Pre-filled edit form — served by captive_portal_bg() so the user sees
// current values without re-typing everything. Format args (NODE_SENSOR):
//   ssid, pass, ha_ip, master_ip, checked_attr ("checked" | "")
static const char PORTAL_HTML_EDIT[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>PlantCare "
    "Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px "
    "auto;padding:0 10px}"
    "label{display:block;margin-bottom:10px}"
    "input{width:100%;box-sizing:border-box;padding:6px;margin-top:2px}</style>"
    "</head><body><h2>Plant Sensor Setup</h2>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>WiFi SSID<input name=\"ssid\" maxlength=\"31\" value=\"%s\" required></label>"
    "<label>WiFi Password<input type=\"password\" name=\"pass\" value=\"%s\" maxlength=\"63\"></label>"
    "<label>HA IP<input name=\"ha_ip\" value=\"%s\" maxlength=\"19\" placeholder=\"192.168.1.100\"></label>"
#if defined(NODE_SENSOR)
    "<label>Master IP<input name=\"master_ip\" value=\"%s\" maxlength=\"19\" placeholder=\"192.168.1.100\"></label>"
    "<label><input type=\"checkbox\" name=\"display_only\" %s>Display only mode</label>"
#endif  // NODE_SENSOR
    "<input type=\"submit\" value=\"Save &amp; Restart\">"
    "</form></body></html>";

// Blank form — used by start_config_portal() on first-time setup.
static const char PORTAL_HTML[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>PlantCare "
    "Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px "
    "auto;padding:0 10px}"
    "label{display:block;margin-bottom:10px}"
    "input{width:100%;box-sizing:border-box;padding:6px;margin-top:2px}</style>"
    "</head><body><h2>Plant Sensor Setup</h2>"
    "<form method=\"POST\" action=\"/save\">"
    "<label>WiFi SSID<input name=\"ssid\" maxlength=\"31\" required></label>"
    "<label>WiFi Password<input type=\"password\" name=\"pass\" maxlength=\"63\"></label>"
    "<label>HA IP<input name=\"ha_ip\" maxlength=\"19\" placeholder=\"192.168.1.100\"></label>"
#if defined(NODE_SENSOR)
    "<label>Master IP<input name=\"master_ip\" maxlength=\"19\" placeholder=\"192.168.1.100\"></label>"
    "<label><input type=\"checkbox\" name=\"display_only\">Display only mode</label>"
#endif  // NODE_SENSOR
    "<input type=\"submit\" value=\"Save &amp; Restart\">"
    "</form></body></html>";

#if defined(NODE_SENSOR)
static void save_config(const char* ssid, const char* pass, const char* ha_ip,
                        const char* master_ip, const bool display_only)
#endif  // NODE_SENSOR
#if defined(NODE_MASTER)
static void save_config(const char* ssid, const char* pass, const char* ha_ip)
#endif  // NODE_MASTER
{
  if (!LittleFS.begin()) return;
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  char buf[CONFIG_BUF_SIZE];
  snprintf(buf, sizeof(buf),
#if defined(NODE_SENSOR)
           "{\"ssid\":\"%s\",\"pass\":\"%s\",\"ha_ip\":\"%s\","
           "\"master_ip\":\"%s\",\"display_only\":%d}",
           ssid, pass, ha_ip, master_ip, (int)display_only);
#endif  // NODE_SENSOR
#if defined(NODE_MASTER)
           "{\"ssid\":\"%s\",\"pass\":\"%s\",\"ha_ip\":\"%s\"}",
           ssid, pass, ha_ip);
#endif  // NODE_MASTER
  f.print(buf);
  f.close();
}

// Shared /save POST handler — used by both start_config_portal and
// captive_portal_bg so save logic is not duplicated.
static void save_endpoint(ESP8266WebServer& portal) {
  char ssid_buf[32], pass_buf[64], ha_buf[20];
  portal.arg("ssid").toCharArray(ssid_buf, sizeof(ssid_buf));
  portal.arg("pass").toCharArray(pass_buf, sizeof(pass_buf));
  portal.arg("ha_ip").toCharArray(ha_buf, sizeof(ha_buf));

#if defined(NODE_SENSOR)
  char master_buf[20];
  portal.arg("master_ip").toCharArray(master_buf, sizeof(master_buf));
  // Unchecked HTML checkboxes send no key at all; hasArg() is the correct test.
  const bool display_only = portal.hasArg("display_only");
  save_config(ssid_buf, pass_buf, ha_buf, master_buf, display_only);
#endif  // NODE_SENSOR
#if defined(NODE_MASTER)
  save_config(ssid_buf, pass_buf, ha_buf);
#endif  // NODE_MASTER

  portal.send(200, "text/html",
              "<html><body><h2>Saved. Rebooting...</h2></body></html>");
  delay(1000);
  ESP.restart();
}

#if defined(NODE_MASTER)
static void start_config_portal(
    std::function<bool(const char*, const char*)> display)
#elif defined(NODE_SENSOR)
static void start_config_portal(std::function<void(const char*)> display)
#endif  // NODE_SENSOR
{
  WiFi.mode(WIFI_AP);
#if defined(NODE_MASTER)
  WiFi.softAP("PlantCare-Master");
#elif defined(NODE_SENSOR)
  WiFi.softAP("PlantCare-Sensor");
#endif  // NODE_SENSOR

  DNSServer dns;
  dns.start(53, "*", IPAddress(192, 168, 4, 1));

#if defined(NODE_MASTER)
  display("Serving setup", "192.168.4.1");
#elif defined(NODE_SENSOR)
  display("Serving setup\n 192.168.4.1");
#endif  // NODE_SENSOR

  ESP8266WebServer portal(80);
  portal.on("/", HTTP_GET,
            [&]() { portal.send_P(200, PSTR("text/html"), PORTAL_HTML); });
  portal.on("/save", HTTP_POST, [&]() { save_endpoint(portal); });
  portal.onNotFound([&]() {
    portal.sendHeader("Location", "http://192.168.4.1/", true);
    portal.send(302, "text/plain", "");
  });
  portal.begin();

  while (true) {
    dns.processNextRequest();
    portal.handleClient();
    yield();
  }
}

/**
 * Loading config function:
 * true  — all required fields present and valid.
 * false — otherwise (triggers captive portal on boot).
 * display_only is optional: missing field defaults to false so old configs
 * without the field are still accepted.
 */
static bool load_config() {
  if (!LittleFS.begin()) return false;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return false;
  char buf[CONFIG_BUF_SIZE];
  const size_t n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n] = '\0';

  // Reads a quoted JSON string value into out[0..sz-1].
  auto extract_str = [](const char* src, const char* key, char* out,
                        size_t sz) -> bool {
    const char* p = strstr(src, key);
    if (!p) return false;
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != '"' && i < sz - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
  };

  // Reads a bare JSON boolean (true/false/0/1) into *out.
  auto extract_bool = [](const char* src, const char* key, bool* out) -> bool {
    const char* p = strstr(src, key);
    if (!p) return false;
    p += strlen(key);
    if (strncmp(p, "true", 4) == 0 || *p == '1') { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0 || *p == '0') { *out = false; return true; }
    return false;
  };

  bool ok = true;
  ok &= extract_str(buf, "\"ssid\":\"", node_cfg.ssid, sizeof(node_cfg.ssid));
  ok &= extract_str(buf, "\"pass\":\"", node_cfg.pass, sizeof(node_cfg.pass));
  ok &= extract_str(buf, "\"ha_ip\":\"", node_cfg.ha_ip, sizeof(node_cfg.ha_ip));
#if defined(NODE_SENSOR)
  ok &= extract_str(buf, "\"master_ip\":\"", node_cfg.master_ip,
                    sizeof(node_cfg.master_ip));
  // display_only is optional — missing field (old config) keeps default false.
  node_cfg.display_only = false;
  extract_bool(buf, "\"display_only\":", &node_cfg.display_only);
#endif  // NODE_SENSOR

  return ok;
}

#if defined(NODE_SENSOR)

// Written once by captive_portal_bg() on first call; read by role_sensor.cpp's
// display() to show the AP password on the OLED.
static char bg_portal_pass[9];

// Called every role_loop() iteration while in display-only mode.
// On the first call: derives a per-node AP password (FNV-1a of NODE_ID +
// gateway IP), starts the soft-AP, and registers routes including a pre-filled
// edit form so the user can reconfigure without re-typing every field.
// Subsequent calls are cheap: one DNS drain + one HTTP client poll.
static void captive_portal_bg() {
  static bool initialized = false;
  static DNSServer dns;
  static ESP8266WebServer portal_srv(80);

  if (!initialized) {
    // FNV-1a over NODE_ID byte then the AP gateway string — deterministic,
    // per-node, no heap allocation.
    uint32_t h = 2166136261UL;
    h ^= (uint8_t)NODE_ID;
    h *= 16777619UL;
    for (const char* p = "192.168.4.1"; *p; ++p) {
      h ^= (uint8_t)*p;
      h *= 16777619UL;
    }
    snprintf(bg_portal_pass, sizeof(bg_portal_pass), "%08x", (unsigned)h);

    char ssid[20];
    snprintf(ssid, sizeof(ssid), "PlantCare-S%s", _XSTR(NODE_ID));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, bg_portal_pass);
    dns.start(53, "*", IPAddress(192, 168, 4, 1));

    // Serve the edit form pre-filled with the values currently in node_cfg.
    portal_srv.on("/", HTTP_GET, []() {
      // Static so this ~900-byte buffer is not on the stack every request.
      static char edit_buf[900];
      snprintf_P(edit_buf, sizeof(edit_buf), PORTAL_HTML_EDIT,
                 node_cfg.ssid,
                 node_cfg.pass,
                 node_cfg.ha_ip,
                 node_cfg.master_ip,
                 node_cfg.display_only ? "checked" : "");
      portal_srv.send(200, "text/html", edit_buf);
    });

    portal_srv.on("/save", HTTP_POST,
                  []() { save_endpoint(portal_srv); });

    portal_srv.onNotFound([]() {
      portal_srv.sendHeader("Location", "http://192.168.4.1/", true);
      portal_srv.send(302, "text/plain", "");
    });

    portal_srv.begin();
    initialized = true;
  }

  dns.processNextRequest();
  portal_srv.handleClient();
}

#endif  // NODE_SENSOR

#endif  // CAPTIVE_PORTAL_H
#endif  // NODE_SENSOR || NODE_MASTER
