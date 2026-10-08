"""builds halfcraft's half-life 2 side (tools/halfcraft/build.py says what that takes).

python tools/build_hl2.py                   both engines: generate projects, build release, lay out
python tools/build_hl2.py --engine hl2      half-life 2's own 32-bit engine only (source-sdk-2013-sp)
python tools/build_hl2.py --engine hl2dm    half-life 2: deathmatch's 64-bit engine only (source-sdk-2013)
python tools/build_hl2.py --no-projects     skip vpc (no .vpc file changed)
python tools/build_hl2.py --no-launcher     skip HalfCraft.exe
"""

import argparse

from halfcraft import build, engines


def main() -> None:
    parser = argparse.ArgumentParser(description="build halfcraft's half-life 2 side")
    parser.add_argument("--engine", choices=["all", *engines.ENGINES], default="all")
    parser.add_argument("--no-projects", action="store_true", help="skip vpc (no .vpc file changed)")
    parser.add_argument("--no-launcher", action="store_true", help="skip HalfCraft.exe")
    parser.add_argument("--configuration", choices=["Release", "Debug"], default="Release")
    options = parser.parse_args()
    build.build(engines.chosen(options.engine), projects=not options.no_projects, launcher=not options.no_launcher, configuration=options.configuration)


if __name__ == "__main__":
    main()
