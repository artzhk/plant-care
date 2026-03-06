/**
 * role_sensor.cpp — Sensor node firmware (NODE_SENSOR builds only).
 *
 * Pinout (NodeMCU v2 / ESP8266):
 *   D1/GPIO5  — I2C SCL  (SSD1306 OLED)
 *   D2/GPIO4  — I2C SDA  (SSD1306 OLED, CD74HC4067)
 *   D3/GPIO0  — DHT11 data  (INPUT at boot → GPIO0 HIGH → normal boot mode)
 *   D5/GPIO14 — RGB Blue
 *   D6/GPIO12 — MUX S0  (LOW = C0 CJMCU UV, HIGH = C1 HW-390 moisture)
 *   D7/GPIO13 — RGB Green
 *   D8/GPIO15 — RGB Red  (NodeMCU pull-down keeps LOW at boot — safe)
 *   A0        — MUX SIG output (shared analog input)
 *
 * Sensor quirks:
 *   DHT11: readTemperatureHumidity() only — back-to-back individual reads
 *     violate the 1 s inter-read minimum and trigger a Soft WDT reset.
 *     Library returns -3 on checksum failure; last good value is retained.
 *
 *   CJMCU-GUVA-S12SD: bare photodiode, no op-amp. Output is ~10–15 ADC
 *     counts indoors (within noise floor); meaningful only in direct sunlight.
 *     Readings <= 20 treated as noise — last valid value retained.
 *
 *   HW-390: capacitive moisture sensor. Higher ADC = drier soil (inverted).
 *     map_adc_to_pct() in led.h handles the inversion transparently.
 *     Readings <= 100 discarded as ADC noise (WiFi TX burst artifact).
 *
 *   CD74HC4067 MUX: pin 15 (/E, active-low enable) must be tied to GND.
 *     A floating /E disables all channels, leaving A0 floating (~10 ADC).
 *
 * MQTT flow:
 *   First connection → publish_discovery() sends four retained HA discovery
 *   payloads to homeassistant/sensor/<id>/config.
 *   Every 2 s → push_state() publishes readings to TOPIC_STATE.
 *   MQTT down → HTTP POST fallback to master node (if master IP configured).
 *
 * RGB LED states (thresholds in led.h):
 *   Pulsing red — sensor values implausible (hardware / wiring fault)
 *   Blue        — soil too dry (threshold adjusted by temperature + humidity)
 *   Orange      — insufficient UV light
 *   Green       — all within acceptable range
 */
#if defined(NODE_SENSOR)

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Wire.h>

#include <cstdint>
#include <cstdio>

#include "DHT11.h"
#include "ESP8266HTTPClient.h"
#include "ESP8266WiFi.h"
#include "HardwareSerial.h"
#include "captive_portal.h"
#include "core_esp8266_features.h"
#include "led.h"
#include "light_CD74HC4067.h"
#include "role.h"
#include "wl_definitions.h"

// PINOUT:
// D2[5], D1[4]           - I2C (OLED, mux)
// D6[12]                 - MUX S0 (C0=UV, C1=moisture; S1-S3 tied to GND)
// D8[15], D5[14], D7[13] - RGB LED (red, green, blue)
// D3[0]                  - DHT11 data (INPUT at boot → GPIO0 HIGH → safe)

static uint8_t constexpr SCREEN_WIDTH = 128;
static uint8_t constexpr SCREEN_HEIGHT = 64;

char* cfg_ha_ip = nullptr;
char* cfg_master_ip = nullptr;

#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

static Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

static struct DHT11Out {
  int8_t humidity, temperature;
} dht_out = {};

/// Temperature, humidity sensor
struct DHT11Reader {
  explicit DHT11Reader(uint8_t p) : inst{p} {}
  bool operator()(DHT11Out& out) {
    // readHumidity() + readTemperature() each do a full 40-bit one-wire read.
    // DHT11 needs ≥1 s between reads; back-to-back calls hang the sensor's
    // response wait loop → Soft WDT after 3.2 s.
    // readTemperatureHumidity() does a single read returning both values.
    int t = 0, h = 0;
    const int rc = this->inst.readTemperatureHumidity(t, h);
    if (t == -3 || h == -3) {
      // DHT11 returns -3 on checksum failure; treat as read failure (keep last
      // good value).
      return 0;
    }

    out.temperature = (int8_t)t;
    out.humidity = (int8_t)h;
    return rc == 0;
  };

