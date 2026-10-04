"""
draws halfcraft.ico: a minecraft grass block face with half-life's lambda on it, 16x16 pixel art
scaled up without smoothing. stdlib only.

    python source/launcher/make_icon.py [preview.png]
"""

import random
import struct
import sys
import zlib
from pathlib import Path

SIZE = 16
SCALES = (1, 2, 3, 4, 8, 16)  # 16, 32, 48, 64, 128 and 256 px

GRASS = [(95, 159, 53), (110, 177, 64), (84, 140, 46), (121, 192, 72)]
DIRT = [(134, 96, 67), (121, 85, 58), (150, 108, 79), (108, 76, 52), (89, 61, 41)]
LAMBDA = (255, 145, 20)
LAMBDA_LIGHT = (255, 190, 80)
OUTLINE = (40, 22, 10)

# how far the grass reaches down each column (the rest of the face is dirt)
GRASS_DEPTH = [4, 3, 4, 5, 3, 4, 4, 3, 5, 4, 3, 4, 6, 4, 3, 4]

GLYPH = [
    "XX.......",
    ".XX......",
    "..XX.....",
    "..XX.....",
    "...XX....",
    "..XXXX...",
    "..XX.XX..",
    ".XX..XX..",
    ".XX...XX.",
    "XX....XXX",
]
GLYPH_LEFT, GLYPH_TOP = 4, 5


def draw() -> list[list[tuple[int, int, int, int]]]:
    rng = random.Random(1998)  # half-life's year: the same icon every build
    pixels = [[(0, 0, 0, 255)] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            palette = GRASS if y < GRASS_DEPTH[x] else DIRT
            pixels[y][x] = (*rng.choice(palette), 255)

    glyph = {(GLYPH_LEFT + gx, GLYPH_TOP + gy) for gy, row in enumerate(GLYPH) for gx, c in enumerate(row) if c == "X"}
    for x, y in glyph:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < SIZE and 0 <= ny < SIZE and (nx, ny) not in glyph:
                    pixels[ny][nx] = (*OUTLINE, 255)
    for x, y in glyph:
        lit = (x - 1, y) not in glyph and (x, y - 1) not in glyph
        pixels[y][x] = (*(LAMBDA_LIGHT if lit else LAMBDA), 255)
    return pixels


def png(pixels: list[list[tuple[int, int, int, int]]], scale: int) -> bytes:
    side = SIZE * scale
    raw = bytearray()
    for y in range(side):
        raw.append(0)
        for x in range(side):
            raw.extend(pixels[y // scale][x // scale])

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", side, side, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def ico(images: list[tuple[int, bytes]]) -> bytes:
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for side, data in images:
        dim = 0 if side >= 256 else side
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in images)


def main() -> None:
    pixels = draw()
    images = [(SIZE * scale, png(pixels, scale)) for scale in SCALES]
    target = Path(__file__).with_name("halfcraft.ico")
    target.write_bytes(ico(images))
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_bytes(images[-1][1])


if __name__ == "__main__":
    main()
