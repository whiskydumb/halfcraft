"""clang-format and clang-tidy of the style's version: .clang-format's options are clang-format 22's."""

import os
import re
import shutil
import sys
from pathlib import Path

from halfcraft import ToolError, commands, visual_studio

MAJOR = 22


def find(tool: str, variable: str) -> Path:
    """clang-format or clang-tidy 22: $<variable> if set, else visual studio's (its c++ clang tools), else
    the one on the path. ci sets the variable to the pinned wheel's (.github/scripts/pinned.ps1).
    """
    candidates = [Path(value)] if (value := os.environ.get(variable)) else []
    if vs := visual_studio.installation():
        candidates.append(vs / f"VC/Tools/Llvm/x64/bin/{tool}.exe")
    if on_path := shutil.which(tool):
        candidates.append(Path(on_path))
    for exe in candidates:
        if not exe.is_file():
            continue
        said = commands.run([exe, "--version"], timeout=60, check=False, capture=True).stdout
        if re.search(rf"version {MAJOR}\.", said):
            return exe
        print(f"warning: {exe} isn't {tool} {MAJOR}", file=sys.stderr)
    raise ToolError(f"no {tool} {MAJOR} found: set {variable}, add visual studio's c++ clang tools, or pip install {tool}=={MAJOR}.*")
