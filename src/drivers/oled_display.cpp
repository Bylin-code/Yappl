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
constexpr uint8_t kBarX = 10;
constexpr uint8_t kBarWidth = 108;
constexpr uint8_t kBarHeight = 12;

}  // namespace

bool OledDisplay::begin() {
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
                                     uint8_t ledBrightness) {
  if (lightLevel > 100) {
    lightLevel = 100;
  }
  if (micLevel > 100) {
    micLevel = 100;
  }

  const uint8_t lightFill = static_cast<uint8_t>((kBarWidth - 4) * lightLevel / 100);
  const uint8_t micFill = static_cast<uint8_t>((kBarWidth - 4) * micLevel / 100);
  const uint8_t ledLevel = static_cast<uint8_t>(ledBrightness * 100 / 255);
  const uint8_t ledFill = static_cast<uint8_t>((kBarWidth - 4) * ledLevel / 100);

  char line[24] = {};

  g_oled.clearBuffer();
  g_oled.drawFrame(0, 0, kDisplayWidth, kDisplayHeight);
  g_oled.setFont(u8g2_font_7x13B_tr);
  g_oled.drawStr(10, 18, "Yappl IO");

  g_oled.setFont(u8g2_font_6x12_tr);
  snprintf(line, sizeof(line), "Button: %s", buttonPressed ? "PRESSED" : "open");
  g_oled.drawStr(10, 34, line);

  snprintf(line, sizeof(line), "Light: %4d %3u%%", lightRaw, lightLevel);
  g_oled.drawStr(10, 50, line);
  g_oled.drawFrame(kBarX, 55, kBarWidth, kBarHeight);
  if (lightFill > 0) {
    g_oled.drawBox(kBarX + 2, 57, lightFill, kBarHeight - 4);
  }

  snprintf(line, sizeof(line), "Mic:          %3u%%", micLevel);
  g_oled.drawStr(10, 78, line);
  g_oled.drawFrame(kBarX, 83, kBarWidth, kBarHeight);
  if (micFill > 0) {
    g_oled.drawBox(kBarX + 2, 85, micFill, kBarHeight - 4);
  }

  snprintf(line, sizeof(line), "LED:%3u%%", ledLevel);
  g_oled.drawStr(10, 106, line);
  g_oled.drawFrame(58, 98, 60, kBarHeight);
  if (ledFill > 0) {
    const uint8_t compactFill = static_cast<uint8_t>((60 - 4) * ledLevel / 100);
    g_oled.drawBox(60, 100, compactFill, kBarHeight - 4);
  }

  g_oled.drawStr(10, 122, buttonPressed ? "Piezo: scale" : "Piezo: off");
  g_oled.sendBuffer();
}

}  // namespace yappl
