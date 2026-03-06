/**
 * role_master.cpp — Master node firmware (NODE_MASTER builds only).
 *
 * Pinout (NodeMCU v2 / ESP8266):
 *   D1/GPIO5  — I2C SCL  (to Arduino Nano display node at address 0x12)
 *   D2/GPIO4  — I2C SDA
 *   D6/GPIO12 — Mode switch: HIGH = sensor display, LOW = show master IP
 *   A0        — Potentiometer wiper (sensor selector, 10k between 3V3/GND)
 *
 * Sensor registry:
 *   Subscribes to plants/sensor/+/state (MQTT wildcard, all sensor nodes).
 *   Incoming messages update registry[id] with {h,t,m,uv,last_seen_ms}.
 *   Sensors silent for > SENSOR_EXPIRY_MS (30 s) are considered offline.
 *   The knob ADC is mapped proportionally across the active sensor count.
 *   200 ms debounce prevents LCD flicker at zone boundaries.
 *
 * LCD protocol:
 *   32 bytes (2 × LCD_COLS) sent over I2C to a Nano (address 0x12) which
 *   drives the physical display. write16_padded() space-pads each line to
 *   exactly LCD_COLS bytes — the Nano needs no clear between updates.
 *
 * HTTP fallback:
 *   Sensor nodes POST to /data when MQTT is unavailable. Parsed into the
 *   same registry[] so both transports feed a consistent data source.
 */
#if defined(NODE_MASTER)
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <Wire.h>

#include <cstdio>

#include "WiFiClient.h"
#include "captive_portal.h"
#include "core_esp8266_features.h"
#include "role.h"

// I2C addr
static constexpr uint8_t I2C_ADDR = 0x12;
// file scope global master node ip 
static char ip[16];

// Maximum supported sensors to select from
static constexpr uint8_t MAX_SENSORS = 8;
static constexpr uint32_t SENSOR_EXPIRY_MS = 30'000UL;

static struct SensorEntry {
  int h, t, m, uv;
  uint32_t last_seen_ms;
} registry[MAX_SENSORS] = {};

char* cfg_ha_ip = nullptr;

static void write16_padded(char out[LCD_COLS], const char* s) {
  uint8_t i = 0;
  for (; i < LCD_COLS && s[i] && s[i] != '\n' && s[i] != '\r'; ++i)
    out[i] = s[i];
  for (; i < LCD_COLS; ++i) out[i] = ' ';
}

static bool send_lcd(const char* line1, const char* line2) {
  char b1[LCD_COLS], b2[LCD_COLS];

  write16_padded(b1, line1);
  write16_padded(b2, line2);

  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reinterpret_cast<const uint8_t*>(b1), LCD_COLS);
  Wire.write(reinterpret_cast<const uint8_t*>(b2), LCD_COLS);

  const uint8_t rc = Wire.endTransmission(1);

  return rc == 0;
}

static void on_mqtt_message(char* topic, byte* payload, unsigned int length) {
  if (length >= sizeof(req_buf)) length = sizeof(req_buf) - 1;
  memcpy(req_buf, payload, length);
  req_buf[length] = '\0';

  int id = 0;
  sscanf(topic, "plants/sensor/%d/state", &id);
  if (id < 1 || id >= MAX_SENSORS) return;

  int h = 0, t = 0, m = 0, uv = 0;
  sscanf(req_buf, "{\"h\":%d,\"t\":%d,\"id\":%*d, \"m\":%d,\"uv\":%d}", &h, &t,
         &m, &uv);

  registry[id] = {h, t, m, uv, millis()};
}

static void handle_data_post() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "no body");
    return;
  }
  server.arg("plain").toCharArray(req_buf, sizeof(req_buf));

  int h = 0, t = 0, m = 0, uv = 0, id = 0;
  sscanf(req_buf, "{\"h\":%d,\"t\":%d,\"id\":%d,\"m\":%d,\"uv\":%d}", &h, &t,
         &id, &m, &uv);

  if (id >= 1 && id < MAX_SENSORS) registry[id] = {h, t, m, uv, millis()};
  server.send(200, "text/plain", "ok");
}

void role_setup() {
  Wire.begin(D2, D1);
  Wire.setClock(I2C_HZ);
  // speak to nano i2c
  Serial1.begin(9600);
  if (!load_config()) {
    start_config_portal(send_lcd);
  }
  cfg_ha_ip = node_cfg.ha_ip;

  // debugging
  Serial.begin(115200);
  pinMode(D6, INPUT);

  // Connect to wifi
  WiFi.begin(node_cfg.ssid, node_cfg.pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    if (!send_lcd("Awaiting", "connection")) {
      Serial.print(F("Failed to send data to lcd"));
    }
    Serial.print(F("."));
  }

  IPAddress ip_A = WiFi.localIP();
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip_A[0], ip_A[1], ip_A[2], ip_A[3]);
  send_lcd("IP:", ip);

  mqtt.setCallback(on_mqtt_message);
  server.on("/data", HTTP_POST, handle_data_post);
  server.begin();
}

void role_loop() {
  static bool mqtt_subscribed = false;
  // Better then delay(), no stacking delays to flows and separate wait concerns
  static uint32_t last_tick_ms = 0;
  static uint32_t last_lcd_ms = 0;
  static uint8_t prev_sel = 255;
  const uint32_t now = millis();

  if ((uint32_t)(now - last_tick_ms) < 100UL) return;
  last_tick_ms = now;
  mqtt_ensure_connected();

  if (mqtt.connected()) {
    mqtt.loop();
    if (!mqtt_subscribed) {
      if (mqtt.subscribe("plants/sensor/+/state")) mqtt_subscribed = true;
    }
  } else {
    mqtt_subscribed = false;  // reset on disconnect
  }

  server.handleClient();

  uint8_t active_ids[MAX_SENSORS];
  uint8_t active_count = 0;
  // summarise last reported sensors in the window of 30seconds 
  for (uint8_t i = 0; i < MAX_SENSORS; ++i) {
    if (registry[i].last_seen_ms != 0 &&
        (uint32_t)(now - registry[i].last_seen_ms) < SENSOR_EXPIRY_MS)
      active_ids[active_count++] = i;
  }

  // if reporting disabled - show the current node's ip
  if (digitalRead(D6) == LOW) {
    if ((uint32_t)(now - last_lcd_ms) >= 2000UL) {
      last_lcd_ms = now;
      send_lcd("IP:", ip);
    }
    return;
  }

  if (active_count == 0) {
    send_lcd("No sensors", "");
    return;
  }

  // potentiometer selector
  const uint8_t raw_sel =
      active_count > 0
          ? (uint8_t)((uint32_t)analogRead(A0) * active_count / 1024)
          : 0;
  static uint8_t pending_sel = 255;
  static uint32_t pending_since = 0;
  if (raw_sel != pending_sel) {
    pending_sel = raw_sel;
    pending_since = now;
  }

  // if selection is stable - commit
  if ((uint32_t)(now - pending_since < 200UL)) return;

  const uint8_t sel = pending_sel;
  if (sel == prev_sel && (uint32_t)(now - last_lcd_ms) < 2'000UL) return;

  // no sensors
  last_lcd_ms = now;
  prev_sel = sel;

  const SensorEntry& e = registry[active_ids[sel]];
  char l1[LCD_COLS + 1], l2[LCD_COLS + 1];
  snprintf(l1, sizeof(l1), "S%d h:%d%% T:%dC", active_ids[sel], e.h, e.t);
  snprintf(l2, sizeof(l2), "M:%d UV:%d", e.m, e.uv);
  send_lcd(l1, l2);
}

#endif
