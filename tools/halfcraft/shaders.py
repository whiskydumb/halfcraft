"""the stock world flashlight shaders with halfcraft's edits (halfcraft-hl2dm.patch). make build compiles them
under their stock names into build\\shaders\\out; the game folders get them in shaders\\fxc, where the engine finds
them before half-life 2's vpk. both engines take the same files (the stock ones are byte for byte the same in
half-life 2 and hl2:dm).
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO

OUT = REPO / "build/shaders/out"


@dataclass(frozen=True)
class Shader:
    source: str  # in the sdk's materialsystem\stdshaders
    name: str  # the .vcs it compiles to
    # the stock combo layout, which stdshader_dx9.dll indexes the compiled shader by
    combos: int
    dynamic: int

    @property
    def vcs(self) -> Path:
        return OUT / f"{self.name}.vcs"


SHADERS = (
    Shader("flashlight_ps2x.fxc", "flashlight_ps20b", 1152, 4),
    Shader("lightmappedgeneric_flashlight_vs20.fxc", "lightmappedgeneric_flashlight_vs20", 32, 2),
)