  DHT11 inst;
};

// light lib is used since it has more straightforward and clear implementation
// than the original one and GNU license UV-light sensor
static constexpr uint8_t MUX_CJMCU = 0;

struct CJMCUReader {
  explicit CJMCUReader(CD74HC4067& m) : mux{m} {}
  bool operator()(int16_t& out) {
    this->mux.channel(MUX_CJMCU);
    int16_t tmp = analogRead(A0);
    if (tmp > 20) {
      healthy_val = tmp;
    }
    out = healthy_val;
    return 1;
  };

 private:
  int16_t healthy_val = 0;
  CD74HC4067& mux;
};

/// Moisture sensor
static constexpr uint8_t MUX_HW390 = 1;
struct HW390Reader {
  explicit HW390Reader(CD74HC4067& m) : mux{m} {}
  bool operator()(int16_t& out) {
    this->mux.channel(MUX_HW390);
    int16_t tmp = analogRead(A0);
    if (tmp > 100) {
      healthy_val = tmp;
    }
    out = healthy_val;
    return 1;
  };

 private:
  int16_t healthy_val = 0;
  CD74HC4067& mux;
};

static DHT11Reader dht{D3};

// ANALOG sensor readers
static CD74HC4067 mux{12, -1, -1, -1};
static int16_t cjmcu_val = 0;
static CJMCUReader cjmcu{mux};

static int16_t hw390_val = 0;
static HW390Reader hw390{mux};

// static const char state_fmt[] PROGMEM =
//     "{\"h\":%d,\"t\":%d,\"id\":1,\"m\":%d,\"uv\":%d}";

static void send_to_upstream(const char* path, const char* json_body) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  char ip_buf[20];
  //   strcpy_P(ip_buf, IP);

#ifdef MASTER_IP_FIXED
  if (cfg_master_ip != nill)
    strncpy(ip_buf, cfg_master_ip, sizeof(ip_buf));
  else
    strncpy(ip_buf, _XSTR(MASTER_IP), sizeof(ip_buf));
#else
  strncpy(ip_buf, cfg_master_ip, sizeof(ip_buf));
#endif

  char url[128] = {};
  int len = snprintf(url, sizeof(url), "http://%s/%s", ip_buf, path);

  if (len <= 0 || len >= (int)sizeof(url)) {
    return;
  }

  if (!http_client.begin(net_client, url)) {
    return;
  }

  http_client.setTimeout(2500);  // default 5 s > Soft WDT limit
  http_client.addHeader("Content-Type", "application/json");
  int code = http_client.POST((uint8_t*)json_body, strlen(json_body));

  if (code > 0) {
    WiFiClient& stream = http_client.getStream();
    size_t n = stream.readBytes(resp_buf, sizeof(resp_buf) - 1);
    resp_buf[n] = '\0';
  }

  http_client.end();
}

static void publish_discovery() {
  char state_t[32], avail_t[32];
  strcpy_P(state_t, TOPIC_STATE);
  strcpy_P(avail_t, TOPIC_AVAIL);

  // DEV_TAIL is PROGMEM (globals.h); all JSON format strings are PROGMEM too —
  // use snprintf_P throughout, and %S for the PROGMEM dev tail.
  int n;

  n = snprintf_P(resp_buf, sizeof(resp_buf), HUMIDITY_JSON, state_t, avail_t,
                 DEV_TAIL);
  if (n > 0 && n < (int)sizeof(resp_buf)) {
    bool ok = mqtt.publish(
        "homeassistant/sensor/plants_sensor" _XSTR(NODE_ID) "_h/config",
        resp_buf, true);
  }

  n = snprintf_P(resp_buf, sizeof(resp_buf), TEMPERATURE_JSON, state_t, avail_t,
                 DEV_TAIL);
  if (n > 0 && n < (int)sizeof(resp_buf))
    mqtt.publish(
        "homeassistant/sensor/plants_sensor" _XSTR(NODE_ID) "_t/config",
        resp_buf, true);

  n = snprintf_P(resp_buf, sizeof(resp_buf), MOISTURE_JSON, state_t, avail_t,
                 DEV_TAIL);
  if (n > 0 && n < (int)sizeof(resp_buf))
    mqtt.publish(
        "homeassistant/sensor/plants_sensor" _XSTR(NODE_ID) "_m/config",
        resp_buf, true);

  n = snprintf_P(resp_buf, sizeof(resp_buf), UV_JSON, state_t, avail_t,
                 DEV_TAIL);
  if (n > 0 && n < (int)sizeof(resp_buf))
    mqtt.publish(
        "homeassistant/sensor/plants_sensor" _XSTR(NODE_ID) "_uv/config",
        resp_buf, true);
}

