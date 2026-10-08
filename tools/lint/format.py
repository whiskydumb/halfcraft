"""formats halfcraft's own code, or checks that it is: make format, make lint.

    c++      clang-format (.clang-format says how): source\\src, source\\launcher and tools\\debug\\*.cpp, tracked
             or new; never the sdk trees, nor protocol\\halfcraft_protocol.h (tools\\build\\protocol.py writes it)
    python   ruff (pyproject.toml says how, and which version): its formatter and its linter over tools\\ and
             .github\\scripts; make format also applies the fixes ruff calls safe

the java is spotless's (minecraft\\eclipse-formatter.prefs), which make format and make lint run too.

    python tools/lint/format.py           rewrites what isn't formatted
    python tools/lint/format.py --check   lists it and fails; writes nothing

clang-format 22 (tools/halfcraft/llvm.py finds it). ruff: $RUFF if set, else the one on the path if it's the
pinned version, else uv's (uvx ruff@<version>). ci sets $RUFF to the pinned wheel's (.github/scripts/pinned.py).
"""

import argparse
import os
import re
import shutil
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, commands, llvm

PATTERNS = ("source/src/*.cpp", "source/src/*.h", "source/launcher/*.cpp", "source/launcher/*.h", "tools/debug/*.cpp")
PYTHON = ("tools", ".github/scripts")
SECONDS = 300
# --dry-run warns once for every line that would change
UNFORMATTED = re.compile(r"^(.+?):\d+:\d+: (?:warning|error):", re.MULTILINE)


def cpp_files() -> list[Path]:
    listed = commands.git("ls-files", "--cached", "--others", "--exclude-standard", "--", *PATTERNS).stdout.splitlines()
    found = [REPO / name for name in listed if (REPO / name).is_file()]
    if not found:
        raise ToolError(f"no c++ files found under {REPO}")
    return found


def cpp(*, check: bool) -> bool:
    """clang-format over halfcraft's c++; False when --check finds files that aren't formatted."""
    clang_format = llvm.find("clang-format", "CLANG_FORMAT")
    sources = cpp_files()
    if not check:
        commands.run([clang_format, "--style=file", "-i", *sources], timeout=SECONDS)
        print(f"formatted {len(sources)} c++ files with {clang_format}")
        return True
    report = commands.run([clang_format, "--style=file", "--dry-run", *sources], timeout=SECONDS, check=False, capture=True)
    unformatted = sorted({Path(match[1]) for match in UNFORMATTED.finditer(report.stdout + report.stderr)})
    if not unformatted and report.returncode != 0:
        raise ToolError(f"clang-format failed ({report.returncode}): {report.stderr.strip()}")
    for path in unformatted:
        print(f"not formatted: {path.relative_to(REPO).as_posix()}")
    print(f"c++: {len(unformatted)} of {len(sources)} files aren't formatted" if unformatted else f"c++: {len(sources)} files formatted")
    return not unformatted


def find_ruff() -> list[str]:
    """the command that runs the ruff pyproject.toml pins."""
    version = tomllib.loads((REPO / "pyproject.toml").read_text(encoding="utf-8"))["tool"]["ruff"]["required-version"].removeprefix("==")
    if from_env := os.environ.get("RUFF"):
        return [from_env]
    if on_path := shutil.which("ruff"):
        said = commands.run([on_path, "--version"], timeout=60, check=False, capture=True).stdout.split()
        if said[-1:] == [version]:
            return [on_path]
        print(f"warning: {on_path} isn't ruff {version}", file=sys.stderr)
    if uvx := shutil.which("uvx"):
        return [uvx, f"ruff@{version}"]
    raise ToolError(f"no ruff {version} found: set RUFF, pip install ruff=={version}, or install uv")


def python(*, check: bool) -> bool:
    """ruff's formatter and linter over halfcraft's python (they print what they find); False when they find any."""
    ruff = find_ruff()
    formatted = commands.run([*ruff, "format", *(["--check"] if check else []), *PYTHON], timeout=SECONDS, what="ruff format", cwd=REPO, check=False)
    linted = commands.run([*ruff, "check", *([] if check else ["--fix"]), *PYTHON], timeout=SECONDS, what="ruff check", cwd=REPO, check=False)
    return formatted.returncode == 0 and linted.returncode == 0


def main() -> None:
    parser = argparse.ArgumentParser(description="format halfcraft's c++ and python, or check that they are")
    parser.add_argument("--check", action="store_true", help="list what isn't formatted and fail; write nothing")
    options = parser.parse_args()

    failed = [language for language, clean in (("c++", cpp(check=options.check)), ("python", python(check=options.check))) if not clean]
    if failed and options.check:
        raise ToolError(f"{' and '.join(failed)} aren't clean: run make format")
    if failed:
        raise ToolError("ruff found what it can't fix itself (above)")


if __name__ == "__main__":
    main()
