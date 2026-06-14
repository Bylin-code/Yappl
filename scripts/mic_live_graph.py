#!/usr/bin/env python3
"""Live terminal graph for Yappl INMP441 CSV debug output.

The mic debug firmware prints:
  block_ms,sample_index,raw_i32,shifted_i24

This script reads those lines from serial and draws a rolling graph of the
shifted 24-bit sample values. It intentionally avoids matplotlib or GUI
dependencies so it works inside PlatformIO's Python environment.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import shutil
import sys
import time
from typing import Deque, Iterable, Optional

import serial
from serial.tools import list_ports


DEFAULT_BAUD = 921600
DEFAULT_HISTORY = 240
MIN_DRAW_INTERVAL = 0.05


@dataclasses.dataclass
class SessionStats:
    """Small state object that remembers useful values across the whole run."""

    samples_seen: int = 0
    session_min_volume: Optional[int] = None
    session_max_volume: int = 0
    latest_sample: int = 0
    latest_volume: int = 0

    def add_sample(self, sample: int) -> None:
        """Record one signed sample and update the live/session statistics."""
        volume = abs(sample)
        self.samples_seen += 1
        self.latest_sample = sample
        self.latest_volume = volume
        self.session_max_volume = max(self.session_max_volume, volume)
        if self.session_min_volume is None:
            self.session_min_volume = volume
        else:
            self.session_min_volume = min(self.session_min_volume, volume)


def choose_port() -> Optional[str]:
    """Pick a likely ESP32 serial port when the user does not pass one in."""
    ports = list(list_ports.comports())
    if not ports:
        return None

    # macOS ESP32 boards usually appear as usbmodem or usbserial devices.
    preferred = [
        port.device
        for port in ports
        if "usbmodem" in port.device.lower() or "usbserial" in port.device.lower()
    ]
    if preferred:
        return preferred[0]
    return ports[0].device


def parse_sample(line: bytes) -> Optional[int]:
    """Extract the shifted 24-bit mic sample from one CSV line of firmware output."""
    try:
        text = line.decode("utf-8", errors="replace").strip()
    except UnicodeDecodeError:
        return None

    parts = text.split(",")
    if len(parts) != 4:
        return None

    try:
        return int(parts[3])
    except ValueError:
        return None


def scaled_row(value: int, minimum: int, maximum: int, rows: int) -> int:
    """Map a sample value into a terminal row number for the ASCII graph."""
    if maximum <= minimum:
        return rows // 2

    fraction = (value - minimum) / (maximum - minimum)
    fraction = max(0.0, min(1.0, fraction))
    return rows - 1 - int(round(fraction * (rows - 1)))


def downsample(values: Iterable[int], width: int) -> list[int]:
    """Compress a long sample history down to one value per terminal column."""
    values = list(values)
    if len(values) <= width:
        return values

    step = len(values) / width
    output: list[int] = []
    for column in range(width):
        start = int(column * step)
        end = max(start + 1, int((column + 1) * step))
        bucket = values[start:end]
        output.append(sum(bucket) // len(bucket))
    return output


def level_bar(value: int, maximum: int, width: int) -> str:
    """Render a compact text progress bar for volume-like values."""
    if maximum <= 0:
        filled = 0
    else:
        filled = int(round(width * min(value, maximum) / maximum))
    return "[" + "#" * filled + "." * (width - filled) + "]"


def draw(values: Deque[int], stats: SessionStats, port: str, baud: int) -> None:
    """Redraw the whole terminal with current mic stats and a rolling waveform."""
    terminal = shutil.get_terminal_size((100, 32))
    graph_width = max(20, terminal.columns - 2)
    graph_rows = max(8, terminal.lines - 13)
    plotted = downsample(values, graph_width)

    rolling_sample_min = min(values) if values else 0
    rolling_sample_max = max(values) if values else 0
    rolling_span = rolling_sample_max - rolling_sample_min
    rolling_volume_min = min((abs(value) for value in values), default=0)
    rolling_volume_max = max((abs(value) for value in values), default=0)
    session_min_volume = stats.session_min_volume if stats.session_min_volume is not None else 0

    # Keep zero visible when possible. That makes DC offset easier to spot.
    graph_min = min(rolling_sample_min, 0)
    graph_max = max(rolling_sample_max, 0)
    if graph_min == graph_max:
        graph_min -= 1
        graph_max += 1

    # Build the graph in memory first, then print it in one screen refresh.
    canvas = [[" " for _ in range(graph_width)] for _ in range(graph_rows)]
    zero_row = scaled_row(0, graph_min, graph_max, graph_rows)
    if 0 <= zero_row < graph_rows:
        for x in range(graph_width):
            canvas[zero_row][x] = "-"

    for x, value in enumerate(plotted):
        y = scaled_row(value, graph_min, graph_max, graph_rows)
        canvas[y][x] = "*"

    bar_width = min(40, max(12, terminal.columns - 42))
    volume_scale = max(stats.session_max_volume, rolling_volume_max, 1)

    sys.stdout.write("\x1b[H\x1b[2J")
    sys.stdout.write("Yappl Mic Live Graph\n")
    sys.stdout.write(f"Port: {port}   Baud: {baud}   Stop: Ctrl+C\n")
    sys.stdout.write("Signal: shifted 24-bit INMP441 sample values\n\n")

    if not values:
        sys.stdout.write("Waiting for samples from ESP32-S3...\n")
        sys.stdout.flush()
        return

    sys.stdout.write(
        f"Latest sample value:        {stats.latest_sample:>12}   "
        f"signed waveform point\n"
    )
    sys.stdout.write(
        f"Latest volume:              {stats.latest_volume:>12}   "
        f"{level_bar(stats.latest_volume, volume_scale, bar_width)}\n"
    )
    sys.stdout.write(
        f"Rolling sample minimum:     {rolling_sample_min:>12}   "
        f"last {len(values)} plotted samples\n"
    )
    sys.stdout.write(
        f"Rolling sample maximum:     {rolling_sample_max:>12}   "
        f"last {len(values)} plotted samples\n"
    )
    sys.stdout.write(
        f"Rolling sample span:        {rolling_span:>12}   "
        f"max - min over visible window\n"
    )
    sys.stdout.write(
        f"Session minimum volume:     {session_min_volume:>12}   "
        f"quietest absolute sample seen\n"
    )
    sys.stdout.write(
        f"Session maximum volume:     {stats.session_max_volume:>12}   "
        f"{level_bar(stats.session_max_volume, volume_scale, bar_width)}\n"
    )
    sys.stdout.write(
        f"Rolling volume min/max:     {rolling_volume_min:>12} / {rolling_volume_max:<12}   "
        f"absolute sample range in window\n"
    )
    sys.stdout.write(f"Total samples received:     {stats.samples_seen:>12}\n")
    sys.stdout.write(f"Graph scale top/bottom:     {graph_max:>12} / {graph_min:<12}\n")
    sys.stdout.write("+" + "-" * graph_width + "+\n")
    for row in canvas:
        sys.stdout.write("|" + "".join(row) + "|\n")
    sys.stdout.write("+" + "-" * graph_width + "+\n")
    sys.stdout.flush()


def main() -> int:
    """Open serial, read CSV forever, and periodically redraw the graph."""
    parser = argparse.ArgumentParser(description="Live graph for Yappl mic serial CSV output.")
    parser.add_argument("port", nargs="?", help="Serial port, for example /dev/cu.usbmodemXXXX")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--history", type=int, default=DEFAULT_HISTORY)
    args = parser.parse_args()

    port = args.port or choose_port()
    if not port:
        print("No serial port found. Connect the ESP32-S3 or pass /dev/cu.usbmodemXXXX.", file=sys.stderr)
        return 1

    samples: Deque[int] = collections.deque(maxlen=args.history)
    stats = SessionStats()
    last_draw = 0.0

    with serial.Serial(port, args.baud, timeout=0.1) as connection:
        # Drop bootloader noise and old buffered lines so the graph starts fresh.
        connection.reset_input_buffer()
        while True:
            line = connection.readline()
            sample = parse_sample(line)
            if sample is not None:
                samples.append(sample)
                stats.add_sample(sample)

            now = time.monotonic()
            if now - last_draw >= MIN_DRAW_INTERVAL:
                draw(samples, stats, port, args.baud)
                last_draw = now


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nStopped.")
