#pragma once

#include <Arduino.h>
#include "drivers/inmp441_microphone.h"
#include "drivers/max98357a_amp.h"
#include "drivers/oled_display.h"

namespace yappl {

class YapplApp {
 public:
  void begin();
  void update();

 private:
  OledDisplay display_;
  Inmp441Microphone mic_;
  Max98357aAmp amp_;
  int32_t micSamples_[256] = {};
  uint32_t lastFrameMs_ = 0;
  bool displayReady_ = false;
  bool micReady_ = false;

#ifdef YAPPL_MIC_RAW_STREAM
  void streamRawMicBlock();
#endif
};

}  // namespace yappl
