"""what the scripts in tools\\ share: where the repo is, how they fail, and the modules next to this one."""

from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


class ToolError(SystemExit):
    """a failure the tool explains itself: python prints the message and exits 1, without a traceback."""
