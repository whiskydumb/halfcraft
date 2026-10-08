"""runs the minecraft mod's gradle on a JDK 25, which the mod needs, with halfcraft's version
(tools/halfcraft/gradle.py finds the JDK).

    python tools/build/gradle.py build        the mod's jar (minecraft\\build\\libs) and its tests
    python tools/build/gradle.py runClient    the dev client (make mc-run checks no test session is on first)
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import gradle


def main() -> None:
    gradle.run(sys.argv[1:] or ["build"])


if __name__ == "__main__":
    main()
