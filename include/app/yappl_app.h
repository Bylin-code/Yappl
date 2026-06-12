#pragma once

#include <Arduino.h>
#include "drivers/button.h"
#include "drivers/inmp441_microphone.h"
#include "drivers/oled_display.h"
#include "drivers/photoresistor.h"
#include "drivers/piezo_buzzer.h"
#include "drivers/status_led.h"
#include "services/led_breather.h"
#include "services/piezo_scale_player.h"

namespace yappl {

class YapplApp {
 public:
  void begin();
  void update();

 private:
  OledDisplay display_;
  Inmp441Microphone mic_;
  StatusLed led_;
  PiezoBuzzer piezo_;
  Photoresistor photoresistor_;
  Button button_;
  LedBreather ledBreather_{led_};
  PiezoScalePlayer scalePlayer_{piezo_};
  int32_t micSamples_[256] = {};
  uint32_t lastSensorMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastLogMs_ = 0;
  int lightRaw_ = 0;
  uint8_t lightLevel_ = 0;
  uint8_t micLevel_ = 0;
  bool buttonPressed_ = false;
  bool displayReady_ = false;
  bool micReady_ = false;

  uint8_t lightLevelFromRaw(int raw) const;
  void updateSensors(uint32_t nowMs);
  void updateDisplay(uint32_t nowMs);
  void updateSerialLog(uint32_t nowMs);
};

}  // namespace yappl
