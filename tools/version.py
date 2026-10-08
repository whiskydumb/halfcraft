"""prints halfcraft's version (tools/halfcraft/version.py says where it comes from).

python tools/version.py             0.2.0-dev.12+1a2b3c4d5
python tools/version.py --numbers   the four numbers a windows version resource takes: 0,2,0,12
"""

import argparse

from halfcraft.version import numbers, version


def main() -> None:
    parser = argparse.ArgumentParser(description="halfcraft's version, from git's tags")
    parser.add_argument("--numbers", action="store_true", help="as a windows version resource's four numbers")
    options = parser.parse_args()
    print(",".join(map(str, numbers(version()))) if options.numbers else version())


if __name__ == "__main__":
    main()
