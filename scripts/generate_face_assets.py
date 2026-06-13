#!/usr/bin/env python3
"""Generate placeholder Yappl face bitmaps and animation metadata.

The generated assets are intentionally simple 1-bit XBM-style frames that U8g2
can draw directly. This keeps the firmware usable before custom pixel art
exists, and gives future hand-drawn assets a concrete data shape to replace.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
WIDTH = 96
HEIGHT = 64


@dataclass(frozen=True)
class FrameDef:
    bitmap_id: str
    duration_ms: int
    x_offset: int = 0
    y_offset: int = 0
    invert: bool = False


@dataclass(frozen=True)
class ActDef:
    act_id: str
    name: str
    frames: tuple[FrameDef, ...]
    loop: bool
    min_downtime_ms: int
    chance_percent: int


class Canvas:
    def __init__(self, width: int = WIDTH, height: int = HEIGHT) -> None:
        self.width = width
        self.height = height
        self.pixels = [[0 for _ in range(width)] for _ in range(height)]

    def set(self, x: int, y: int, value: int = 1) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x] = 1 if value else 0

    def ellipse(self, cx: int, cy: int, rx: int, ry: int, value: int = 1) -> None:
        if rx <= 0 or ry <= 0:
            return
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, value)

    def rect(self, x: int, y: int, w: int, h: int, value: int = 1) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, value)

    def line(self, x0: int, y0: int, x1: int, y1: int, value: int = 1) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set(x0, y0, value)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def text(self, x: int, y: int, text: str, scale: int = 1) -> None:
        cursor = x
        for char in text.upper():
            glyph = FONT_5X7.get(char, FONT_5X7[" "])
            for row, bits in enumerate(glyph):
                for col in range(5):
                    if bits & (1 << (4 - col)):
                        self.rect(cursor + col * scale, y + row * scale, scale, scale, 1)
            cursor += 6 * scale

    def to_xbm_bytes(self) -> list[int]:
        data: list[int] = []
        for y in range(self.height):
            for byte_x in range((self.width + 7) // 8):
                byte = 0
                for bit in range(8):
                    x = byte_x * 8 + bit
                    if x < self.width and self.pixels[y][x]:
                        byte |= 1 << bit
                data.append(byte)
        return data


FONT_5X7: dict[str, tuple[int, ...]] = {
    " ": (0, 0, 0, 0, 0, 0, 0),
    "!": (0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100, 0),
    "D": (0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110),
    "G": (0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110),
    "H": (0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001),
    "I": (0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111),
    "N": (0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001),
    "O": (0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110),
    "T": (0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100),
    "Z": (0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111),
}


def draw_open_eyes(
    pupil_x: int = 0,
    pupil_y: int = 0,
    eye_h: int = 18,
    droop: int = 0,
    sweat: bool = False,
    sparkle: bool = False,
) -> Canvas:
    c = Canvas()
    left = (31, 32)
    right = (65, 32)
    for cx, cy in (left, right):
        c.ellipse(cx, cy + droop, 18, eye_h, 1)
        c.ellipse(cx + pupil_x, cy + pupil_y + droop, 6, 8, 0)
        c.ellipse(cx + pupil_x - 2, cy + pupil_y - 3 + droop, 2, 2, 1)
        if droop > 0:
            c.rect(cx - 18, cy - eye_h + droop - 1, 37, droop + 4, 0)
            c.line(cx - 15, cy - eye_h + droop + 2, cx + 15, cy - eye_h + droop + 6, 1)
    if sweat:
        c.ellipse(82, 23, 3, 5, 1)
        c.ellipse(82, 21, 1, 2, 0)
    if sparkle:
        c.line(16, 16, 16, 24, 1)
        c.line(12, 20, 20, 20, 1)
        c.line(80, 46, 80, 54, 1)
        c.line(76, 50, 84, 50, 1)
    return c


def draw_sleep_eyes(zzz_count: int = 0, good_night: bool = False, closed_y: int = 34) -> Canvas:
    c = Canvas()
    c.line(16, closed_y, 42, closed_y + 2, 1)
    c.line(54, closed_y + 2, 80, closed_y, 1)
    c.line(18, closed_y + 1, 20, closed_y + 4, 1)
    c.line(34, closed_y + 2, 36, closed_y + 5, 1)
    c.line(62, closed_y + 2, 64, closed_y + 5, 1)
    c.line(76, closed_y + 1, 78, closed_y + 4, 1)
    for i in range(zzz_count):
        c.text(66 + i * 9, 8 + i * 7, "Z", 1)
    if good_night:
        c.text(13, 50, "GOOD NIGHT!", 1)
    return c


def make_bitmaps() -> dict[str, Canvas]:
    return {
        "EyesStraight": draw_open_eyes(),
        "EyesBlinkHalf": draw_open_eyes(eye_h=9),
        "EyesBlinkClosed": draw_sleep_eyes(closed_y=32),
        "EyesLookLeft": draw_open_eyes(pupil_x=-8),
        "EyesLookRight": draw_open_eyes(pupil_x=8),
        "EyesAnxiousLeft": draw_open_eyes(pupil_x=-7, droop=7, sweat=True),
        "EyesAnxiousRight": draw_open_eyes(pupil_x=7, droop=7, sweat=True),
        "EyesNotYetLeft": draw_open_eyes(pupil_x=-7, eye_h=13, droop=4),
        "EyesNotYetRight": draw_open_eyes(pupil_x=7, eye_h=13, droop=4),
        "EyesDanceUp": draw_open_eyes(pupil_y=-6, sparkle=True),
        "EyesDanceDown": draw_open_eyes(pupil_y=6, sparkle=True),
        "EyesDanceLeft": draw_open_eyes(pupil_x=-7, sparkle=True),
        "EyesDanceRight": draw_open_eyes(pupil_x=7, sparkle=True),
        "EyesNodUp": draw_open_eyes(pupil_y=-5),
        "EyesNodDown": draw_open_eyes(pupil_y=5),
        "EyesSleepy1": draw_open_eyes(eye_h=12, droop=5),
        "EyesSleepy2": draw_open_eyes(eye_h=7, droop=8),
        "EyesSleepClosed": draw_sleep_eyes(),
        "EyesGoodNight": draw_sleep_eyes(good_night=True),
        "EyesNightZ1": draw_sleep_eyes(zzz_count=1),
        "EyesNightZ2": draw_sleep_eyes(zzz_count=2),
        "EyesNightZ3": draw_sleep_eyes(zzz_count=3),
    }


ACTS: tuple[ActDef, ...] = (
    ActDef("IdleStraight", "idle_straight", (FrameDef("EyesStraight", 1000),), True, 0, 0),
    ActDef(
        "Blink",
        "blink",
        (
            FrameDef("EyesStraight", 80),
            FrameDef("EyesBlinkHalf", 70),
            FrameDef("EyesBlinkClosed", 90),
            FrameDef("EyesBlinkHalf", 70),
            FrameDef("EyesStraight", 120),
        ),
        False,
        2500,
        18,
    ),
    ActDef(
        "LookLeftRight",
        "look_left_right",
        (
            FrameDef("EyesStraight", 150),
            FrameDef("EyesLookLeft", 550),
            FrameDef("EyesStraight", 180),
            FrameDef("EyesLookRight", 550),
            FrameDef("EyesStraight", 220),
        ),
        False,
        5000,
        8,
    ),
    ActDef(
        "ReminderAnxious",
        "reminder_anxious",
        (
            FrameDef("EyesAnxiousLeft", 450),
            FrameDef("EyesAnxiousRight", 450),
        ),
        True,
        0,
        0,
    ),
    ActDef(
        "ReminderShake",
        "reminder_shake",
        (
            FrameDef("EyesAnxiousLeft", 70, -2, 0),
            FrameDef("EyesAnxiousRight", 70, 2, 0),
            FrameDef("EyesAnxiousLeft", 70, -2, 0),
            FrameDef("EyesAnxiousRight", 70, 2, 0),
        ),
        False,
        1500,
        35,
    ),
    ActDef(
        "NotYetHeadShake",
        "not_yet_headshake",
        (
            FrameDef("EyesNotYetLeft", 120, -3, 0),
            FrameDef("EyesNotYetRight", 120, 3, 0),
            FrameDef("EyesNotYetLeft", 120, -3, 0),
            FrameDef("EyesNotYetRight", 120, 3, 0),
        ),
        False,
        0,
        0,
    ),
    ActDef(
        "ActivationDance",
        "activation_dance",
        (
            FrameDef("EyesDanceUp", 120, 0, -2),
            FrameDef("EyesDanceRight", 120, 2, 0),
            FrameDef("EyesDanceDown", 120, 0, 2),
            FrameDef("EyesDanceLeft", 120, -2, 0),
        ),
        True,
        0,
        0,
    ),
    ActDef("ListeningStraight", "listening_straight", (FrameDef("EyesStraight", 1000),), True, 0, 0),
    ActDef(
        "ListeningNod",
        "listening_nod",
        (
            FrameDef("EyesNodUp", 140, 0, -2),
            FrameDef("EyesStraight", 100),
            FrameDef("EyesNodDown", 140, 0, 2),
            FrameDef("EyesStraight", 160),
        ),
        False,
        2500,
        16,
    ),
    ActDef(
        "DeactivationSleep",
        "deactivation_sleep",
        (
            FrameDef("EyesStraight", 300),
            FrameDef("EyesSleepy1", 450),
            FrameDef("EyesSleepy2", 450),
            FrameDef("EyesSleepClosed", 500),
            FrameDef("EyesGoodNight", 250),
            FrameDef("EyesSleepClosed", 180),
            FrameDef("EyesGoodNight", 250),
            FrameDef("EyesSleepClosed", 180),
            FrameDef("EyesGoodNight", 250),
        ),
        False,
        0,
        0,
    ),
    ActDef(
        "IdleNightSleep",
        "idle_night_sleep",
        (
            FrameDef("EyesSleepClosed", 600),
            FrameDef("EyesNightZ1", 500),
            FrameDef("EyesNightZ2", 500),
            FrameDef("EyesNightZ3", 800),
        ),
        True,
        0,
        0,
    ),
)


def enum_name(prefix: str, value: str) -> str:
    return f"{prefix}{value}"


def write_header(bitmaps: dict[str, Canvas]) -> None:
    out = ROOT / "include/assets/face_bitmaps.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    ids = ",\n  ".join(enum_name("FaceBitmapId::", key) for key in bitmaps)
    # Strip the scoped prefix for enum declaration.
    ids = ids.replace("FaceBitmapId::", "")
    out.write_text(
        f"""#pragma once

