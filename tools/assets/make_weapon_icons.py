"""draws the 16x16 item icons minecraft shows for half-life's weapons (#12), and writes each item's
model files next to them. our own pixel art, no valve art. stdlib only.

    python tools/assets/make_weapon_icons.py [preview.png]

writes into minecraft/src/main/resources/assets/halfcraft/: textures/item/<name>.png,
models/item/<name>.json and items/<name>.json. the optional preview is every icon side by side, 8x.
"""

import json
import struct
import sys
import zlib
from pathlib import Path

SIZE = 16
PREVIEW_SCALE = 8

PALETTE = {
    ".": None,
    "k": (28, 28, 32),  # outline
    "d": (58, 60, 66),  # dark metal
    "g": (104, 107, 116),  # metal
    "l": (164, 167, 176),  # light metal
    "w": (222, 224, 230),  # shine
    "r": (150, 24, 24),  # crowbar red, dark
    "R": (205, 52, 44),  # crowbar red
    "o": (226, 120, 18),  # gravity gun orange
    "O": (255, 178, 64),  # orange, lit
    "y": (246, 214, 92),  # glow, bugbait
    "b": (98, 62, 36),  # wood, dark
    "B": (142, 94, 56),  # wood
    "n": (44, 70, 40),  # olive, dark
    "N": (74, 108, 62),  # olive
    "G": (118, 150, 92),  # olive, lit
    "u": (66, 84, 104),  # combine blue-grey
    "c": (120, 214, 255),  # combine glow
}

# name: rows (16 x 16); the model's parent ("handheld" points along the diagonal like a sword)
ICONS = {
    "crowbar": (
        "handheld",
        [
            "................",
            "...........kkk..",
            "..........kRRrk.",
            ".........kRrkkrk",
            "........kRrk..kk",
            ".......kRrk.....",
            "......kRrk......",
            ".....kRrk.......",
            "....kRrk........",
            "...kRrk.........",
            "..kRrk..........",
            ".kRrk...........",
            "kRrk............",
            "krrk............",
            ".kk.............",
            "................",
        ],
    ),
    "gravity_gun": (
        "generated",
        [
            "................",
            "................",
            "............kk..",
            "...........klgk.",
            "..kkkkkkkkkkgkk.",
            ".kOOOOOOooklkk..",
            ".kOoooooookdkyk.",
            ".kooooooookdyyy.",
            ".kdddkddddkdkyk.",
            ".kgdk.kddkklkk..",
            ".kgdk..kkkkgkk..",
            ".kgdk.....klgk..",
            ".kkkk......kk...",
            "................",
            "................",
            "................",
        ],
    ),
    "pistol": (
        "generated",
        [
            "................",
            "................",
            "................",
            "................",
            "..kkkkkkkkkkkk..",
            "..kwllllllllgk..",
            "..kgggggggggdk..",
            "..kkkkdddkkkkk..",
            "....kddkdk......",
            "...kddkkk.......",
            "...kddk.........",
            "..kddk..........",
            "..kddk..........",
            "..kkkk..........",
            "................",
            "................",
        ],
    ),
    "revolver": (
        "generated",
        [
            "................",
            "................",
            "................",
            "................",
            ".kkkkkkkkkkkkkk.",
            ".kwlllllllllllk.",
            ".kkkkllglkkkkkk.",
            "....kllggk......",
            "....kglggk......",
            "...kbkkkkk......",
            "..kBbkdk........",
            "..kBbkk.........",
            ".kBbk...........",
            ".kbbk...........",
            ".kkkk...........",
            "................",
        ],
    ),
    "smg": (
        "generated",
        [
            "................",
            "................",
            "................",
            ".........kk.....",
            ".kkk.kkkkddkkk..",
            ".kgk.kgggggggkkk",
            ".kgkkkddddddddgk",
            ".kgggkdkkkdkkkk.",
            ".kkkkkdk.kdk....",
            "......kk.kdk....",
            ".........kdk....",
            ".........kdk....",
            ".........kkk....",
            "................",
            "................",
            "................",
        ],
    ),
    "pulse_rifle": (
        "generated",
        [
            "................",
            "................",
            "................",
            "................",
            "...kkkkkkkkkk...",
            "..kuuuuuuuuuukkk",
            ".kuuucccuuuuuuuk",
            "kuuuuuuuuuukkkk.",
            "kuukkkkduuk.....",
            "kkk....kduk.....",
            ".......kduk.....",
            ".......kkkk.....",
            "................",
            "................",
            "................",
            "................",
        ],
    ),
    "shotgun": (
        "generated",
        [
            "................",
            "................",
            "................",
            "................",
            "................",
            "kkkkkkkkkkkkkkk.",
            "kddddddddddddddk",
            "kgggkkgggggggggk",
            "kkkkkkbBBBbkkkk.",
            "..kdk.kkkkk.....",
            ".kddk...........",
            ".kdk............",
            ".kkk............",
            "................",
            "................",
            "................",
        ],
    ),
    "crossbow": (
        "generated",
        [
            "................",
            "......kk........",
            "......kgk.......",
            ".......kgk......",
            "........kgk.....",
            "kkkkkkkkkkkkkkk.",
            "kBBBBBBBkOOOOyyk",
            "kbbbbbbbkkkkkkk.",
            "kkkkkdk.kgk.....",
            "....kdk.kgk.....",
            "....kkkkgk......",
            "......kgk.......",
            "......kk........",
            "................",
            "................",
            "................",
        ],
    ),
    "grenade": (
        "generated",
        [
            "................",
            ".......kk.......",
            "......kRrk......",
            "......kggk......",
            ".....kkllkkkk...",
            "....kNNNNkkgk...",
            "...kNGGNNNkkk...",
            "...kGNNNNNNk....",
            "...kGNNNNNNk....",
            "...kNNNNNNnk....",
            "...kNNNNNnnk....",
            "....kNNnnnk.....",
            ".....kkkkk......",
            "................",
            "................",
            "................",
        ],
    ),
    "rpg": (
        "generated",
        [
            "................",
            "................",
            "................",
            "..........kkk...",
            "..........kgk...",
            "kkkkkkkkkkkgkkkk",
            "kNNNNNNNNNNNNNNk",
            "kGGGGGGGGGGGGGGk",
            "knnnnnnnnnnnnnnk",
            "kkkkkkdkkkdkkkk.",
            "......dk..dk....",
            "......kk..kk....",
            "................",
            "................",
            "................",
            "................",
        ],
    ),
    "bugbait": (
        "generated",
        [
            "................",
            "................",
            "................",
            "......kkkk......",
            "....kkyyyykk....",
            "...kyyOyyyyyk...",
            "..kyOOyyyoyyyk..",
            "..kyyyyyoooyyk..",
            "..kyyoyyyyyyok..",
            "..kyooyyyoyyyk..",
            "...kyyyyoooyk...",
            "....kkyyyykk....",
            "......kkkk......",
            "................",
            "................",
            "................",
        ],
    ),
}


