"""decodes the collision ring in the live link's shared memory (read-only) and sums it up: its clears, and the
regions it still holds (their 8-block cell, epoch, blocks and filled sub-voxels). once the ring has wrapped it
holds the latest of what the host sent (halfcraft.link.CollisionRing says what it keeps).

python tools/debug/dump_collision.py [player x y z]   with a position, also the region it's in
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import protocol
from halfcraft.link import CollisionRing, open_link


def main() -> None:
    schema = protocol.load()
    link = open_link(schema)
    ring = CollisionRing(link, schema)
    region, block = schema.structs["ColRegion"], schema.structs["ColBlock"]
    bits = block.field("bits")
    messages = ring.messages()
    print(f"head={ring.head} tail={ring.tail}; it holds the last {ring.head - ring.kept_from} bytes the host wrote")
    regions = []
    for message in messages:
        if message.type == ring.clear:
            print("CLEAR epoch", ring.epoch(message))
        elif message.type == ring.region:
            box = region.read(link, message.at)
            first = message.at + region.size + bits.offset
            filled = sum(mask.bit_count() for i in range(box["count"]) for mask in bits.read(link, first + i * block.size))
            regions.append(((box["minX"] // 8, box["minY"] // 8, box["minZ"] // 8), box["epoch"], box["count"], filled))
    print(f"{len(regions)} regions; non-empty: {sum(1 for each in regions if each[2])}")
    if len(sys.argv) > 1:
        x, y, z = map(float, sys.argv[1:4])
        print("player region", (int(x // 8), int(y // 8), int(z // 8)))
    for each in regions[-40:]:
        print(each)


if __name__ == "__main__":
    main()
