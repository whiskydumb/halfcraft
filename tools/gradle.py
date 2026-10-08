"""runs the minecraft mod's gradle on a JDK 25, which the mod needs, with halfcraft's version
(tools/halfcraft/gradle.py finds the JDK).

    python tools/gradle.py build        the mod's jar (minecraft\\build\\libs) and its tests
    python tools/gradle.py runClient    the dev client (make mc-run checks no test session is on first)
"""

import sys

from halfcraft import gradle


def main() -> None:
    gradle.run(sys.argv[1:] or ["build"])


if __name__ == "__main__":
    main()
