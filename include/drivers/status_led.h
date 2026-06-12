#pragma once

#include <Arduino.h>

namespace yappl {

class StatusLed {
 public:
  bool begin();
  void set(bool on);
  void setBrightness(uint8_t brightness);

 private:
  bool started_ = false;
};

}  // namespace yappl
