"""decodes the collision ring in the live link's shared memory (read-only) and sums it up: its clears, and the
regions it still holds (their 8-block cell, epoch, blocks and filled sub-voxels).

python tools/debug/dump_collision.py [player x y z]   with a position, also the region it's in
"""

import mmap
import struct
import sys

NAME = "Local\\HalfCraft_v1"
OFF_COL = 0x40000
COL_BYTES = 32 << 20
DATA = COL_BYTES - 0x80
COL_PAD, COL_CLEAR, COL_REGION = 0, 1, 2


def main() -> None:
    link = mmap.mmap(-1, OFF_COL + COL_BYTES, tagname=NAME, access=mmap.ACCESS_READ)
    (head,) = struct.unpack_from("<Q", link, OFF_COL)
    (tail,) = struct.unpack_from("<Q", link, OFF_COL + 0x40)
    print(f"head={head} tail={tail}")
    pos = 0 if head <= DATA else head - DATA
    regions = []
    while pos < head:
        offset = pos % DATA
        kind, size = struct.unpack_from("<II", link, OFF_COL + 0x80 + offset)
        if kind == COL_PAD:
            pos += DATA - offset
            continue
        body = OFF_COL + 0x80 + offset + 8
        if kind == COL_CLEAR:
            print("CLEAR epoch", struct.unpack_from("<I", link, body)[0])
        elif kind == COL_REGION:
            min_x, min_y, min_z, _max_x, _max_y, _max_z, epoch, count = struct.unpack_from("<iiiiiiII", link, body)
            bits = sum(mask.bit_count() for i in range(count) for mask in struct.unpack_from("<8Q", link, body + 32 + i * 80 + 16))
            regions.append(((min_x // 8, min_y // 8, min_z // 8), epoch, count, bits))
        pos += (8 + size + 7) & ~7
    print(f"{len(regions)} regions; non-empty: {sum(1 for region in regions if region[2])}")
    if len(sys.argv) > 1:
        x, y, z = map(float, sys.argv[1:4])
        print("player region", (int(x // 8), int(y // 8), int(z // 8)))
    for region in regions[-40:]:
        print(region)


if __name__ == "__main__":
    main()
