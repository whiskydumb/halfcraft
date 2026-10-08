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
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import protocol
from halfcraft.link import open_link

_kernel32 = ctypes.windll.kernel32


def qpc() -> int:
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


def qpc_frequency() -> int:
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceFrequency(ctypes.byref(value))
    return value.value


def write_csv(path: Path, header: list[str], rows: list[list[float]]) -> None:
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(header)
        writer.writerows(rows)


def main() -> None:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    out = Path(sys.argv[2] if len(sys.argv) > 2 else ".")
    out.mkdir(parents=True, exist_ok=True)
    schema = protocol.load()
    link = open_link(schema, os.environ.get("HALFCRAFT_LINK"))
    state, at = schema.structs["McState"], schema.regions["McState"].at
    frequency = qpc_frequency()
    start = qpc()

    def ms(when: int) -> float:
        return (when - start) * 1000.0 / frequency

    ticks: list[list[float]] = []
    frames: list[list[float]] = []
    last_tick = last_frame = None
    end = time.perf_counter() + seconds
    while time.perf_counter() < end:
        # a copy first: minecraft may write while it's read, and a copy takes the least time
        sample = state.read(bytes(link[at : at + state.size]))
        now = qpc()
        if sample["tickQpc"] != last_tick:
            last_tick = sample["tickQpc"]
            feet = [sample[name] for name in ("prevX", "prevY", "prevZ", "curX", "curY", "curZ")]
            ticks.append([ms(last_tick), ms(now), *feet, sample["tickEyeO"], sample["tickEye"], sample["tickMs"], sample["flags"]])
        if sample["frameCounter"] != last_frame:
            last_frame = sample["frameCounter"]
            where = [sample[name] for name in ("x", "y", "z", "eyeX", "eyeY", "eyeZ")]
            look = [sample[name] for name in ("yaw", "pitch", "bobPhase", "bobAmount", "flags")]
            frames.append([ms(now), last_frame, sample["cameraMode"], sample["cameraDistance"], *where, *look])
    write_csv(out / "ticks.csv", ["tick_ms", "seen_ms", "px", "py", "pz", "cx", "cy", "cz", "eye_o", "eye_t", "ms_per_tick", "flags"], ticks)
    header = ["seen_ms", "frame", "cam_mode", "cam_dist", "x", "y", "z", "ex", "ey", "ez", "yaw", "pitch", "bob_phase", "bob_amt", "flags"]
    write_csv(out / "frames.csv", header, frames)
    print(f"{len(ticks)} ticks, {len(frames)} Minecraft frames in {seconds:.0f} s -> {out}")


if __name__ == "__main__":
    main()
