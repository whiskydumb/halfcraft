"""ci's tools, each one download pinned by its sha256: fetches one into the runner's temp folder and points the
job's later steps at it (GITHUB_ENV).

python .github/scripts/pinned.py clang-format   CLANG_FORMAT: clang-format 22.1.3, out of the windows wheel pip would install
python .github/scripts/pinned.py clang-tidy     CLANG_TIDY: clang-tidy 22.1.8, the same way (pypi has no 22.1.3 of it)
python .github/scripts/pinned.py jdk            JAVA_HOME: temurin 25.0.4.1+1
python .github/scripts/pinned.py ruff           RUFF: ruff 0.16.9 (pyproject.toml's), out of its windows wheel
"""

import argparse
import os
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

# halfcraft is in tools, next to .github
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from halfcraft import ToolError, downloads


@dataclass(frozen=True)
class Pin:
    url: str
    sha256: str
    file: str  # the file in the archive that says where the tool is
    variable: str  # the environment variable the job's later steps find it by


PINS = {
    "clang-format": Pin(
        "https://files.pythonhosted.org/packages/86/07/e5a31c0865e1e10d591c4e511e0db79873c0cf48bb62c3934543295b6f5a/clang_format-22.1.3-py2.py3-none-win_amd64.whl",
        "426453233bea583775542da9b859de6e088bf32b4f393527fc5b05bbc7d33ae3",
        "clang-format.exe",
        "CLANG_FORMAT",
    ),
    "clang-tidy": Pin(
        "https://files.pythonhosted.org/packages/54/af/0580f6145d8a0c218844208a6c90ce539a4bdf65a66b672e6b4604a81c0a/clang_tidy-22.1.8-py2.py3-none-win_amd64.whl",
        "df9bf841ecbf501d08b6fa34523be840f59b1a76d16b6ee5edb0e5c7c22b59c2",
        "clang-tidy.exe",
        "CLANG_TIDY",
    ),
    "jdk": Pin(
        "https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.4.1%2B1/OpenJDK25U-jdk_x64_windows_hotspot_25.0.4.1_1.zip",
        "00c847d804f4a78e9f04f2683faf14fed898535b177b7fc704486cb0284e9283",
        "javac.exe",
        "JAVA_HOME",
    ),
    "ruff": Pin(
        "https://files.pythonhosted.org/packages/14/21/26e4643629b3ebb44f0a06f9c9a53058d63d989415f63a9a3c28e2ee7f22/ruff-0.16.9-py3-none-win_amd64.whl",
        "6bd40fec8cd4c8a3d4dd589bd8ad4e6320c13c29234159bfd959a40d529d597b",
        "ruff.exe",
        "RUFF",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser(description="fetch one of ci's pinned tools")
    parser.add_argument("tool", choices=list(PINS))
    options = parser.parse_args()
    pin = PINS[options.tool]

    pinned = Path(os.environ["RUNNER_TEMP"]) / "pinned"
    archive = pinned / f"{options.tool}.zip"  # a wheel is a zip too
    root = pinned / options.tool
    downloads.fetch(pin.url, archive, "sha256", pin.sha256)
    with zipfile.ZipFile(archive) as unpacked:
        unpacked.extractall(root)
    found = next(root.rglob(pin.file), None)
    if not found:
        raise ToolError(f"no {pin.file} in {pin.url}")
    value = found.parent.parent if options.tool == "jdk" else found  # the jdk's home is the folder over bin\
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as env:
        env.write(f"{pin.variable}={value}\n")
    print(f"{options.tool}: {value}")


if __name__ == "__main__":
    main()
