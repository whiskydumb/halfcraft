"""Prints what the game and Minecraft are telling each other right now (shared memory, read-only).

    python tools/hc_state.py [seconds]
"""

import mmap
import struct
import sys
import time

NAME = "Local\\HalfCraft_v1"
SIZE = 0x200 + 0xD8


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 0.0
    try:
        m = mmap.mmap(-1, SIZE, tagname=NAME, access=mmap.ACCESS_READ)
    except OSError as error:
        raise SystemExit(f"no shared memory ({error}): is the game running?")
    end = time.time() + seconds
    while True:
        magic, version, host_pid, mc_pid = struct.unpack_from("<IIII", m, 0)
        (seq, flags, world, epoch, x, y, z, yaw, pitch, tp, vw, vh, hour) = struct.unpack_from("<IIIIdddffIIIf", m, 0x100)
        (mseq, mflags, mx, my, mz, myaw, mpitch, eye, sens, ack) = struct.unpack_from("<IIdddffffI", m, 0x200)
        print(f"host pid {host_pid} flags {flags:#x} world {world:08x} epoch {epoch} pos ({x:.2f} {y:.2f} {z:.2f}) look ({yaw:.1f} {pitch:.1f}) tp {tp} view {vw}x{vh}")
        print(f"mc   pid {mc_pid} flags {mflags:#x} pos ({mx:.2f} {my:.2f} {mz:.2f}) look ({myaw:.1f} {mpitch:.1f}) eye {eye:.2f} ack {ack}")
        if time.time() >= end:
            break
        time.sleep(0.5)


if __name__ == "__main__":
    main()
