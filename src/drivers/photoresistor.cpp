#include "drivers/photoresistor.h"

#include "app/config.h"

namespace yappl {

bool Photoresistor::begin() {
  // The ADC pin is high impedance; the external divider defines the voltage.
  pinMode(AppConfig::photoresistorPin, INPUT);
  started_ = true;
  return true;
}

int Photoresistor::readRaw() const {
  if (!started_) {
    return 0;
  }

  // Arduino returns the ESP32-S3 ADC reading as an integer, typically 0-4095.
  return analogRead(AppConfig::photoresistorPin);
}

}  // namespace yappl
