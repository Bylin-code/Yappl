#pragma once

#include <Arduino.h>

namespace yappl {

// Reads the raw ADC value from the photoresistor voltage divider. Conversion to
// a normalized light percentage happens in the app layer.
class Photoresistor {
 public:
  // Configure the ADC-capable GPIO as an input.
  bool begin();

  // Return the raw ADC reading. AppConfig maps this to a light percentage.
  int readRaw() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
