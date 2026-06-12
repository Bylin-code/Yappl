#pragma once

#include <Arduino.h>

namespace yappl {

class Button {
 public:
  bool begin();
  bool isPressed() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
