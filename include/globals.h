/**
 * globals.h — Shared declarations for NODE_SENSOR and NODE_MASTER builds.
 *
 * Network objects (mqtt, server, http_client, net_client) and I/O buffers
 * are declared `extern` here and defined exactly once in utils.cpp.
 * Declaring them `static` in a header would give each translation unit its
 * own private copy — utils.cpp would connect one mqtt instance while
 * role_sensor.cpp would operate on a separate, never-connected one.
 *
 * PROGMEM strings live in flash to preserve the ESP8266's ~45 KB heap.
 * They must be copied to RAM before use (strcpy_P / snprintf_P).
 *
 * _XSTR(NODE_ID) forces two expansion steps: first the macro value is
 * expanded (e.g. 2), then stringified ("2"). A single _STR(NODE_ID)
 * would produce the literal token "NODE_ID" instead.
 */
#ifndef GLOBALS_H
#define GLOBALS_H
#include "config_check.h"

// Two-level stringify: expands the macro first, then quotes it.
//   -DNODE_ID=2  →  _XSTR(NODE_ID)  →  "2"
#define _STR(x) #x
#define _XSTR(x) _STR(x)

static constexpr uint8_t LCD_COLS = 16;

#if defined(NODE_SENSOR) || defined(NODE_MASTER)
static constexpr uint32_t I2C_HZ = 100000;

#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>

#include <cstdint>

// nodes MQTT account
static const char MQTT_USER[] PROGMEM = "mqtt";
static const char MQTT_PASS[] PROGMEM = "3751";

static const char cross_nodes_state_fmt[] =
    "{\"h\":%d,\"t\":%d,\"id\":" _XSTR(NODE_ID) ",\"m\":%d,\"uv\":%d}";

// Runtime IPs — loaded from EEPROM at boot.
extern char* cfg_ha_ip;

#if defined(NODE_SENSOR)
extern char* cfg_master_ip;
static constexpr uint8_t ID = NODE_ID;
static const char MQTT_CLIENT_ID[] PROGMEM = "sensor" _XSTR(NODE_ID);

static const char DEV_TAIL[] PROGMEM =
    "\"dev\":{\"ids\":[\"plants_sensor" _XSTR(NODE_ID) "\"],"
    "\"name\":\"Plants Sensor " _XSTR(NODE_ID) "\","
    "\"mdl\":\"ESP8266\",\"mf\":\"Custom\",\"sw\":\"arduino-mqtt-2\"}}";

static const char HUMIDITY_JSON[] PROGMEM =
    "{\"name\":\"Plant Humidity\","
    "\"uniq_id\":\"plants_sensor" _XSTR(NODE_ID) "_h\","
    "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.h}}\","
    "\"unit_of_meas\":\"%%\",\"dev_cla\":\"humidity\","
    "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",%S";

static const char TEMPERATURE_JSON[] PROGMEM =
    "{\"name\":\"Plant Temperature\","
    "\"uniq_id\":\"plants_sensor" _XSTR(NODE_ID) "_t\","
    "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.t}}\","
    "\"unit_of_meas\":\"\xc2\xb0""C\",\"dev_cla\":\"temperature\","
    "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",%S";

static const char MOISTURE_JSON[] PROGMEM =
    "{\"name\":\"Plant Moisture\","
    "\"uniq_id\":\"plants_sensor" _XSTR(NODE_ID) "_m\","
    "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.m}}\","
    "\"unit_of_meas\":\"%%\",\"dev_cla\":\"moisture\","
    "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",%S";

static const char UV_JSON[] PROGMEM =
    "{\"name\":\"Plant UV\","
    "\"uniq_id\":\"plants_sensor" _XSTR(NODE_ID) "_uv\","
    "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.uv}}\","
    "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",%S";
#endif  // NODE_SENSOR

#if defined(NODE_MASTER)
// this master node's own address.
static const char MQTT_CLIENT_ID[] PROGMEM = "master";
#endif  // NODE_MASTER

// cfg_ha_ip is defined in role_sensor.cpp / role_master.cpp and used here via
// extern above.
static const uint16_t MQTT_PORT PROGMEM = 1883;
static const char TOPIC_AVAIL[] PROGMEM =
    "plants/sensor/" _XSTR(NODE_ID) "/availability";
static const char TOPIC_STATE[] PROGMEM =
    "plants/sensor/" _XSTR(NODE_ID) "/state";
static const char TOPIC_DISP[] PROGMEM = "plants/sensor/display";

extern uint32_t next_mqtt_retry_ms;
extern ESP8266WebServer server;
extern HTTPClient http_client;
extern WiFiClient net_client;
extern PubSubClient mqtt;

extern char req_buf[512];
extern char resp_buf[512];

// defined in utils.cpp
void mqtt_ensure_connected();
#endif  // NODE_SENSOR || NODE_MASTER
#endif  // GLOBALS_H
