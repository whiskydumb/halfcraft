"""waits for a line in a game's log, for test scripts: half-life's console.log (run with -condebug, as make run
does) or minecraft's latest.log.

mark=$(python tools/game/wait_log.py mc-test --mark)          where the log ends now (a byte offset)
python tools/game/hl2_command.py "hc_mc kill @s"
python tools/game/wait_log.py hl2dm --after "$mark" --pattern "so does gordon" --timeout 20
python tools/game/wait_log.py mc-test --after "$mark" --pattern "teleported" --absent --timeout 5

the log: hl2 or hl2dm (build\\game-<engine>\\console.log), mc (minecraft\\run, the player's dev client), mc-test
(minecraft\\run\\test, make mc-test's) or a file's path. --pattern is a python regex. lines before --after don't
count; without it only lines written after this starts do, which can miss a line the command before already
caused: take a --mark first. prints the line it found and exits 0; exits 1 when the timeout passes without
one. --absent turns it round: 0 when no line matched in time.
"""

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, engines, windows

LOGS = {
    **{name: engine.game / "console.log" for name, engine in engines.ENGINES.items()},
    "mc": REPO / "minecraft/run/logs/latest.log",
    "mc-test": REPO / "minecraft/run/test/logs/latest.log",
}
POLL_SECONDS = 0.25


def length(path: Path) -> int:
    return path.stat().st_size if path.is_file() else 0


def wait(path: Path, pattern: re.Pattern[str], after: int, timeout: float) -> str | None:
    """the first line after byte offset `after` that matches, or None when none does in time."""
    offset = after
    partial = b""  # the line the game is still writing
    deadline = time.monotonic() + timeout
    while True:
        log = windows.open_shared(path)
        if log:
            with log:
                size = log.seek(0, 2)
                if size < offset:  # a shorter file is a new one (a restarted minecraft): read it from the start
                    offset, partial = 0, b""
                log.seek(offset)
                written = log.read(size - offset)
            offset += len(written)
            *finished, partial = (partial + written).split(b"\n")
            for line in finished:
                text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                if pattern.search(text):
                    return text
        if time.monotonic() >= deadline:
            return None
        time.sleep(POLL_SECONDS)


def main() -> int:
    parser = argparse.ArgumentParser(description="wait for a line in a game's log")
    parser.add_argument("log", help="hl2, hl2dm, mc, mc-test or a file's path")
    parser.add_argument("--pattern", help="a python regex")
    parser.add_argument("--timeout", type=float, default=30, help="seconds")
    parser.add_argument("--after", type=int, default=-1, help="a byte offset from --mark")
    parser.add_argument("--mark", action="store_true", help="print where the log ends now")
    parser.add_argument("--absent", action="store_true", help="succeed when no line matches in time")
    options = parser.parse_args()

    path = LOGS.get(options.log, Path(options.log))
    if options.mark:
        print(length(path))
        return 0
    if not options.pattern:
        raise ToolError("--pattern is needed (or --mark)")
    after = options.after if options.after >= 0 else length(path)
    line = wait(path, re.compile(options.pattern), after, options.timeout)
    if line is not None:
        print(f"{'unexpected' if options.absent else 'found'} in {options.log}: {line}")
    else:
        print(f"not in {options.log} within {options.timeout:g} s{', as expected' if options.absent else ''}: {options.pattern}")
    return int((line is not None) == options.absent)


if __name__ == "__main__":
    raise SystemExit(main())
