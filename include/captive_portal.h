/** 
 * The captive portal serves as the first entry point to build a state for each node, it's main purpose to create an
 * access point, prompt user to connect to it and type the ssid, password, home assistant ip address and master ip
 * address. After saving the data, a sensor node is reloaded and the portal is not invoked.
 *
 * NODE_SENSOR/NODE_MASTER macros is used to build a function definition based on either sensor, either master node
 * configuration. Sensor needs an additional config "master_ip" field, whereas Master node doesn't need such a
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
#endif // NODE_SENSOR
  char ha_ip[20];
} node_cfg;

// The buffer used to transfer configuration data
static const uint8_t CONFIG_BUF_SIZE = 250;

// Simple web page
static const char PORTAL_HTML[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>PlantCare "
    "Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px "
    "auto;padding:0 10px}"
    "label{display:block;margin-bottom:10px}"
    "input{width:100%;box-sizing:border-box;padding:6px;margin-top:2px}</style>"
#if defined(NODE_SENSOR)
    "</head><body><h2>Plant Master Setup</h2>"
#endif // NODE_SENSOR
#if defined(NODE_MASTER)
    "</head><body><h2>Plant Master Setup</h2>"
#endif // NODE_MASTER
    "<form method=\"POST\" action=\"/save\">"
    "<label>WiFi SSID<input name=\"ssid\" maxlength=\"31\" required></label>"
    "<label>WiFi Password<input type=\"password\" name=\"pass\" "
    "maxlength=\"63\"></label>"
    "<label>HA IP<input name=\"ha_ip\" maxlength=\"19\" "
    "placeholder=\"192.168.1.100\" required></label>"
#if defined(NODE_SENSOR)
    "<label>MASTER IP<input name=\"master_ip\" maxlength=\"19\" "
    "placeholder=\"192.168.1.100\" required></label>"
#endif // NODE_SENSOR
    "<input type=\"submit\" value=\"Save &amp; Restart\">"
    "</form></body></html>";

#if defined(NODE_SENSOR)
static void save_config(const char* ssid, const char* pass, const char* ha_ip,
                        const char* master_ip)
#endif // NODE_SENSOR
#if defined(NODE_MASTER)
    static void save_config(const char* ssid, const char* pass,
                            const char* ha_ip)
#endif // NODE_MASTER
{
  if (!LittleFS.begin()) return;
  File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  char buf[CONFIG_BUF_SIZE];
  snprintf(
      buf, sizeof(buf),
#if defined(NODE_SENSOR)
      "{\"ssid\":\"%s\",\"pass\":\"%s\",\"ha_ip\":\"%s\",\"master_ip\":\"%s\"}",
      ssid, pass, ha_ip, master_ip);
#endif  // NODE_SENSOR
#if defined(NODE_MASTER)
      "{\"ssid\":\"%s\",\"pass\":\"%s\",\"ha_ip\":\"%s\"}",
      ssid, pass, ha_ip);
#endif  // NODE_MASTER
      f.print(buf);
      f.close();
}

#if defined(NODE_MASTER)
static void start_config_portal(
    std::function<bool(const char*, const char*)> display)
#endif // NODE_MASTER
#if defined(NODE_SENSOR)
static void start_config_portal(std::function<void(const char*)> display)
#endif // NODE_SENSOR
{
  WiFi.mode(WIFI_AP);
#if defined(NODE_MASTER)
  WiFi.softAP("PlantCare-Master");
#endif // NODE_MASTER
#if defined(NODE_SENSOR)
  WiFi.softAP("PlantCare-Sensor");
#endif // NODE_SENSOR

  // Confuguring and starting access point
  DNSServer dns;
  dns.start(53, "*", IPAddress(192, 168, 4, 1));

#if defined(NODE_MASTER)
  display("Serving setup", "192.168.4.1");
#endif  // NODE_MASTER
#if defined(NODE_SENSOR)
  display("Serving setup\n 192.168.4.1");
#endif  // NODE_SENSOR

  ESP8266WebServer portal(80);
  // configuring mini frontend to be accessed
  portal.on("/", HTTP_GET,
            [&]() { portal.send_P(200, PSTR("text/html"), PORTAL_HTML); });
  // save config logic
  portal.on("/save", HTTP_POST, [&]() {
#if defined(NODE_MASTER)
    char ssid_buf[32], pass_buf[64], ha_buf[20];
#endif // NODE_MASTER
#if defined(NODE_SENSOR)
    char ssid_buf[32], pass_buf[64], ha_buf[20], master_buf[20];
    portal.arg("master_ip").toCharArray(master_buf, sizeof(master_buf));
#endif // NODE_SENSOR
    portal.arg("ssid").toCharArray(ssid_buf, sizeof(ssid_buf));
    portal.arg("pass").toCharArray(pass_buf, sizeof(pass_buf));
    portal.arg("ha_ip").toCharArray(ha_buf, sizeof(ha_buf));

#if defined(NODE_MASTER)
    save_config(ssid_buf, pass_buf, ha_buf);
#endif // NODE_MASTER
#if defined(NODE_SENSOR)
    save_config(ssid_buf, pass_buf, ha_buf, master_buf);
#endif // NODE_SENSOR
    portal.send(200, "text/html",
                "<html><body><h2>Saved. Rebooting...</h2></body></html>");
    delay(1000);
    Serial1.println("Rebooting");
    ESP.restart();
  });
  // not found fallback
  portal.onNotFound([&]() {
    portal.sendHeader("Location", "http://192.168.4.1/", true);
    portal.send(302, "text/plain", "");
  });
  // starting the server
  portal.begin();

  while (true) {
    dns.processNextRequest();
    portal.handleClient();
    yield();
  }
}

/** 
 * Loading config function:
 * true -  if the configuration can be loaded and all needed fields can be accessed and are valid.
 * false - otherwise
 */
static bool load_config() {
  if (!LittleFS.begin()) return false;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return false;
  char buf[CONFIG_BUF_SIZE];
  const size_t n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n] = '\0';

  auto extract = [](const char* src, const char* key, char* out,
                    size_t sz) -> bool {
    const char* p = strstr(src, key);
    if (!p) return false;
    p += strlen(key);
    size_t i = 0;
    while (*p && *p != '"' && i < sz - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
  };

  bool ok = 1;
  ok &= extract(buf, "\"ssid\":\"", node_cfg.ssid, sizeof(node_cfg.ssid));
  ok &= extract(buf, "\"pass\":\"", node_cfg.pass, sizeof(node_cfg.pass));
#if defined(NODE_SENSOR)
  ok &= extract(buf, "\"master_ip\":\"", node_cfg.master_ip,
                sizeof(node_cfg.master_ip));
#endif // NODE_SENSOR
  ok &= extract(buf, "\"ha_ip\":\"", node_cfg.ha_ip, sizeof(node_cfg.ha_ip));
  return ok;
}

#endif  // CAPTIVE_PORTAL_H
#endif  // NODE_SENSOR || NODE_MASTER
