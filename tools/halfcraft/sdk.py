"""the source sdk 2013 trees halfcraft builds against, one per engine, and halfcraft's edits to them
(source\\sdk\\halfcraft-<engine>.patch).
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO, ToolError


@dataclass(frozen=True)
class Tree:
    engine: str
    folder: str  # in the repo
    src: str  # valve's code in it, as git names it
    libs: tuple[str, ...] = ()  # library projects (in src) the build makes before the game's dlls

    @property
    def path(self) -> Path:
        return REPO / self.folder

    @property
    def patch(self) -> Path:
        return REPO / f"source/sdk/halfcraft-{self.engine}.patch"

    def source(self) -> Path:
        """valve's code, checked out: make build stops here without it."""
        src = self.path / self.src
        if not (src / "createallprojects.bat").is_file():
            raise ToolError(f"no sdk at {src} - run make setup ENGINE={self.engine} first")
        return src

    def built(self) -> list[str]:
        """the tracked .lib files the libraries' build writes over, as git names them: build output, not edits."""
        return [f"{self.src}/lib/public/{Path(lib).stem}.lib" for lib in self.libs]

    def edited(self) -> list[str]:
        """git's pathspecs of what the patch holds."""
        return [self.src, *(f":(exclude){path}" for path in self.built())]


TREES = {
    "hl2": Tree(
        "hl2",
        "source-sdk-2013-sp",
        "sp/src",
        # valve's prebuilt vs2013 copies of these don't link with today's crt
        ("tier1/tier1.vcxproj", "mathlib/mathlib.vcxproj", "raytrace/raytrace.vcxproj", "vgui2/vgui_controls/vgui_controls.vcxproj"),
    ),
    "hl2dm": Tree("hl2dm", "source-sdk-2013", "src"),
}


def trees(engine: str) -> list[Tree]:
    """the tree of one engine, or both for "all"."""
    return list(TREES.values()) if engine == "all" else [TREES[engine]]
