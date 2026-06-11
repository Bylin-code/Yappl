#!/bin/sh
set -eu

# Upload a temporary firmware build that prints raw INMP441 samples as CSV,
# then open PlatformIO's serial monitor at 921600 baud.
#
# Optional:
#   scripts/mic_raw_stream.sh /dev/cu.usbmodemXXXX

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PIO="${PIO:-/Users/bradylin/.platformio/penv/bin/pio}"
PORT="${1:-}"

cd "$PROJECT_DIR"

if [ -n "$PORT" ]; then
  "$PIO" run -e mic-raw-stream --target upload --upload-port "$PORT"
  "$PIO" device monitor --port "$PORT" --baud 921600
else
  "$PIO" run -e mic-raw-stream --target upload
  "$PIO" device monitor --baud 921600
fi
