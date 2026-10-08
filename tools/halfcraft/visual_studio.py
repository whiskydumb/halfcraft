"""where visual studio is (vswhere knows)."""

import os
from pathlib import Path

from halfcraft import commands


def installation(*components: str) -> Path | None:
    """the newest visual studio with these components (e.g. Microsoft.Component.MSBuild), or None."""
    vswhere = Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        return None
    command: list[str | Path] = [vswhere, "-latest", "-prerelease", "-products", "*", "-property", "installationPath"]
    for component in components:
        command += ["-requires", component]
    found = commands.run(command, timeout=60, check=False, capture=True).stdout.strip()
    return Path(found.splitlines()[0]) if found else None
