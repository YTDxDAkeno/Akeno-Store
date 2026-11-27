#!/usr/bin/env python3
# Akeno Gaming Hub - renders sce_sys/icon0.png (512x512) and the shortcut fallback icon.
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproducible app icon: navy-to-violet field, light orbs, a glowing gradient "A"."""

import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "native/assets/fonts/Inter-SemiBold.ttf"
SIZE = 512


def radial(size, center, radius, color):
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pixels = layer.load()
    cx, cy = center
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy) / radius
            if d < 1:
                a = int(color[3] * (1 - d) ** 2)
                pixels[x, y] = (color[0], color[1], color[2], a)
    return layer


def gradient(size, top, bottom):
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    for y in range(size):
        t = y / (size - 1)
        draw.line([(0, y), (size, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    return image


def icon(output: Path):
    base = gradient(SIZE, (10, 14, 40), (34, 16, 74))
    base = Image.alpha_composite(base, radial(SIZE, (400, 90), 360, (139, 92, 246, 190)))
    base = Image.alpha_composite(base, radial(SIZE, (80, 470), 330, (64, 110, 255, 150)))

    font = ImageFont.truetype(str(FONT), 330)
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).text((SIZE / 2, SIZE / 2 + 8), "A", font=font, fill=255, anchor="mm")
    glow = mask.filter(ImageFilter.GaussianBlur(22))
    glow_layer = Image.new("RGBA", (SIZE, SIZE), (170, 120, 255, 0))
    glow_layer.putalpha(glow.point(lambda v: int(v * 0.9)))
    base = Image.alpha_composite(base, glow_layer)

    letter = Image.new("RGBA", (SIZE, SIZE))
    draw = ImageDraw.Draw(letter)
    for y in range(SIZE):
        t = y / (SIZE - 1)
        draw.line([(0, y), (SIZE, y)], fill=(int(250 - 60 * t), int(240 - 120 * t), 255, 255))
    letter.putalpha(mask)
    base = Image.alpha_composite(base, letter)

    small = ImageFont.truetype(str(FONT), 46)
    ImageDraw.Draw(base).text((SIZE / 2, 470), "AKENO", font=small, fill=(235, 232, 255, 235), anchor="mm")
    # A thin inner rim for depth.
    rim = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle([6, 6, SIZE - 7, SIZE - 7], radius=40, outline=(190, 160, 255, 70), width=3)
    base = Image.alpha_composite(base, rim)
    output.parent.mkdir(parents=True, exist_ok=True)
    base.convert("RGB").save(output, optimize=True)


if __name__ == "__main__":
    icon(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "ps5/sce_sys/icon0.png")
