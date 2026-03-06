#if defined(DISPLAY)
#include <Arduino.h>
#include <LiquidCrystal.h>

#include "Arduino.h"
#include "Wire.h"
#include "globals.h"
#include "role.h"

static LiquidCrystal lcd(12, 11, 4, 5, 6, 7);
static constexpr uint8_t I2C_ADDR = 0x12;
static volatile bool g_frame_ready = false;

static char line1[LCD_COLS + 1];
static char line2[LCD_COLS + 1];

static void display(const char* a, const char* b) {
  lcd.setCursor(0, 0);
  lcd.print(a);
  lcd.setCursor(0, 1);
  lcd.print(b);
}

static void receive(int n) {
  if (n != LCD_COLS * 2) {
    while (Wire.available()) (void)Wire.read();
    return;
  }

  for (int i = 0; i < LCD_COLS; ++i) line1[i] = (char)Wire.read();
  for (int i = 0; i < LCD_COLS; ++i) line2[i] = (char)Wire.read();

  line1[LCD_COLS] = '\0';
  line2[LCD_COLS] = '\0';
  g_frame_ready = true;
}

void role_setup() {
  Serial.begin(9600);
  Serial.setTimeout(200);  // must exceed your sender’s worst-case gap
  lcd.begin(16, 2);

  lcd.clear();
  lcd.print("Nano read");

  Wire.begin(I2C_ADDR);
  Wire.onReceive(receive);
}

void role_loop() {
  if (!g_frame_ready) return;
  noInterrupts();
  g_frame_ready = false;
  interrupts();
  display(line1, line2);
}
#endif
