"""checks a map's collision voxels offline: wherever half-life 2's player can stand, minecraft's mobs and items
must find a voxel to stand on too. builds tools\\debug\\collision_check.cpp (the streamer's own voxelizer,
source\\src\\core\\hc_collision_shapes.h) into build\\collision_check and runs it on the map's .bsp from half-life 2's
install. no game has to run.

python tools/debug/collision_check.py --map d1_canals_01 --grid-z 16                    the whole map
python tools/debug/collision_check.py --map d1_canals_01 --grid-z 16 --at 217,-968      512 units square around a spot (x, y;
                                                                                  --at=-217,968 when x is negative)
python tools/debug/collision_check.py --bsp C:\\maps\\x.bsp --grid-z 0                    any .bsp

--grid-z: the map's grid height, as server.dll logs it on a load ("grid offset for d1_canals_01: 16 units").
exits 1 when it finds holes, and lists the worst of them (source coordinates). it also prints what the
streamer's worker thread spends on a region (triangulate + voxelize), on average and at worst.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, commands, engines, visual_studio

OUT = REPO / "build/collision_check"
EXE = OUT / "collision_check.exe"
SOURCE = REPO / "tools/debug/collision_check.cpp"
COMPILE_SECONDS = 600
CHECK_SECONDS = 1800


def find_bsp(name: str) -> Path:
    """a map of half-life 2 or its episodes from half-life 2's install."""
    install = engines.ENGINES["hl2"].find_exe().parent
    for game in ("hl2", "episodic", "ep2"):
        if (bsp := install / game / "maps" / f"{name}.bsp").is_file():
            return bsp
    raise ToolError(f"{name}.bsp isn't in half-life 2's install ({install})")


def build() -> None:
    """compiles the checker when it or the headers it takes from core\\ changed since."""
    sources = [SOURCE, *(REPO / "source/src/core").glob("hc_*.h")]
    if EXE.is_file() and all(source.stat().st_mtime <= EXE.stat().st_mtime for source in sources):
        return
    vs = visual_studio.installation("Microsoft.VisualStudio.Component.VC.Tools.x86.x64")
    if not vs:
        raise ToolError("visual studio with the c++ compiler not found")
    OUT.mkdir(parents=True, exist_ok=True)
    cl = f'cl /nologo /O2 /W4 /EHsc /std:c++17 /I "{REPO}\\source\\src" /Fo"{OUT}\\\\" /Fe"{EXE}" "{SOURCE}" || exit /b 1'
    visual_studio.run_script(vs, OUT / "build.cmd", [cl], timeout=COMPILE_SECONDS, what="collision_check build")


def spot(text: str) -> tuple[float, float]:
    try:
        x, y = (float(part) for part in text.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError("a spot is x,y (source units)") from None
    return x, y


def main() -> int:
    parser = argparse.ArgumentParser(description="check a map's collision voxels offline")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--map", help="a map of half-life 2 or its episodes")
    source.add_argument("--bsp", type=Path, help="any .bsp")
    parser.add_argument("--grid-z", type=int, required=True, help="the map's grid height (server.dll logs it on a load)")
    parser.add_argument("--at", type=spot, help="only around this spot: x,y")
    parser.add_argument("--radius", type=float, default=256, help="half the square's side around --at")
    options = parser.parse_args()

    bsp = options.bsp or find_bsp(options.map)
    build()
    arguments = [bsp, str(options.grid_z)]
    if options.at:
        arguments += [f"{options.at[0]:g}", f"{options.at[1]:g}", f"{options.radius:g}"]
    return commands.run([EXE, *arguments], timeout=CHECK_SECONDS, what="collision_check", check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
