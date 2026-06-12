#include "drivers/oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "app/config.h"

namespace yappl {
namespace {

// HiLetgo's 1.5 inch 128x128 OLED uses an SH1107 controller. This panel maps
// correctly with U8g2's zero-offset Seeed 128x128 variant; the generic SH1107
// constructor applies a 96-column offset and causes quarter-screen wraparound.
U8G2_SH1107_SEEED_128X128_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE);

constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 128;
constexpr uint8_t kMeterX = 10;
constexpr uint8_t kMeterY = 76;
constexpr uint8_t kMeterWidth = 108;
constexpr uint8_t kMeterHeight = 18;
constexpr uint8_t kLabelX = 10;
constexpr uint8_t kPercentX = 38;
constexpr uint8_t kBarX = 66;
constexpr uint8_t kBarWidth = 52;
constexpr uint8_t kBarHeight = 9;
constexpr uint8_t kRowLightY = 62;
constexpr uint8_t kRowMicY = 82;
constexpr uint8_t kRowLedY = 102;

const char *noteName(uint16_t frequencyHz) {
  if (frequencyHz == 0) {
    return "--";
  }

  switch (frequencyHz) {
    case 262:
      return "C4";
    case 294:
      return "D4";
    case 330:
      return "E4";
    case 349:
      return "F4";
    case 392:
      return "G4";
    case 440:
      return "A4";
    case 494:
      return "B4";
    case 523:
      return "C5";
    default:
      return "??";
  }
}

void drawValueBar(uint8_t y, const char *label, uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }

  const uint8_t fillWidth = static_cast<uint8_t>((kBarWidth - 4) * percent / 100);
  char percentText[6] = {};

  g_oled.drawStr(kLabelX, y, label);
  snprintf(percentText, sizeof(percentText), "%3u%%", percent);
  g_oled.drawStr(kPercentX, y, percentText);
  g_oled.drawFrame(kBarX, y - 8, kBarWidth, kBarHeight);
  if (fillWidth > 0) {
    g_oled.drawBox(kBarX + 2, y - 6, fillWidth, kBarHeight - 4);
  }
}

}  // namespace

bool OledDisplay::begin() {
  // I2C uses explicit pins because the ESP32-S3 board's defaults are not the
  // pins used by this wiring.
  Wire.begin(AppConfig::oledSdaPin, AppConfig::oledSclPin);
  Wire.setClock(400000);

  // U8g2 expects the 8-bit I2C address form, so shift the 7-bit address left.
  g_oled.setI2CAddress(AppConfig::oledAddress << 1);
  g_oled.begin();
  g_oled.setContrast(180);
  clear();
  return true;
}

void OledDisplay::clear() {
  // Full-buffer U8g2 mode: draw into RAM first, then send to the display.
  g_oled.clearBuffer();
  g_oled.sendBuffer();
}

void OledDisplay::drawFrame() {
  g_oled.drawFrame(0, 0, kDisplayWidth, kDisplayHeight);
  g_oled.setFont(u8g2_font_7x13B_tr);
  g_oled.drawStr(10, 23, "Yappl");
  g_oled.setFont(u8g2_font_6x12_tr);
  g_oled.drawStr(10, 52, "Mic level");
}

void OledDisplay::drawMeter(uint8_t level) {
  if (level > 100) {
    level = 100;
  }

  uint8_t fillWidth = static_cast<uint8_t>((kMeterWidth - 4) * level / 100);
  if (level > 0 && fillWidth < 4) {
    fillWidth = 4;
  }

  g_oled.clearBuffer();
  drawFrame();
  g_oled.drawFrame(kMeterX, kMeterY, kMeterWidth, kMeterHeight);
  g_oled.drawBox(kMeterX + 2, kMeterY + 2, fillWidth, kMeterHeight - 4);

  char label[8] = {};
  snprintf(label, sizeof(label), "%3u%%", level);
  g_oled.setFont(u8g2_font_6x12_tr);
  g_oled.drawStr(92, 112, label);
  g_oled.sendBuffer();
}

void OledDisplay::drawHardwareStatus(bool buttonPressed,
                                     int lightRaw,
                                     uint8_t lightLevel,
                                     uint8_t micLevel,
                                     uint8_t ledBrightness,
                                     uint16_t piezoFrequencyHz) {
  (void)lightRaw;
  const uint8_t ledLevel = static_cast<uint8_t>(ledBrightness * 100 / 255);

  // Keep labels short because this is a 128x128 display.
  char line[24] = {};

  g_oled.clearBuffer();
  g_oled.drawFrame(0, 0, kDisplayWidth, kDisplayHeight);

  g_oled.setFont(u8g2_font_7x13B_tr);
  g_oled.drawStr(10, 16, "Yappl");

  g_oled.setFont(u8g2_font_6x12_tr);
  snprintf(line, sizeof(line), "Button: %s", buttonPressed ? "DOWN" : "UP");
  g_oled.drawStr(10, 34, line);

  snprintf(line, sizeof(line), "Piezo: %s", noteName(piezoFrequencyHz));
  g_oled.drawStr(10, 48, line);

  drawValueBar(kRowLightY, "Light", lightLevel);
  drawValueBar(kRowMicY, "Mic", micLevel);
  drawValueBar(kRowLedY, "LED", ledLevel);
  g_oled.sendBuffer();
}

}  // namespace yappl
