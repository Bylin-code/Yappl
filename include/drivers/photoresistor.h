#pragma once

#include <Arduino.h>

namespace yappl {

class Photoresistor {
 public:
  bool begin();
  int readRaw() const;

 private:
  bool started_ = false;
};

}  // namespace yappl