def pixels(name: str, rows: list[str]) -> list[list[tuple[int, int, int, int]]]:
    if len(rows) != SIZE or any(len(row) != SIZE for row in rows):
        raise SystemExit(f"{name} isn't {SIZE}x{SIZE}")
    return [[(0, 0, 0, 0) if PALETTE[c] is None else (*PALETTE[c], 255) for c in row] for row in rows]


def png(image: list[list[tuple[int, int, int, int]]], scale: int = 1) -> bytes:
    height = len(image) * scale
    width = len(image[0]) * scale
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(image[y // scale][x // scale])

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", newline="\n")


def main() -> None:
    assets = Path(__file__).resolve().parents[2] / "minecraft/src/main/resources/assets/halfcraft"
    sheet: list[list[tuple[int, int, int, int]]] = [[] for _ in range(SIZE)]
    for name, (parent, rows) in ICONS.items():
        image = pixels(name, rows)
        texture = assets / "textures/item" / f"{name}.png"
        texture.parent.mkdir(parents=True, exist_ok=True)
        texture.write_bytes(png(image))
        write_json(assets / "models/item" / f"{name}.json", {"parent": f"minecraft:item/{parent}", "textures": {"layer0": f"halfcraft:item/{name}"}})
        write_json(assets / "items" / f"{name}.json", {"model": {"type": "minecraft:model", "model": f"halfcraft:item/{name}"}})
        for y in range(SIZE):
            sheet[y].extend(image[y] + [(255, 255, 255, 255)])
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_bytes(png(sheet, PREVIEW_SCALE))


if __name__ == "__main__":
    main()
