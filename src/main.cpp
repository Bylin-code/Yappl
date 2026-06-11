#include <Arduino.h>

#include "app/yappl_app.h"

yappl::YapplApp app;

void setup() {
  app.begin();
}

void loop() {
  app.update();
}
