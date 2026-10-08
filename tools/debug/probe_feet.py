"""reads the live link (read-only) and shows the collision voxels under the player: for the block column the
player stands in and the eight around it, how many of each block's 512 sub-voxels are filled. once the collision
ring has wrapped, only what it still holds (halfcraft.link.CollisionRing).

python tools/debug/probe_feet.py
"""

import mmap
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import protocol
from halfcraft.link import CollisionRing, open_link
from halfcraft.protocol import Schema

Block = tuple[int, int, int]


def blocks_of(link: mmap.mmap, schema: Schema, epoch: int) -> dict[Block, list[int]]:
    """every block's sub-voxel masks the ring still holds for this epoch: a region replaces its whole box."""
    ring = CollisionRing(link, schema)
    region, block = schema.structs["ColRegion"], schema.structs["ColBlock"]
    blocks: dict[Block, list[int]] = {}
    for message in ring.messages():
        if message.type != ring.region:
            continue
        box = region.read(link, message.at)
        if box["epoch"] != epoch:
            continue
        for x in range(box["minX"], box["maxX"] + 1):
            for y in range(box["minY"], box["maxY"] + 1):
                for z in range(box["minZ"], box["maxZ"] + 1):
                    blocks.pop((x, y, z), None)
        for i in range(box["count"]):
            record = block.read(link, message.at + region.size + i * block.size)
            blocks[record["x"], record["y"], record["z"]] = record["bits"]
    if ring.kept_from:
        print(f"the collision ring wrapped: it holds the last {ring.head - ring.kept_from} bytes the host wrote")
    return blocks


def main() -> None:
    schema = protocol.load()
    link = open_link(schema)
    host = schema.structs["HostState"].read(link, schema.regions["HostState"].at)
    mc = schema.structs["McState"].read(link, schema.regions["McState"].at)
    print(
        f"host flags={host['flags']:#x} world={host['worldId']:#x} epoch={host['collisionEpoch']} "
        f"pos=({host['posX']:.2f} {host['posY']:.2f} {host['posZ']:.2f}) yaw={host['yaw']:.1f} pitch={host['pitch']:.1f} tpseq={host['teleportSeq']}"
    )
    print(f"mc  flags={mc['flags']:#x} pos=({mc['x']:.2f} {mc['y']:.2f} {mc['z']:.2f}) yaw={mc['yaw']:.1f} ack={mc['teleportAck']}")
    blocks = blocks_of(link, schema, host["collisionEpoch"])
    feet_x, feet_y, feet_z = host["posX"], host["posY"], host["posZ"]
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
