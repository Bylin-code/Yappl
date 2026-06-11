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
import shutil
import sys
import time
from typing import Deque, Iterable, Optional

import serial
from serial.tools import list_ports


DEFAULT_BAUD = 921600
DEFAULT_HISTORY = 240
MIN_DRAW_INTERVAL = 0.05


def choose_port() -> Optional[str]:
    ports = list(list_ports.comports())
    if not ports:
        return None

    preferred = [
        port.device
        for port in ports
        if "usbmodem" in port.device.lower() or "usbserial" in port.device.lower()
    ]
    if preferred:
        return preferred[0]
    return ports[0].device


def parse_sample(line: bytes) -> Optional[int]:
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
    if maximum <= minimum:
        return rows // 2

    fraction = (value - minimum) / (maximum - minimum)
    fraction = max(0.0, min(1.0, fraction))
    return rows - 1 - int(round(fraction * (rows - 1)))


def downsample(values: Iterable[int], width: int) -> list[int]:
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


def draw(values: Deque[int], port: str, baud: int) -> None:
    terminal = shutil.get_terminal_size((100, 32))
    graph_width = max(20, terminal.columns - 2)
    graph_rows = max(8, terminal.lines - 8)
    plotted = downsample(values, graph_width)

    minimum = min(values) if values else -1
    maximum = max(values) if values else 1
    span = maximum - minimum

    # Keep zero visible when possible. That makes DC offset easier to spot.
    minimum = min(minimum, 0)
    maximum = max(maximum, 0)
    if minimum == maximum:
        minimum -= 1
        maximum += 1

    canvas = [[" " for _ in range(graph_width)] for _ in range(graph_rows)]
    zero_row = scaled_row(0, minimum, maximum, graph_rows)
    if 0 <= zero_row < graph_rows:
        for x in range(graph_width):
            canvas[zero_row][x] = "-"

    for x, value in enumerate(plotted):
        y = scaled_row(value, minimum, maximum, graph_rows)
        canvas[y][x] = "*"

    latest = values[-1] if values else 0

    sys.stdout.write("\x1b[H\x1b[2J")
    sys.stdout.write(f"Yappl mic live graph | port={port} baud={baud} | Ctrl+C to stop\n")
    sys.stdout.write(
        f"latest={latest} min={min(values)} max={max(values)} span={span} samples={len(values)}\n"
        if values
        else "waiting for samples...\n"
    )
    sys.stdout.write(f"scale top={maximum} bottom={minimum}\n")
    sys.stdout.write("+" + "-" * graph_width + "+\n")
    for row in canvas:
        sys.stdout.write("|" + "".join(row) + "|\n")
    sys.stdout.write("+" + "-" * graph_width + "+\n")
    sys.stdout.flush()


def main() -> int:
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
    last_draw = 0.0

    with serial.Serial(port, args.baud, timeout=0.1) as connection:
        connection.reset_input_buffer()
        while True:
            line = connection.readline()
            sample = parse_sample(line)
            if sample is not None:
                samples.append(sample)

            now = time.monotonic()
            if now - last_draw >= MIN_DRAW_INTERVAL:
                draw(samples, port, args.baud)
                last_draw = now


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nStopped.")