static bool validate_sensors() {
  // DHT
  if (dht_out.humidity < -3 || dht_out.humidity == -3) return false;
  // ANALOG noise floor is ~30; values below that are likely WiFi interference
  // or ADC issues rather than real readings
  if (hw390_val < 15 || cjmcu_val < 20) return false;
  return 1;
}

static void handle_status() {
  dht(dht_out);
  hw390(hw390_val);
  cjmcu(cjmcu_val);

  if (!validate_sensors()) {
    server.send(503, "text/plain", "Sensor values out of range");
    return;
  }

  // TODO: Map values
  int len =
      snprintf(resp_buf, sizeof(resp_buf),
               "{\"humidity\": %i, \"temperature\": %i, \"id\":%i, "
               "\"moisture\": %i, \"uv\": %i }",
               dht_out.humidity, dht_out.temperature, ID, hw390_val, cjmcu_val);

  // TODO: Sat Feb 14 08:08:03 CET 2026 Other sensor, consult the midterm to
  // form sensor ping dtos
  if (len < 0) {
    server.send(500, "text/plain", "Internal server error occured");
    return;
  }

  if (len >= (int)sizeof(resp_buf)) {
    len = sizeof(resp_buf) - 1;
    resp_buf[len] = '\0';
  }

  server.send(200, "application/json", resp_buf);
}

// TODO: display
static void display(const __FlashStringHelper* msg) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.setCursor(0, 0);
  oled.println(msg);
  oled.display();
}

