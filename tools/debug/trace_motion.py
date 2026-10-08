"""records how the player's motion reaches the host game, to diagnose camera and body jitter.

read-only: it samples the link's McState (written by minecraft every frame and every physics tick) as fast as
it can and writes two csv files:
  ticks.csv   one row per minecraft physics tick: when it was stamped, when it became visible, previous and
              latest feet, eye height
  frames.csv  one row per minecraft frame: camera mode and zoom distance, feet, eye, look, bob

python tools/debug/trace_motion.py [seconds] [out_dir]

then walk (uphill, in third person, ...) while it runs.
"""

import csv
import ctypes
import mmap
import os
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

OFF_MC = 0x200
MC_BYTES = 0xC8

_kernel32 = ctypes.windll.kernel32


@dataclass(frozen=True)
class McSample:
    seq: int
    flags: int
    x: float
    y: float
    z: float
    yaw: float
    pitch: float
    frame: int
    bob_phase: float
    bob_amount: float
    eye: tuple[float, float, float]
    tick_qpc: int
    prev: tuple[float, float, float]
    cur: tuple[float, float, float]
    eye_o: float
    eye_t: float
    tick_ms: float
    camera_mode: int
    camera_distance: float


def qpc() -> int:
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


def qpc_frequency() -> int:
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceFrequency(ctypes.byref(value))
    return value.value


def read_mc(link: mmap.mmap) -> McSample:
    state = link[OFF_MC : OFF_MC + MC_BYTES]
    seq, flags, x, y, z, yaw, pitch, _eye_height, _sensitivity, _ack, _gui, frame, _fov, bob_phase, bob_amount = struct.unpack_from(
        "<IIdddffffIIQfff", state, 0
    )
    eye_o, eye_t, _walk_o, _walk, _bob_o, _bob, tick_ms = struct.unpack_from("<fffffff", state, 0xA0)
    camera_mode, camera_distance = struct.unpack_from("<If", state, 0xC0)
    return McSample(
        seq,
        flags,
        x,
        y,
        z,
        yaw,
        pitch,
        frame,
        bob_phase,
        bob_amount,
        eye=struct.unpack_from("<ddd", state, 0x50),
        tick_qpc=struct.unpack_from("<q", state, 0x68)[0],
        prev=struct.unpack_from("<ddd", state, 0x70),
        cur=struct.unpack_from("<ddd", state, 0x88),
        eye_o=eye_o,
        eye_t=eye_t,
        tick_ms=tick_ms,
        camera_mode=camera_mode,
        camera_distance=camera_distance,
    )


def write_csv(path: Path, header: list[str], rows: list[list[float]]) -> None:
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(header)
        writer.writerows(rows)


def main() -> None:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    out = Path(sys.argv[2] if len(sys.argv) > 2 else ".")
    out.mkdir(parents=True, exist_ok=True)
    link = mmap.mmap(-1, 0x1000, tagname=os.environ.get("HALFCRAFT_LINK", "Local\\HalfCraft_v1"), access=mmap.ACCESS_READ)
    frequency = qpc_frequency()
    start = qpc()

    def ms(when: int) -> float:
        return (when - start) * 1000.0 / frequency

    ticks: list[list[float]] = []
    frames: list[list[float]] = []
    last_tick = last_frame = None
    end = time.perf_counter() + seconds
    while time.perf_counter() < end:
        sample = read_mc(link)
        now = qpc()
        if sample.tick_qpc != last_tick:
            last_tick = sample.tick_qpc
            ticks.append([ms(sample.tick_qpc), ms(now), *sample.prev, *sample.cur, sample.eye_o, sample.eye_t, sample.tick_ms, sample.flags])
        if sample.frame != last_frame:
            last_frame = sample.frame
            look = [sample.yaw, sample.pitch, sample.bob_phase, sample.bob_amount, sample.flags]
            frames.append([ms(now), sample.frame, sample.camera_mode, sample.camera_distance, sample.x, sample.y, sample.z, *sample.eye, *look])
    write_csv(out / "ticks.csv", ["tick_ms", "seen_ms", "px", "py", "pz", "cx", "cy", "cz", "eye_o", "eye_t", "ms_per_tick", "flags"], ticks)
    header = ["seen_ms", "frame", "cam_mode", "cam_dist", "x", "y", "z", "ex", "ey", "ez", "yaw", "pitch", "bob_phase", "bob_amt", "flags"]
    write_csv(out / "frames.csv", header, frames)
    print(f"{len(ticks)} ticks, {len(frames)} Minecraft frames in {seconds:.0f} s -> {out}")


if __name__ == "__main__":
    main()
