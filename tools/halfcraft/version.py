"""halfcraft's version, from git's tags. a commit tagged vX.Y.Z is release X.Y.Z; N commits past the newest
such tag are X.(Y+1).0-dev.N+<commit>: the release they lead to, and how far along. before the first tag
they lead to 0.1.0. $HALFCRAFT_VERSION wins when set (ci's jobs share one; a source tree without git).
the minecraft mod (tools/gradle.py), HalfCraft.exe (tools/halfcraft/build.py) and the release zip
(tools/package.py) all take it from here.
"""

import os
import re

from halfcraft import ToolError
from halfcraft.commands import git

DESCRIBED = re.compile(r"^v(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)$")
VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?")


def version() -> str:
    """the version, e.g. 0.2.0-dev.12+1a2b3c4d5."""
    if from_ci := os.environ.get("HALFCRAFT_VERSION"):
        return from_ci
    described = git("describe", "--tags", "--long", "--abbrev=9", "--match", "v[0-9]*.[0-9]*.[0-9]*", check=False)
    tagged = DESCRIBED.match(described.stdout.strip()) if described.returncode == 0 else None
    if tagged:
        major, minor, patch, ahead, commit = tagged.groups()
        if ahead == "0":
            return f"{major}.{minor}.{patch}"
        return f"{major}.{int(minor) + 1}.0-dev.{ahead}+{commit}"
    count = git("rev-list", "--count", "HEAD", check=False)
    commit = git("rev-parse", "--short=9", "HEAD", check=False)
    if count.returncode != 0 or commit.returncode != 0 or not count.stdout.strip():
        raise ToolError("no version: not a git checkout, and HALFCRAFT_VERSION isn't set")
    return f"0.1.0-dev.{count.stdout.strip()}+{commit.stdout.strip()}"


def numbers(text: str) -> tuple[int, int, int, int]:
    """the four numbers a windows version resource takes: 0.2.0-dev.12+... is 0, 2, 0, 12."""
    parsed = VERSION.match(text)
    if not parsed:
        raise ToolError(f"{text} isn't a halfcraft version")
    major, minor, patch, build = parsed.groups()
    return int(major), int(minor), int(patch), int(build or 0)