static void display(const char* msg) {
  for (uint8_t i = 0; msg[i] != '\0'; ++i) {
    if (i == 255) {
      return;
    }
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.setCursor(0, 0);
  oled.println(msg);
  oled.display();
}

static void display() {
  // Pure stack – no heap allocation.
  // oled.printf() heap-allocs when output > 63 bytes; toString() also
  // heap-allocs. Both fragment the heap every 2 s and eventually crash the
  // board.
  char line[32];
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.setCursor(0, 0);
  oled.println(F("RAW DATA:"));
  snprintf(line, sizeof(line), "H:%i%% T:%iC", dht_out.humidity,
           dht_out.temperature);
  oled.println(line);
  snprintf(line, sizeof(line), "M:%d L:%d", hw390_val, cjmcu_val);
  oled.println(line);
  IPAddress ip = WiFi.localIP();
  snprintf(line, sizeof(line), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  oled.println(line);
  snprintf(line, sizeof(line), "Sensor ID: %s", _XSTR(NODE_ID));
  oled.println(line);
  snprintf(line, sizeof(line), "heap:%u", ESP.getFreeHeap());
  oled.println(line);
  oled.display();
}

static void read_sensors() {
  delay(500);
  DHT11Out tmp_dht;
  if (dht(tmp_dht)) dht_out = tmp_dht;  // keep last good reading on failure
  cjmcu(cjmcu_val);
  hw390(hw390_val);
}

static void rgb_update(uint32_t now_ms) {
  static uint32_t last_upd_ms = 0;

  if ((uint32_t)(now_ms - last_upd_ms) < LED_UPDATE_MS) return;
  last_upd_ms = now_ms;

  if (!analog_values_plausible(hw390_val, cjmcu_val)) {
    // Sensor error: pulsing red
    const uint16_t t = (uint16_t)(now_ms % 1000U);
    uint16_t amp = 0;
    if (t < 600U) {
      amp = t < 300U ? (uint16_t)((uint32_t)t * 1023U / 300U)
                     : (uint16_t)((uint32_t)(600U - t) * 1023U / 300U);
    }
    led_set_rgb(amp, 0, 0);
    return;
  }

  const uint8_t moisture_pct =
      map_adc_to_pct(hw390_val, MOISTURE_RAW_DRY, MOISTURE_RAW_WET, 0, 100);
  const uint8_t light_pct =
      map_adc_to_pct(cjmcu_val, LIGHT_RAW_DARK, LIGHT_RAW_BRIGHT, 0, 100);

  // Hot air dries soil faster → raise the threshold so blue triggers sooner.
  // Humid air slows evaporation → lower the threshold.
  uint8_t dry_thresh = MOISTURE_DRY_PCT;
  if (dht_out.temperature > 28)
    dry_thresh += 10;
  else if (dht_out.temperature < 15)
    dry_thresh = dry_thresh > 10 ? dry_thresh - 10 : 0;
  if (dht_out.humidity > 70) dry_thresh = dry_thresh > 5 ? dry_thresh - 5 : 0;

  const bool needs_water = moisture_pct < dry_thresh;
  const bool needs_light = light_pct < LIGHT_LOW_PCT;

  if (needs_water) {
    led_set_rgb(0, 0, 1023);  // blue: needs water (takes priority)
  } else if (needs_light) {
    led_set_rgb(1023, 420, 0);  // orange: needs light
  } else {
    led_set_rgb(0, 1023, 0);  // green: I'm fine
  }
}

static void push_state() {
  int len =
      snprintf(req_buf, sizeof(req_buf), cross_nodes_state_fmt,
               dht_out.humidity, dht_out.temperature, hw390_val, cjmcu_val);
  if (len <= 0 || len >= (int)sizeof(req_buf)) return;

  if (mqtt.connected()) {
    char topic[32];
    strcpy_P(topic, TOPIC_STATE);
    mqtt.publish(topic, req_buf);
    return;
  }
  // Only fall back to HTTP if MQTT was never available (cfg_master_ip set)
  if (cfg_master_ip && cfg_master_ip[0] != '\0')
    send_to_upstream("data", req_buf);
}

void role_setup() {
  Wire.begin(D2, D1);
  Wire.setClock(I2C_HZ);
  // TODO put this and other begin to the start of the function
  if (!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED alocation failed"));
    for (;;) delay(2000);
  }
  if (!load_config()) {
    start_config_portal([](const char* s) { display(s); });
  }

  cfg_ha_ip = node_cfg.ha_ip;
  cfg_master_ip = node_cfg.master_ip;

  // Setup RGB out
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  analogWriteRange(1023);
  led_set_rgb(0, 0, 0);

  Serial.begin(115200);
  Serial.print(F("Reset: "));
  Serial.println(ESP.getResetReason());
  {
    const rst_info* ri = ESP.getResetInfoPtr();
    if (ri->reason == REASON_EXCEPTION_RST)
      Serial.printf("  exception=%d epc1=0x%08x excvaddr=0x%08x\n",
                    ri->exccause, ri->epc1, ri->excvaddr);
  }

  display(F("Starting WIFI"));
  WiFi.begin(node_cfg.ssid, node_cfg.pass);

  // Awaiting for a wifi connect
  while (WiFi.status() != WL_CONNECTED) {
    display(F("Awaiting connection"));
    delay(2000);
    Serial.print(F("."));
  }

  // Serial print ip
  Serial.print(F("\nIP: "));
  Serial.print(WiFi.localIP());

  // Printing current nodes ip
  char ip_buf[16];
  IPAddress ip = WiFi.localIP();
  snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  display(ip_buf);

  // Setting up API endpoints
  server.on(F("/status"), HTTP_GET, handle_status);
  server.begin();
  mqtt_ensure_connected();
}

// TODO: Some additional private methods to post/preprocess dififrent sensors
// and logic
void rgb() { rgb_update(millis()); }

void role_loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(2000);
    return;
  }

  mqtt_ensure_connected();
  if (mqtt.connected()) {
    mqtt.loop();
    static bool discovery_done = false;
    if (!discovery_done) {
      publish_discovery();
      discovery_done = true;
    }
  }

  server.handleClient();

  static uint32_t last_push_ms = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - last_push_ms) >= 2000ul) {
    last_push_ms = now;
    read_sensors();
    rgb();
    push_state();
    display();
  }
}

#endif
