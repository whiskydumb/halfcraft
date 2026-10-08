"""reads the live link (read-only) and shows the collision voxels under the player: for the block column the
player stands in and the eight around it, how many of each block's 512 sub-voxels are filled.

python tools/debug/probe_feet.py
"""

import mmap
import struct

NAME = "Local\\HalfCraft_v1"
OFF_COL = 0x40000
COL_BYTES = 32 << 20
DATA = COL_BYTES - 0x80
COL_PAD, COL_REGION = 0, 2

Block = tuple[int, int, int]


def blocks_of(link: mmap.mmap, epoch: int) -> dict[Block, tuple[int, ...]]:
    """every block's sub-voxel masks the ring still holds for this epoch: a region replaces its whole box."""
    (head,) = struct.unpack_from("<Q", link, OFF_COL)
    blocks: dict[Block, tuple[int, ...]] = {}
    pos = 0 if head <= DATA else head - DATA
    while pos < head:
        offset = pos % DATA
        kind, size = struct.unpack_from("<II", link, OFF_COL + 0x80 + offset)
        if kind == COL_PAD:
            pos += DATA - offset
            continue
        body = OFF_COL + 0x80 + offset + 8
        if kind == COL_REGION:
            min_x, min_y, min_z, max_x, max_y, max_z, region_epoch, count = struct.unpack_from("<iiiiiiII", link, body)
            if region_epoch == epoch:
                for x in range(min_x, max_x + 1):
                    for y in range(min_y, max_y + 1):
                        for z in range(min_z, max_z + 1):
                            blocks.pop((x, y, z), None)
                for i in range(count):
                    record = body + 32 + i * 80
                    x, y, z, _flags = struct.unpack_from("<iiiI", link, record)
                    blocks[x, y, z] = struct.unpack_from("<8Q", link, record + 16)
        pos += (8 + size + 7) & ~7
    return blocks


def main() -> None:
    link = mmap.mmap(-1, OFF_COL + COL_BYTES, tagname=NAME, access=mmap.ACCESS_READ)
    host = struct.unpack_from("<IIIIdddffIIIf", link, 0x100)
    mc = struct.unpack_from("<IIdddffffIIQfffIddd", link, 0x200)
    print(
        f"host flags={host[1]:#x} world={host[2]:#x} epoch={host[3]} pos=({host[4]:.2f} {host[5]:.2f} {host[6]:.2f}) "
        f"yaw={host[7]:.1f} pitch={host[8]:.1f} tpseq={host[9]}"
    )
    print(f"mc  flags={mc[1]:#x} pos=({mc[2]:.2f} {mc[3]:.2f} {mc[4]:.2f}) yaw={mc[5]:.1f} ack={mc[9]}")
    blocks = blocks_of(link, host[3])
    feet_x, feet_y, feet_z = host[4], host[5], host[6]
    column_x, column_z = int(feet_x // 1), int(feet_z // 1)
    print("column under player (block y: filled sub-voxels /512):")
    for y in range(int(feet_y) + 3, int(feet_y) - 6, -1):
        row = []
        for dz in (-1, 0, 1):
            for dx in (-1, 0, 1):
                masks = blocks.get((column_x + dx, y, column_z + dz))
                row.append(f"{sum(mask.bit_count() for mask in masks) if masks else 0:3d}")
        print(f"  y={y:4d}  {' '.join(row)}")


if __name__ == "__main__":
    main()
