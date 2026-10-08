"""formats halfcraft's own c++ with clang-format (.clang-format says how), or checks that it is: make format,
make lint. the files: source\\src, source\\launcher, protocol and tools\\*.cpp, tracked or new; never the sdk
trees. the java is spotless's (minecraft\\eclipse-formatter.prefs), which make format and make lint run too.

    python tools/format.py           rewrites the files that aren't formatted
    python tools/format.py --check   lists them and fails; writes nothing

clang-format 22 (tools/halfcraft/llvm.py finds it).
"""

import argparse
import re
from pathlib import Path

from halfcraft import REPO, ToolError, commands, llvm

PATTERNS = ("source/src/*.cpp", "source/src/*.h", "source/launcher/*.cpp", "source/launcher/*.h", "protocol/*.h", "tools/*.cpp")
SECONDS = 300
# --dry-run warns once for every line that would change
UNFORMATTED = re.compile(r"^(.+?):\d+:\d+: (?:warning|error):", re.MULTILINE)


def files() -> list[Path]:
    listed = commands.git("ls-files", "--cached", "--others", "--exclude-standard", "--", *PATTERNS).stdout.splitlines()
    found = [REPO / name for name in listed if (REPO / name).is_file()]
    if not found:
        raise ToolError(f"no c++ files found under {REPO}")
    return found


def check(clang_format: Path, sources: list[Path]) -> None:
    report = commands.run([clang_format, "--style=file", "--dry-run", *sources], timeout=SECONDS, check=False, capture=True)
    unformatted = sorted({Path(match[1]) for match in UNFORMATTED.finditer(report.stdout + report.stderr)})
    if not unformatted and report.returncode != 0:
        raise ToolError(f"clang-format failed ({report.returncode}): {report.stderr.strip()}")
    for path in unformatted:
        print(f"not formatted: {path.relative_to(REPO).as_posix()}")
    if unformatted:
        raise ToolError(f"{len(unformatted)} of {len(sources)} files aren't formatted: run make format")
    print(f"formatted: {len(sources)} files")


def main() -> None:
    parser = argparse.ArgumentParser(description="clang-format over halfcraft's own c++")
    parser.add_argument("--check", action="store_true", help="list the files that aren't formatted and fail; write nothing")
    options = parser.parse_args()

    clang_format = llvm.find("clang-format", "CLANG_FORMAT")
    sources = files()
    if options.check:
        check(clang_format, sources)
        return
    commands.run([clang_format, "--style=file", "-i", *sources], timeout=SECONDS)
    print(f"formatted {len(sources)} files with {clang_format}")


if __name__ == "__main__":
    main()
