"""the minecraft mod's gradle, on a JDK 25 (the mod needs one) whatever JAVA_HOME points at, with halfcraft's
version (halfcraft.version).
"""

import os
import re
from collections.abc import Sequence
from pathlib import Path

from halfcraft import REPO, ToolError, commands
from halfcraft.version import version

MINECRAFT = REPO / "minecraft"
JDK_VENDORS = ("Eclipse Adoptium", "Java", "Microsoft", "Zulu", "Amazon Corretto")
# a build, its tests or a check; the clients run as long as they're played
SECONDS = 3600
CLIENTS = {"runClient", "runTestClient"}


def find_jdk() -> Path:
    """JAVA_HOME if it's 25 or newer, else the newest one installed in program files."""
    candidates = [Path(home)] if (home := os.environ.get("JAVA_HOME")) else []
    # PROGRAMW6432 is the 64-bit program files even in a process a 32-bit one started (ezwinports' make)
    program_files = Path(os.environ.get("PROGRAMW6432") or os.environ["PROGRAMFILES"])
    for vendor in JDK_VENDORS:
        if (root := program_files / vendor).is_dir():
            candidates += sorted((jdk for jdk in root.iterdir() if jdk.is_dir()), key=lambda jdk: jdk.name, reverse=True)
    for jdk in candidates:
        release = jdk / "release"
        if (jdk / "bin/javac.exe").is_file() and release.is_file():
            found = re.search(r'JAVA_VERSION="(\d+)', release.read_text(encoding="utf-8", errors="replace"))
            if found and int(found[1]) >= 25:
                return jdk
    raise ToolError("no JDK 25 or newer found: set JAVA_HOME to one")


def run(tasks: Sequence[str]) -> None:
    """runs gradle tasks in minecraft\\ and waits for them."""
    env = {**os.environ, "JAVA_HOME": str(find_jdk())}
    timeout = None if CLIENTS.intersection(tasks) else SECONDS
    command = [MINECRAFT / "gradlew.bat", *tasks, "--no-configuration-cache", f"-Pversion={version()}"]
    commands.run(command, timeout=timeout, what=f"gradle {' '.join(tasks)}", cwd=MINECRAFT, env=env)
