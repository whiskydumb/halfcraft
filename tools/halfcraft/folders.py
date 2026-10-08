"""folders the tools lay out and throw away."""

import os
import shutil
import stat
from collections.abc import Callable
from pathlib import Path


def remove_tree(path: Path) -> None:
    """removes a folder with everything in it, read-only files too (ShaderCompile2 writes its .inc files so)."""

    def writable(function: Callable[[str], object], name: str, _: BaseException) -> None:
        os.chmod(name, stat.S_IWRITE)  # noqa: PTH101 rmtree hands a str
        function(name)

    if path.exists():
        shutil.rmtree(path, onexc=writable)
