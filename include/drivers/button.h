#pragma once

#include <Arduino.h>

namespace yappl {

// Minimal wrapper for the physical user button. The current circuit uses an
// external pulldown resistor, so pressed is HIGH.
class Button {
 public:
  // Configure the button GPIO as a plain input.
  bool begin();

  // Return true when the external pulldown circuit is pulled up by a press.
  bool isPressed() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
