#!/usr/bin/env python3
"""Writes artwork/BeTunes.hvif, BeTunes' vector icon.

HVIF is Haiku's compact vector icon format (what Icon-O-Matic exports). The
encoding follows src/libs/icon/flat_icon/FlatIconImporter.cpp in the Haiku
source tree: a magic number, then styles, paths and shapes. Coordinates are
in a 64 x 64 space.

The icon, in the spirit of the mid 2000s iTunes icon: a silver CD with a
rainbow sheen and a big blue double note in front of it.
"""

import math
import os
import struct
import sys

STYLE_SOLID_COLOR = 1
STYLE_GRADIENT = 2
STYLE_SOLID_COLOR_NO_ALPHA = 3
SHAPE_PATH_SOURCE = 10

GRADIENT_LINEAR = 0
GRADIENT_CIRCULAR = 1
GRADIENT_FLAG_TRANSFORM = 1 << 1
GRADIENT_FLAG_NO_ALPHA = 1 << 2

PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_NO_CURVES = 1 << 3


def coord(value):
    """One byte for small integers, otherwise 15 bit fixed point."""
    if float(value).is_integer() and -32 <= value <= 95:
        return bytes([int(value) + 32])
    raw = int(round((value + 128.0) * 102.0)) | 0x8000
    return bytes([raw >> 8, raw & 0xFF])


def float24(value):
    """1 sign bit, 6 bit exponent, 17 bit mantissa."""
    if value == 0.0:
        return b"\0\0\0"
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xFF) - 127
    mantissa = bits & 0x7FFFFF
    if exponent >= 32 or exponent < -32:
        return b"\0\0\0"
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    return bytes([(short >> 16) & 0xFF, (short >> 8) & 0xFF, short & 0xFF])


def matrix(m):
    return b"".join(float24(v) for v in m)


def rgb(hex_color):
    hex_color = hex_color.lstrip("#")
    return bytes(int(hex_color[i:i + 2], 16) for i in (0, 2, 4))


# --- styles -----------------------------------------------------------------

def solid(hex_color, alpha=None):
    if alpha is None:
        return bytes([STYLE_SOLID_COLOR_NO_ALPHA]) + rgb(hex_color)
    return bytes([STYLE_SOLID_COLOR]) + rgb(hex_color) + bytes([alpha])


def linear_gradient(x0, y0, x1, y1, stops):
    """Gradient along the line (x0, y0) -> (x1, y1).

    stops: (offset 0..255, "#rrggbb", alpha or None). A linear gradient
    runs from -64 to 64 along x in gradient space; the matrix maps that
    onto the line.
    """
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    s = length / 128.0
    c, n = dx / length, dy / length
    m = (c * s, n * s, -n * s, c * s, (x0 + x1) / 2.0, (y0 + y1) / 2.0)
    opaque = all(alpha is None for _, _, alpha in stops)
    flags = GRADIENT_FLAG_TRANSFORM | (GRADIENT_FLAG_NO_ALPHA if opaque else 0)
    data = bytes([STYLE_GRADIENT, GRADIENT_LINEAR, flags, len(stops)])
    data += matrix(m)
    for offset, color, alpha in stops:
        data += bytes([offset]) + rgb(color)
        if not opaque:
            data += bytes([255 if alpha is None else alpha])
    return data


def radial_gradient(cx, cy, radius, stops):
    s = radius / 64.0
    m = (s, 0.0, 0.0, s, cx, cy)
    data = bytes([STYLE_GRADIENT, GRADIENT_CIRCULAR,
                  GRADIENT_FLAG_TRANSFORM | GRADIENT_FLAG_NO_ALPHA,
                  len(stops)])
    data += matrix(m)
    for offset, color in stops:
        data += bytes([offset]) + rgb(color)
    return data


# --- paths ------------------------------------------------------------------

def polygon(points):
    data = bytes([PATH_FLAG_CLOSED | PATH_FLAG_NO_CURVES, len(points)])
    for x, y in points:
        data += coord(x) + coord(y)
    return data


def curves(points):
    """Closed path of (point, in-handle, out-handle) triples."""
    data = bytes([PATH_FLAG_CLOSED, len(points)])
    for point, handle_in, handle_out in points:
        for x, y in (point, handle_in, handle_out):
            data += coord(x) + coord(y)
    return data


