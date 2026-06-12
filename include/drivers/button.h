#pragma once

#include <Arduino.h>

namespace yappl {

// Minimal wrapper for the physical user button. The current circuit uses an
// external pulldown resistor, so pressed is HIGH.
class Button {
 public:
  bool begin();
  bool isPressed() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
