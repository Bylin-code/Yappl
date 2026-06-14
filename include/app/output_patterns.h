#pragma once

#include <Arduino.h>

#include "app/app_state.h"

namespace yappl {
namespace OutputPatterns {

// Convert the current product mode into desired hardware output values. These
// functions are pure calculations, so they are easy to tune without touching
// RTOS task code or driver code.
uint8_t ledBrightnessFor(AppMode mode, uint32_t modeStartedAtMs, uint32_t nowMs, const AppState &snapshot);
uint16_t piezoFrequencyFor(AppMode mode, uint32_t modeStartedAtMs, uint32_t nowMs);

}  // namespace OutputPatterns
}  // namespace yappl