def ellipse(cx, cy, rx, ry, degrees=0.0):
    """An ellipse rotated around its center, as four Bezier segments."""
    k = 0.5523
    a = math.radians(degrees)
    ca, sa = math.cos(a), math.sin(a)

    def at(x, y):
        return (cx + x * ca - y * sa, cy + x * sa + y * ca)

    return curves([
        (at(0, -ry), at(-k * rx, -ry), at(k * rx, -ry)),
        (at(rx, 0), at(rx, -k * ry), at(rx, k * ry)),
        (at(0, ry), at(k * rx, ry), at(-k * rx, ry)),
        (at(-rx, 0), at(-rx, k * ry), at(-rx, -k * ry)),
    ])


def circle(cx, cy, r):
    return ellipse(cx, cy, r, r)


def quad(points):
    return polygon(points)


# --- shapes -----------------------------------------------------------------

def shape(style, paths):
    return bytes([SHAPE_PATH_SOURCE, style, len(paths)]) + bytes(paths) \
        + bytes([0])


def note_paths(grow):
    """The double note: two heads, two stems and the beam; "grow" widens
    every part (for the outline drawn behind it)."""
    head1 = (22.0, 51.0)
    head2 = (45.0, 45.0)
    rx, ry = 7.6 + grow, 5.6 + grow
    stem = 2.6 + grow
    # stems on the right side of the heads
    s1 = head1[0] + rx - grow - 1.4
    s2 = head2[0] + rx - grow - 1.4
    top1, top2 = 17.0 - grow, 11.0 - grow
    beam = 7.0 + 2 * grow
    return [
        ellipse(head1[0], head1[1], rx, ry, -22),
        ellipse(head2[0], head2[1], rx, ry, -22),
        quad([(s1 - stem / 2, top1 + 2), (s1 + stem / 2, top1 + 2),
              (s1 + stem / 2, head1[1]), (s1 - stem / 2, head1[1])]),
        quad([(s2 - stem / 2, top2 + 2), (s2 + stem / 2, top2 + 2),
              (s2 + stem / 2, head2[1]), (s2 - stem / 2, head2[1])]),
        quad([(s1 - stem / 2, top1), (s2 + stem / 2, top2),
              (s2 + stem / 2, top2 + beam), (s1 - stem / 2, top1 + beam)]),
    ]


def build():
    styles = [
        solid("#1E252E"),                                         # 0 outline
        radial_gradient(26, 24, 40, [(0, "#FFFFFF"), (150, "#E2E6EB"),
                                     (255, "#9EA7B3")]),           # 1 disc
        linear_gradient(8, 12, 56, 52, [                          # 2 sheen
            (0, "#FF7AA8", 0), (70, "#FFD36E", 110),
            (120, "#7CF0C8", 120), (170, "#7AB8FF", 110),
            (255, "#C48CFF", 0)]),
        solid("#C9CED6"),                                         # 3 ring
        solid("#5B6370"),                                         # 4 hole
        linear_gradient(32, 10, 32, 58, [(0, "#8EC7FF", None),
                                         (255, "#1846B0", None)]), # 5 note
        solid("#FFFFFF", 90),                                     # 6 shine
        solid("#000000", 50),                                     # 7 shadow
    ]

    paths = [
        circle(32, 32, 29.5),    # 0 disc outline
        circle(32, 32, 28),      # 1 disc
        circle(32, 32, 9),       # 2 ring
        circle(32, 32, 4),       # 3 hole
    ]
    first_outline = len(paths)
    paths += note_paths(1.6)     # outline of the note
    first_note = len(paths)
    paths += note_paths(0.0)
    shine = len(paths)
    paths.append(ellipse(19.5, 48.5, 3.6, 1.8, -22))
    paths.append(ellipse(42.5, 42.5, 3.6, 1.8, -22))
    shadow = len(paths)
    paths.append(ellipse(33, 60, 22, 2.5))

    note_outline = list(range(first_outline, first_outline + 5))
    note = list(range(first_note, first_note + 5))
    shapes = [
        shape(7, [shadow]),
        shape(0, [0]),
        shape(1, [1]),
        shape(2, [1]),
        shape(3, [2]),
        shape(4, [3]),
        shape(0, note_outline),
        shape(5, note),
        shape(6, [shine, shine + 1]),
    ]

    data = b"ncif"
    data += bytes([len(styles)]) + b"".join(styles)
    data += bytes([len(paths)]) + b"".join(paths)
    data += bytes([len(shapes)]) + b"".join(shapes)
    return data


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, "artwork", "BeTunes.hvif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
