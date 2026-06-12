#include <Arduino.h>

#include "app/yappl_app.h"

// Single global app object. Arduino calls setup()/loop(), but YapplApp starts
// the FreeRTOS tasks that do the real runtime work.
yappl::YapplApp app;

void setup() {
  app.begin();
}

void loop() {
  // Kept for Arduino compatibility. The app's update method intentionally
  // idles because output, sensor, and display work run in RTOS tasks.
  app.update();
}
