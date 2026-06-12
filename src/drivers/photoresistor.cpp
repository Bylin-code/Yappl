#include "drivers/photoresistor.h"

#include "app/config.h"

namespace yappl {

bool Photoresistor::begin() {
  pinMode(AppConfig::photoresistorPin, INPUT);
  started_ = true;
  return true;
}

int Photoresistor::readRaw() const {
  if (!started_) {
    return 0;
  }

  return analogRead(AppConfig::photoresistorPin);
}

}  // namespace yappl
