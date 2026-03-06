/**
 * led.h — RGB LED control and analog sensor calibration (NODE_SENSOR only).
 *
 * The RGB LED (common-cathode, active HIGH) uses 10-bit PWM resolution
 * (analogWriteRange(1023)). Set LED_ACTIVE_LOW = true for common-anode
 * wiring — no other code changes required.
 *
 * Calibration: MOISTURE_RAW_DRY/WET and LIGHT_RAW_DARK/BRIGHT must be
 * measured in the deployment environment. HW-390 is inverted (higher ADC =
 * drier); map_adc_to_pct() detects in_lo > in_hi and handles it correctly.
 *
 * LED states (evaluated in rgb_update() in role_sensor.cpp):
 *   Pulsing red — analog_values_plausible() failed (sensor / wiring fault)
 *   Blue        — moisture below dry threshold (needs water)
 *   Orange      — UV light below low threshold (needs more light)
 *   Green       — all conditions within acceptable range
 *
 * The dry threshold is adjusted at runtime by DHT11 readings:
 *   temp > 28°C  → +10% (hot air dries soil faster)
 *   temp < 15°C  → -10% (cold air, lower evaporation)
 *   humidity > 70% → -5% (humid air slows soil evaporation)
 */
#if defined(NODE_SENSOR)
#ifndef LED_H
#define LED_H

#include <ESP8266WebServer.h>

// D8/GPIO15: NodeMCU pull-down holds it LOW at boot regardless of firmware state
// D5/GPIO14: no boot constraints, freed by moving DHT11 to D3
// D3/GPIO0:  DHT11 moved here — DHT11 is INPUT at power-on → GPIO0 stays HIGH → safe boot
static uint8_t constexpr RED_LED_PIN = D8;
static uint8_t constexpr GREEN_LED_PIN = D7;
static uint8_t constexpr BLUE_LED_PIN = D5;

// Set to 1 if your RGB LED is common-anode (pins sink current / active LOW)
static constexpr bool LED_ACTIVE_LOW = false;

// Calibration: adjust to your sensors after one serial print session
// HW390: typical is WET lower ADC, DRY higher ADC (swap if yours is inverted)
static constexpr uint16_t MOISTURE_RAW_DRY = 850;
static constexpr uint16_t MOISTURE_RAW_WET = 300;

// CJMCU UV/light: DARK lower ADC, BRIGHT higher ADC (swap if inverted)
// Bare photodiode module (no op-amp): full range is ~15-1000 counts.
// The 9 to 20 is omstly noise, but it says no direct sunlight
// Recalibrate after measuring min/max in your actual environment.
static constexpr uint16_t LIGHT_RAW_DARK = 20;
static constexpr uint16_t LIGHT_RAW_BRIGHT = 1000;

// Thresholds in percent (0..100)
static constexpr uint8_t MOISTURE_DRY_PCT = 35;  // below → needs water
static constexpr uint8_t LIGHT_LOW_PCT    = 10;  // below → needs light

// Update cadence (non-blocking)
static constexpr uint32_t LED_UPDATE_MS = 20;
static constexpr uint32_t LED_READ_ANALOG_MS = 1000;

static inline uint16_t clamp_u16(uint32_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (uint16_t)v;
}

// Maps raw ADC to a percent range; handles inverted sensors (in_lo > in_hi).
static uint8_t map_adc_to_pct(uint16_t x, uint16_t in_lo, uint16_t in_hi,
                              uint8_t out_lo, uint8_t out_hi) {
  if (in_lo == in_hi) return out_lo;
  const bool inv = (in_lo > in_hi);
  if (inv) { uint16_t t = in_lo; in_lo = in_hi; in_hi = t; }
  if (x <= in_lo) return inv ? out_hi : out_lo;
  if (x >= in_hi) return inv ? out_lo : out_hi;
  const uint8_t r = (uint8_t)((uint32_t)(x - in_lo) * (out_hi - out_lo)
                               / (in_hi - in_lo) + out_lo);
  return inv ? (uint8_t)(out_hi - r + out_lo) : r;
}

static inline void led_write_raw(uint8_t pin, uint16_t v1023) {
  v1023 = clamp_u16(v1023, 0, 1023);
  if (LED_ACTIVE_LOW) v1023 = 1023 - v1023;
  analogWrite(pin, v1023);
}

static inline void led_set_rgb(uint16_t r, uint16_t g, uint16_t b) {
  led_write_raw(RED_LED_PIN, r);
  led_write_raw(GREEN_LED_PIN, g);
  led_write_raw(BLUE_LED_PIN, b);
}

static inline bool analog_values_plausible(uint16_t m, uint16_t l) {
  // Treat hard-rails as “missing/unplugged” signal (simple + cheap).
  // If you ever see legit 0/1023 in operation, widen these bounds.
  return (m > 5 && m < 1018 && l > 5 && l < 1018);
}

#endif
#endif
