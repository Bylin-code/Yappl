#include "drivers/oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "app/config.h"

namespace yappl {
namespace {

// HiLetgo's 1.5 inch 128x128 OLED uses an SH1107 controller. Use U8g2's
// generic 128x128 SH1107 mapping, not the Seeed-specific mapping, because the
// Seeed variant uses a different column offset and can make the image wrap.
U8G2_SH1107_128X128_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE);

constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 128;
constexpr uint8_t kMeterX = 10;
constexpr uint8_t kMeterY = 76;
constexpr uint8_t kMeterWidth = 108;
constexpr uint8_t kMeterHeight = 18;

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

}  // namespace yappl
