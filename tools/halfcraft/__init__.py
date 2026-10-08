"""what the scripts in tools\\ share: where the repo is, how they fail, and the modules next to this one."""

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# a pipe (a test script's, ci's) gets windows' code page, which can't hold every character a game's log or a
# path may have: utf-8 can
for _stream in (sys.stdout, sys.stderr):
    if _stream is not None:
        _stream.reconfigure(encoding="utf-8", errors="replace")


class ToolError(SystemExit):
    """a failure the tool explains itself: python prints the message and exits 1, without a traceback."""
