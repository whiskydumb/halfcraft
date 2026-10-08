"""starts halfcraft's half-life 2 from its dev game folder (build\\game-<engine>, make build) on one of the two
engines (tools/halfcraft/engines.py); that engine's steam app must be installed. minecraft: make mc-run (the dev
client); it links up by itself once the game runs.

python tools/game/run_hl2.py                          hl2:dm's 64-bit engine, windowed 1280x720, main menu
python tools/game/run_hl2.py --engine hl2             half-life 2's own 32-bit engine
python tools/game/run_hl2.py --map d1_trainstation_02 straight into a map
python tools/game/run_hl2.py --fullscreen
python tools/game/run_hl2.py --width 2560 --height 1080
python tools/game/run_hl2.py -- +hc_debug_blocks 1    more engine arguments after --
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError, engines


def main() -> None:
    parser = argparse.ArgumentParser(description="start halfcraft's half-life 2 from its dev game folder")
    parser.add_argument("--engine", choices=list(engines.ENGINES), default="hl2dm")
    parser.add_argument("--map", help="straight into this map")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("extra", nargs="*", help="more engine arguments, after --")
    options = parser.parse_args()

    engine = engines.ENGINES[options.engine]
    if not (engine.game / engine.game_bin / "client.dll").is_file():
        raise ToolError(f"no build in {engine.game} - run make build ENGINE={engine.name} first")
    if game := engines.running():
        raise ToolError(f"{game[1].name} already runs: source runs one game at a time")
    exe = engine.find_exe()

    arguments = ["-game", engine.game, "-novid", "-condebug", "+con_enable", "1", "+developer", "1"]
    if not options.fullscreen:
        arguments += ["-windowed", "-w", str(options.width), "-h", str(options.height)]
    arguments += options.extra
    if options.map:
        arguments += ["+map", options.map]
    # on its own: no handles of this console, and no ctrl+c from it
    flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([exe, *arguments], cwd=exe.parent, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
    print(f"started {exe}; console log: {engine.game / 'console.log'}")


if __name__ == "__main__":
    main()
