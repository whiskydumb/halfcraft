"""the source sdk 2013 trees halfcraft builds against, one per engine, and halfcraft's edits to them
(source\\sdk\\halfcraft-<engine>.patch).
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO


@dataclass(frozen=True)
class Tree:
    engine: str
    folder: str  # in the repo
    src: str  # valve's code in it, as git names it
    built: tuple[str, ...] = ()  # tracked files its build writes over: build output, not edits

    @property
    def path(self) -> Path:
        return REPO / self.folder

    @property
    def patch(self) -> Path:
        return REPO / f"source/sdk/halfcraft-{self.engine}.patch"

    def edited(self) -> list[str]:
        """git's pathspecs of what the patch holds."""
        return [self.src, *(f":(exclude){path}" for path in self.built)]


TREES = {
    "hl2": Tree(
        "hl2",
        "source-sdk-2013-sp",
        "sp/src",
        # the build writes these over the prebuilt vs2013 copies, which don't link with today's crt
        tuple(f"sp/src/lib/public/{lib}.lib" for lib in ("tier1", "mathlib", "raytrace", "vgui_controls")),
    ),
    "hl2dm": Tree("hl2dm", "source-sdk-2013", "src"),
}


def trees(engine: str) -> list[Tree]:
    """the tree of one engine, or both for "all"."""
    return list(TREES.values()) if engine == "all" else [TREES[engine]]
