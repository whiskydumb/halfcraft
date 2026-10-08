"""watches minecraft's physics ticks and frames through the live link (read-only) and reports stalls: ticks more
than 70 ms apart or published more than 8 ms after their stamp, frames more than 20 ms apart.

python tools/debug/tick_monitor.py [seconds]
"""

import ctypes
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


def main() -> None:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
    schema = protocol.load()
    link = open_link(schema)
    state, at = schema.structs["McState"], schema.regions["McState"].at
    tick_field, frame_field = state.field("tickQpc"), state.field("frameCounter")
    frequency = qpc_frequency()
    last_tick = last_frame = None
    last_frame_at = qpc()
    ticks: list[tuple[float, float]] = []  # interval, how late it was published
    stalls: list[tuple[str, float, float]] = []
    frame_stalls: list[tuple[str, float]] = []
    end = time.time() + seconds
    while time.time() < end:
        tick = tick_field.read(link, at + tick_field.offset)
        frame = frame_field.read(link, at + frame_field.offset)
        now = qpc()
        if tick != last_tick:
            if last_tick is not None and tick > last_tick:
                interval = (tick - last_tick) * 1000 / frequency
                late = (now - tick) * 1000 / frequency
                ticks.append((interval, late))
                if interval > 70 or late > 8:
                    stalls.append((time.strftime("%H:%M:%S"), round(interval, 1), round(late, 1)))
            last_tick = tick
        if frame != last_frame:
            gap = (now - last_frame_at) * 1000 / frequency
            if last_frame is not None and gap > 20:
                frame_stalls.append((time.strftime("%H:%M:%S"), round(gap, 1)))
            last_frame, last_frame_at = frame, now
        time.sleep(0.0005)

    intervals = sorted(interval for interval, _ in ticks)
    lates = sorted(late for _, late in ticks)
    if intervals:
        count = len(intervals)
        print(f"ticks: {count}  interval median {intervals[count // 2]:.1f}ms p99 {intervals[int(count * 0.99)]:.1f} max {intervals[-1]:.1f}")
        print(f"tick published after its timestamp: median {lates[count // 2]:.1f}ms p99 {lates[int(count * 0.99)]:.1f} max {lates[-1]:.1f}")
    print(f"tick anomalies (interval>70ms or published >8ms late): {len(stalls)}")
    for stall in [stall for stall in stalls if stall[1] > 70][:20]:
        print("   BIG", stall)
    for stall in stalls[:10]:
        print("  ", stall)
    print(f"MC frame stalls >20ms: {len(frame_stalls)}")
    for stall in frame_stalls[:40]:
        print("  ", stall)


if __name__ == "__main__":
    main()
