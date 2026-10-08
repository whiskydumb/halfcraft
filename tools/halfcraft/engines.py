"""the two engines halfcraft runs on, and what they need.

hl2    half-life 2's own engine: 32-bit hl2.exe (app 220), dlls from source-sdk-2013-sp
hl2dm  half-life 2: deathmatch's: 64-bit hl2mp_win64.exe (app 320), dlls from source-sdk-2013
"""

import re
import winreg
from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO, ToolError, windows


@dataclass(frozen=True)
class Engine:
    name: str
    bin: str  # where its sdk tree publishes the episodic dlls (they run half-life 2 and both episodes, as valve's hl2_complete does)
    game_bin: str  # the game folder's subfolder its engine loads them from
    app: int  # the steam app whose folder has the engine
    steam_folder: str
    exe: str

    @property
    def game(self) -> Path:
        """its dev game folder (make build lays it out, make run plays from it)."""
        return REPO / f"build/game-{self.name}"

    def dlls(self) -> list[Path]:
        """client.dll and server.dll as its sdk tree's build publishes them."""
        return [REPO / self.bin / f"{dll}.dll" for dll in ("client", "server")]

    def find_exe(self) -> Path:
        """the engine's exe in whichever steam library has it (steamapps\\libraryfolders.vdf lists them)."""
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
        except OSError:
            raise ToolError("steam not found") from None
        libraries = [steam]
        if (vdf := steam / "steamapps/libraryfolders.vdf").is_file():
            listed = re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace"))
            libraries += [Path(path.replace("\\\\", "\\")) for path in listed]
        for library in libraries:
            if (exe := library / "steamapps/common" / self.steam_folder / self.exe).is_file():
                return exe
        raise ToolError(f"{self.steam_folder} isn't installed (steam app {self.app})")


ENGINES = {
    "hl2": Engine("hl2", "source-sdk-2013-sp/sp/game/mod_episodic/bin", "bin", 220, "Half-Life 2", "hl2.exe"),
    "hl2dm": Engine("hl2dm", "source-sdk-2013/game/mod_ep1/bin/x64", "bin/x64", 320, "Half-Life 2 Deathmatch", "hl2mp_win64.exe"),
}


def chosen(engine: str) -> list[Engine]:
    """one engine, or both for "all"."""
    return list(ENGINES.values()) if engine == "all" else [ENGINES[engine]]


def running() -> tuple[Engine, windows.Process] | None:
    """the running source game (halfcraft's or not: only one can run at a time) and its engine, or None."""
    found = windows.processes(*(engine.exe for engine in ENGINES.values()))
    if not found:
        return None
    game = found[0]
    return next(engine for engine in ENGINES.values() if engine.exe.lower() == game.name.lower()), game