#include <Arduino.h>

namespace yappl {{

struct FaceBitmap {{
  const uint8_t *data;
  uint8_t width;
  uint8_t height;
}};

enum class FaceBitmapId : uint8_t {{
  {ids},
}};

const FaceBitmap &faceBitmap(FaceBitmapId id);

}}  // namespace yappl
""",
        encoding="utf-8",
    )


def write_cpp(bitmaps: dict[str, Canvas]) -> None:
    out = ROOT / "src/assets/face_bitmaps.cpp"
    out.parent.mkdir(parents=True, exist_ok=True)
    parts: list[str] = [
        '#include "assets/face_bitmaps.h"\n\n',
        "namespace yappl {\nnamespace {\n\n",
    ]
    for key, canvas in bitmaps.items():
        data = canvas.to_xbm_bytes()
        parts.append(f"const uint8_t k{key}[] PROGMEM = {{\n")
        for i in range(0, len(data), 12):
            parts.append("    " + ", ".join(f"0x{b:02x}" for b in data[i : i + 12]) + ",\n")
        parts.append("};\n\n")
    parts.append("const FaceBitmap kFaceBitmaps[] = {\n")
    for key in bitmaps:
        parts.append(f"    {{k{key}, {WIDTH}, {HEIGHT}}},\n")
    parts.append("};\n\n}  // namespace\n\n")
    parts.append(
        """const FaceBitmap &faceBitmap(FaceBitmapId id) {
  const uint8_t index = static_cast<uint8_t>(id);
  return kFaceBitmaps[index];
}

}  // namespace yappl
"""
    )
    out.write_text("".join(parts), encoding="utf-8")


def write_animation_header() -> None:
    out = ROOT / "include/assets/face_animations.h"
    act_ids = ",\n  ".join(act.act_id for act in ACTS)
    out.write_text(
        f"""#pragma once

