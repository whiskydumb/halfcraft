"""the two engines halfcraft runs on, and what they need.

hl2    half-life 2's own engine: 32-bit hl2.exe (app 220), dlls from source-sdk-2013-sp
hl2dm  half-life 2: deathmatch's: 64-bit hl2mp_win64.exe (app 320), dlls from source-sdk-2013
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO


@dataclass(frozen=True)
class Engine:
    name: str
    bin: str  # where its sdk tree publishes the episodic dlls (they run half-life 2 and both episodes, as valve's hl2_complete does)
    game_bin: str  # the game folder's subfolder its engine loads them from
    app: int  # the steam app whose folder has the engine
    steam_folder: str
    exe: str

    def dlls(self) -> list[Path]:
        """client.dll and server.dll as its sdk tree's build publishes them."""
        return [REPO / self.bin / f"{dll}.dll" for dll in ("client", "server")]


ENGINES = {
    "hl2": Engine("hl2", "source-sdk-2013-sp/sp/game/mod_episodic/bin", "bin", 220, "Half-Life 2", "hl2.exe"),
    "hl2dm": Engine("hl2dm", "source-sdk-2013/game/mod_ep1/bin/x64", "bin/x64", 320, "Half-Life 2 Deathmatch", "hl2mp_win64.exe"),
}


def chosen(engine: str) -> list[Engine]:
    """one engine, or both for "all"."""
    return list(ENGINES.values()) if engine == "all" else [ENGINES[engine]]
