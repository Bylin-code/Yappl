#include "drivers/button.h"

#include "app/config.h"

namespace yappl {

bool Button::begin() {
  // The hardware has an external pulldown, so pressed reads HIGH.
  pinMode(AppConfig::buttonPin, INPUT);
  started_ = true;
  return true;
}

bool Button::isPressed() const {
  if (!started_) {
    // A not-yet-started driver should fail safe as "not pressed."
    return false;
  }

  return digitalRead(AppConfig::buttonPin) == HIGH;
}

}  // namespace yappl
