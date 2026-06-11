#!/bin/sh
set -eu

# Upload the mic debug firmware, then show a live terminal graph of the
# shifted 24-bit INMP441 samples.
#
# Optional:
#   scripts/mic_live_graph.sh /dev/cu.usbmodemXXXX

PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PIO="${PIO:-/Users/bradylin/.platformio/penv/bin/pio}"
PYTHON="${PYTHON:-/Users/bradylin/.platformio/penv/bin/python}"
PORT="${1:-}"

cd "$PROJECT_DIR"

if [ -n "$PORT" ]; then
  echo "Uploading mic-raw-stream debug firmware. OLED meter will not run in this mode."
  "$PIO" run -e mic-raw-stream --target upload --upload-port "$PORT"
  "$PYTHON" scripts/mic_live_graph.py "$PORT"
else
  echo "Uploading mic-raw-stream debug firmware. OLED meter will not run in this mode."
  "$PIO" run -e mic-raw-stream --target upload
  "$PYTHON" scripts/mic_live_graph.py
fi
