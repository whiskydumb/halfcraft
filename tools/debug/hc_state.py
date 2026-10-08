"""prints what the game and minecraft are telling each other right now (shared memory, read-only).

python tools/debug/hc_state.py [seconds]
"""

import mmap
import struct
import sys
import time

NAME = "Local\\HalfCraft_v1"
SIZE = 0x200 + 0xD8
MAGIC = 0x464C4148  # "HALF"


def main() -> None:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 0.0
    # a name nobody holds gets a new, empty mapping: the magic tells
    link = mmap.mmap(-1, SIZE, tagname=NAME, access=mmap.ACCESS_READ)
    if struct.unpack_from("<I", link, 0)[0] != MAGIC:
        raise SystemExit("no link in shared memory: is the game running?")
    end = time.time() + seconds
    while True:
        _magic, _version, host_pid, mc_pid = struct.unpack_from("<IIII", link, 0)
        _seq, flags, world, epoch, x, y, z, yaw, pitch, teleport, width, height, _hour = struct.unpack_from("<IIIIdddffIIIf", link, 0x100)
        _mc_seq, mc_flags, mc_x, mc_y, mc_z, mc_yaw, mc_pitch, eye, _sensitivity, ack = struct.unpack_from("<IIdddffffI", link, 0x200)
        print(
            f"host pid {host_pid} flags {flags:#x} world {world:08x} epoch {epoch} pos ({x:.2f} {y:.2f} {z:.2f}) "
            f"look ({yaw:.1f} {pitch:.1f}) tp {teleport} view {width}x{height}"
        )
        print(f"mc   pid {mc_pid} flags {mc_flags:#x} pos ({mc_x:.2f} {mc_y:.2f} {mc_z:.2f}) look ({mc_yaw:.1f} {mc_pitch:.1f}) eye {eye:.2f} ack {ack}")
        if time.time() >= end:
            break
        time.sleep(0.5)


if __name__ == "__main__":
    main()
