"""the source sdk 2013 trees halfcraft builds against, one per engine, pinned to a commit, and halfcraft's edits
to them (source\\sdk\\halfcraft-<engine>.patch).

hl2dm  source-sdk-2013      hl2dm-sp: valve's 2025 sdk with the singleplayer campaigns building and running on
                            half-life 2: deathmatch's 64-bit engine
hl2    source-sdk-2013-sp   valve's singleplayer sdk as of 2015 (the last one it published), for half-life 2's
                            own 32-bit engine; its patch also makes it build with today's visual studio
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO, ToolError


@dataclass(frozen=True)
class Tree:
    engine: str
    folder: str  # in the repo
    src: str  # valve's code in it, as git names it
    url: str
    commit: str
    sparse: str | None = None  # the one folder checked out, if not all
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
        # valve's repository dropped the singleplayer tree in 2025: its last commit with sp\ (2015-09-09)
        url="https://github.com/ValveSoftware/source-sdk-2013.git",
        commit="0d8dceea4310fde5706b3ce1c70609d72a38efdf",
        sparse="sp",
        # valve's prebuilt vs2013 copies of these don't link with today's crt
        libs=("tier1/tier1.vcxproj", "mathlib/mathlib.vcxproj", "raytrace/raytrace.vcxproj", "vgui2/vgui_controls/vgui_controls.vcxproj"),
    ),
    "hl2dm": Tree(
        "hl2dm",
        "source-sdk-2013",
        "src",
        url="https://github.com/hardlightbridge/hl2dm-sp.git",
        commit="67f81f0f5a64a7f2bb3c0de02409ed63a0570ed5",  # 2026-10-02
    ),
}


def trees(engine: str) -> list[Tree]:
    """the tree of one engine, or both for "all"."""
    return list(TREES.values()) if engine == "all" else [TREES[engine]]
