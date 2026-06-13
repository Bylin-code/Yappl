#include "drivers/oled_display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "app/config.h"
#include "assets/face_bitmaps.h"

namespace yappl {
namespace {

// HiLetgo's 1.5 inch 128x128 OLED uses an SH1107 controller. This panel maps
// correctly with U8g2's zero-offset Seeed 128x128 variant.
U8G2_SH1107_SEEED_128X128_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE);

constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 128;

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

void OledDisplay::drawFaceFrame(const FaceFrame &frame) {
  const FaceBitmap &bitmap = faceBitmap(frame.bitmapId);
  const int16_t x = (kDisplayWidth - bitmap.width) / 2 + frame.xOffset;
  const int16_t y = (kDisplayHeight - bitmap.height) / 2 + frame.yOffset;

  g_oled.clearBuffer();
  if (frame.invert) {
    g_oled.drawBox(0, 0, kDisplayWidth, kDisplayHeight);
    g_oled.setDrawColor(0);
  }
  g_oled.drawXBMP(x, y, bitmap.width, bitmap.height, bitmap.data);
  if (frame.invert) {
    g_oled.setDrawColor(1);
  }
  g_oled.sendBuffer();
}

}  // namespace yappl