#include <Arduino.h>

#include "assets/face_bitmaps.h"

namespace yappl {{

struct FaceFrame {{
  FaceBitmapId bitmapId;
  uint16_t durationMs;
  int8_t xOffset;
  int8_t yOffset;
  bool invert;
}};

struct FaceAct {{
  const char *name;
  const FaceFrame *frames;
  uint8_t frameCount;
  bool loop;
  uint32_t minDowntimeMs;
  uint8_t chancePercent;
}};

enum class FaceActId : uint8_t {{
  {act_ids},
}};

const FaceAct &faceAct(FaceActId id);

}}  // namespace yappl
""",
        encoding="utf-8",
    )


def frame_literal(frame: FrameDef) -> str:
    invert = "true" if frame.invert else "false"
    return (
        f"{{FaceBitmapId::{frame.bitmap_id}, {frame.duration_ms}, "
        f"{frame.x_offset}, {frame.y_offset}, {invert}}}"
    )


def write_animation_cpp() -> None:
    out = ROOT / "src/assets/face_animations.cpp"
    parts = ['#include "assets/face_animations.h"\n\n', "namespace yappl {\nnamespace {\n\n"]
    for act in ACTS:
        parts.append(f"const FaceFrame k{act.act_id}Frames[] = {{\n")
        for frame in act.frames:
            parts.append(f"    {frame_literal(frame)},\n")
        parts.append("};\n\n")
    parts.append("const FaceAct kFaceActs[] = {\n")
    for act in ACTS:
        loop = "true" if act.loop else "false"
        parts.append(
            f'    {{"{act.name}", k{act.act_id}Frames, '
            f"{len(act.frames)}, {loop}, {act.min_downtime_ms}, {act.chance_percent}}},\n"
        )
    parts.append("};\n\n}  // namespace\n\n")
    parts.append(
        """const FaceAct &faceAct(FaceActId id) {
  const uint8_t index = static_cast<uint8_t>(id);
  return kFaceActs[index];
}

}  // namespace yappl
"""
    )
    out.write_text("".join(parts), encoding="utf-8")


def write_readme() -> None:
    out = ROOT / "assets/bitmaps/README.md"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        """# Yappl Face Bitmap Sources

Put hand-drawn pixel art source files here later.

Recommended source format:

- 1-bit black/white PNG
- 96x64 px for the face area
- transparent or black background
- white pixels for lit OLED pixels

Current firmware placeholder assets are generated by:

```bash
python3 scripts/generate_face_assets.py
```

Generated firmware files:

- `include/assets/face_bitmaps.h`
- `src/assets/face_bitmaps.cpp`
- `include/assets/face_animations.h`
- `src/assets/face_animations.cpp`

Long term, this folder can hold PNG frames and the generator can be updated to
convert those PNGs instead of procedurally drawing placeholder eyes.
""",
        encoding="utf-8",
    )


def main() -> None:
    bitmaps = make_bitmaps()
    write_header(bitmaps)
    write_cpp(bitmaps)
    write_animation_header()
    write_animation_cpp()
    write_readme()


if __name__ == "__main__":
    main()
