#pragma once

#include <Arduino.h>

namespace yappl {

// Reads the raw ADC value from the photoresistor voltage divider. Conversion to
// a normalized light percentage happens in the app layer.
class Photoresistor {
 public:
  bool begin();
  int readRaw() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
