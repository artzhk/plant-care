/**
 * utils.cpp — MQTT lifecycle management and shared object definitions.
 *
 * Owns the single definition of all extern objects declared in globals.h
 * (mqtt, server, http_client, net_client, req_buf, resp_buf). Centralising
 * them here makes ownership unambiguous and prevents multiple-definition
 * linker errors.
 *
 * mqtt_ensure_connected() is the only MQTT entry point for role files.
 * It is non-blocking: next_mqtt_retry_ms gates reconnect attempts to once
 * every 5 s, avoiding reconnect storms without any delay() call.
 *
 * mqtt.setSocketTimeout(2) is critical on ESP8266: the default 15 s CONNACK
 * wait exceeds the Soft WDT limit (3.2 s), causing a reset before the
 * connection can be established or rejected gracefully.
 */
#if defined(NODE_MASTER) || defined(NODE_SENSOR)
#include <PubSubClient.h>

#include <cstdint>
#include <cstdio>

#include "ESP8266WebServer.h"
#include "HardwareSerial.h"
#include "WiFiClient.h"
#include "globals.h"

ESP8266WebServer server(80);
HTTPClient http_client;
WiFiClient net_client;
PubSubClient mqtt;
char req_buf[512];
char resp_buf[512];
uint32_t next_mqtt_retry_ms = 0;

static void mqtt_connect_blocking_once() {
  static char host_buf[20];  // static: mqtt.setServer() stores this pointer;
                             // must outlive the call
  char client_buf[10], user_buf[8], pass_buf[8], avail_buf[32];

#ifdef HA_IP_FIXED
  if (cfg_ha_ip != nullptr)
    strncpy(host_buf, cfg_ha_ip, sizeof(host_buf));
  else
    strncpy(host_buf, _XSTR(HA_IP), sizeof(host_buf));
#else
  strncpy(host_buf, cfg_ha_ip, sizeof(host_buf));
#endif
  strcpy_P(client_buf, MQTT_CLIENT_ID);
  strcpy_P(user_buf, MQTT_USER);
  strcpy_P(pass_buf, MQTT_PASS);
  strcpy_P(avail_buf, TOPIC_AVAIL);

  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(2);  // default 15 s CONNACK wait >> 3.2 s Soft WDT
  mqtt.setClient(net_client);
  mqtt.setServer(host_buf, MQTT_PORT);

  // LWT: offline if connection drops
  const bool ok = mqtt.connect(client_buf, user_buf, pass_buf, avail_buf, 1,
                               true, "offline");

  if (!ok) return;

  mqtt.publish(avail_buf, "online", true);
}

void mqtt_ensure_connected() {
  if (mqtt.connected()) return;

  const uint32_t now = millis();
  if ((int32_t)(now - next_mqtt_retry_ms) < 0) return;

  mqtt_connect_blocking_once();

  // retry every 5s; simple and good enough
  next_mqtt_retry_ms = now + 5000;
}

#endif
