"""clang-tidy over halfcraft's own c++ (.clang-tidy says which checks and which headers report): make tidy.

    python tools/tidy.py                  everything, both engines
    python tools/tidy.py --engine hl2dm   one engine's client.dll and server.dll (and the launcher, tools)
    python tools/tidy.py source/src/client/hc_block_lights.cpp ...   just these files
    python tools/tidy.py --checks="-*,bugprone-*"                    other checks than .clang-tidy's

each file is checked with the flags it's built with: client.dll's and server.dll's from the projects vpc
generates (make build makes them), the launcher's and tools\\*.cpp's from their cl lines. a file both dlls
build (source\\src\\core, shared) is checked for each, since CLIENT_DLL and GAME_DLL change it. findings in
the sdk trees' own code never report. exits 1 on any finding.

clang-tidy 22 (clang-format's version): $CLANG_TIDY if set, else visual studio's (its c++ clang tools),
else the one on the path. ci gets the pinned wheel's (.github/scripts/pinned.ps1 clang-tidy).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAJOR = 22
SECONDS_PER_FILE = 300
MSBUILD = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
OURS = ("source/src/", "source/launcher/", "protocol/", "tools/")
# source's RESTRICT puts __restrict on member function definitions but not their declarations: msvc
# lets that pass, clang calls them different functions. and its DEFINE_FIELD narrows a 32-bit sizeof
# into an int in a braced list, which clang calls an error in every save-restore table that uses it
CLANG_ONLY = ["-D__restrict=", "-Wno-c++11-narrowing"]


@dataclass(frozen=True)
class Project:
    engine: str
    path: Path  # the .vcxproj vpc writes
    config: str  # its Configuration|Platform
    target: str  # clang's triple for that platform


PROJECTS = [
    Project("hl2dm", REPO / "source-sdk-2013/src/game/client/client_win64_episodic.vcxproj", "Release|x64", "x86_64-pc-windows-msvc"),
    Project("hl2dm", REPO / "source-sdk-2013/src/game/server/server_win64_episodic.vcxproj", "Release|x64", "x86_64-pc-windows-msvc"),
    Project("hl2", REPO / "source-sdk-2013-sp/sp/src/game/client/client_episodic.vcxproj", "Release|Win32", "i686-pc-windows-msvc"),
    Project("hl2", REPO / "source-sdk-2013-sp/sp/src/game/server/server_episodic.vcxproj", "Release|Win32", "i686-pc-windows-msvc"),
]
# built by plain cl lines (tools/build_hl2.ps1, tools/collision_check.ps1), 64-bit
PLAIN = {
    "source/launcher/": ["-DUNICODE", "-D_UNICODE", f"-I{REPO / 'source/src'}", f"-I{REPO / 'protocol'}"],
    "tools/": [f"-I{REPO / 'source/src'}"],
}
DIAGNOSTIC = re.compile(r"^(?P<path>[A-Za-z]:[^:]+|[^:]+):(?P<line>\d+):(?P<col>\d+): (?P<kind>warning|error): (?P<text>.*?)(?: \[(?P<check>[^\]]+)\])?$")


@dataclass(frozen=True)
class Job:
    file: Path
    args: tuple[str, ...]  # the compile command after "--"
    label: str  # which build the flags are from


def find_clang_tidy() -> str:
    """clang-tidy of the same major version as the style's clang-format."""
    candidates = []
    if os.environ.get("CLANG_TIDY"):
        candidates.append(os.environ["CLANG_TIDY"])
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        found = subprocess.run([str(vswhere), "-latest", "-prerelease", "-products", "*", "-property", "installationPath"], capture_output=True, text=True, timeout=60)
        for line in found.stdout.splitlines():
            if line.strip():
                candidates.append(str(Path(line.strip()) / "VC/Tools/Llvm/x64/bin/clang-tidy.exe"))
    if shutil.which("clang-tidy"):
        candidates.append(shutil.which("clang-tidy"))
    for exe in candidates:
        if not Path(exe).exists():
            continue
        version = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=60).stdout
        if re.search(rf"LLVM version {MAJOR}\.", version):
            return exe
        print(f"warning: {exe} isn't clang-tidy {MAJOR}", file=sys.stderr)
    raise SystemExit(f"no clang-tidy {MAJOR} found: set CLANG_TIDY, add visual studio's c++ clang tools, or pip install clang-tidy=={MAJOR}.*")


def ours(path: Path) -> str | None:
    """the repo-relative path of one of halfcraft's own c++ files (sources and headers), or None."""
    try:
        relative = path.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return None
    return relative if relative.startswith(OURS) else None


def header_filter() -> str:
    """the headers whose findings report: halfcraft's own, wherever the repo is."""
    separator = r"[/\\]"
    root = separator.join(re.escape(part) for part in REPO.parts[1:])
    return rf"{root}{separator}(source{separator}(src|launcher)|protocol|tools){separator}"


def project_jobs(project: Project) -> list[Job]:
    """one job for each of halfcraft's files in a project, with that project's flags."""
    if not project.path.exists():
        raise SystemExit(f"{project.path.relative_to(REPO)} is missing: make build generates the projects")
    root = ElementTree.parse(project.path).getroot()
    base = project.path.parent
    settings = None
    for group in root.findall("m:ItemDefinitionGroup", MSBUILD):
        if f"=='{project.config}'" in group.get("Condition", ""):
            settings = group.find("m:ClCompile", MSBUILD)
    if settings is None:
        raise SystemExit(f"{project.path.name} has no {project.config} settings")

    def listed(tag: str) -> list[str]:
        node = settings.find(f"m:{tag}", MSBUILD)
        return [item for item in (node.text or "").split(";") if item and "%(" not in item] if node is not None else []

    args = ["--driver-mode=cl", "/TP", "/std:c++17", "/Zc:__cplusplus", f"--target={project.target}", *CLANG_ONLY]
    args += [f"-I{(base / include).resolve()}" for include in listed("AdditionalIncludeDirectories")]
    args += [f"-D{define}" for define in listed("PreprocessorDefinitions")]
    jobs = []
    for item in root.iter(f"{{{MSBUILD['m']}}}ClCompile"):
        source = item.get("Include")
        if source and source.endswith(".cpp") and ours(base / source):
            jobs.append(Job((base / source).resolve(), tuple(args), f"{project.engine} {project.path.stem}"))
    return jobs


def plain_jobs() -> list[Job]:
    """the launcher's and tools' files, with the flags of their cl lines."""
    jobs = []
    for folder, flags in PLAIN.items():
        args = ("--driver-mode=cl", "/TP", "/std:c++17", "/EHsc", "--target=x86_64-pc-windows-msvc", *flags)
        jobs += [Job(file, args, folder.rstrip("/")) for file in sorted((REPO / folder).glob("*.cpp"))]
    return jobs


def run(clang_tidy: str, job: Job, checks: str | None) -> str:
    command = [clang_tidy, "--quiet", f"--header-filter={header_filter()}", str(job.file)]
    if checks:
        command.insert(1, f"--checks={checks}")
    try:
        done = subprocess.run([*command, "--", *job.args], capture_output=True, text=True, cwd=REPO, timeout=SECONDS_PER_FILE)
    except subprocess.TimeoutExpired:
        return f"{job.file}:1:1: error: clang-tidy took over {SECONDS_PER_FILE} s [tidy-timeout]\n"
    return done.stdout + done.stderr


def findings(output: str) -> dict[tuple[str, int, int, str], list[str]]:
    """each finding in clang-tidy's output (the diagnostic line and the notes and code under it), by where it is."""
    found: dict[tuple[str, int, int, str], list[str]] = {}
    current = None
    for line in output.splitlines():
        match = DIAGNOSTIC.match(line)
        if match:
            # .clang-tidy's WarningsAsErrors tags every check name with ",-warnings-as-errors"
            check = ",".join(name for name in (match["check"] or match["kind"]).split(",") if name != "-warnings-as-errors")
            relative = ours(Path(match["path"]))
            if relative is None and check == "clang-diagnostic-error":
                relative = match["path"]  # a file that doesn't compile leaves the checks half blind, wherever it broke
            if relative is None:
                current = None  # the sdk's, or a macro from the command line
                continue
            current = (relative, int(match["line"]), int(match["col"]), check)
            found.setdefault(current, [f"{relative}:{match['line']}:{match['col']}: {match['kind']}: {match['text']} [{current[3]}]"])
            continue
        if current and line.strip() and not line.startswith(("Suppressed ", "Use -header-filter", "Found compiler error")) and not re.match(r"^\d+ warnings? (and \d+ errors? )?generated", line):
            if line not in found[current]:
                found[current].append(line)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description="clang-tidy over halfcraft's own c++")
    parser.add_argument("files", nargs="*", type=Path, help="only these files")
    parser.add_argument("--engine", choices=["hl2", "hl2dm", "all"], default="all")
    parser.add_argument("--checks", help="instead of .clang-tidy's checks")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    options = parser.parse_args()

    clang_tidy = find_clang_tidy()
    jobs = [job for project in PROJECTS if options.engine in ("all", project.engine) for job in project_jobs(project)] + plain_jobs()
    if options.files:
        wanted = {file.resolve() for file in options.files}
        jobs = [job for job in jobs if job.file in wanted]
        if not jobs:
            raise SystemExit("none of those files is one of halfcraft's own .cpp files in a build")
    print(f"clang-tidy: {len(jobs)} checks of {len({job.file for job in jobs})} files with {clang_tidy}")

    found: dict[tuple[str, int, int, str], list[str]] = {}
    with ThreadPoolExecutor(max_workers=options.jobs) as pool:
        for output in pool.map(lambda job: run(clang_tidy, job, options.checks), jobs):
            for key, lines in findings(output).items():
                found.setdefault(key, lines)
    for key in sorted(found):
        print("\n".join(found[key]))
    by_check: dict[str, int] = {}
    for key in found:
        by_check[key[3]] = by_check.get(key[3], 0) + 1
    for check, count in sorted(by_check.items(), key=lambda item: (-item[1], item[0])):
        print(f"{count:5}  {check}")
    print(f"clang-tidy: {len(found)} findings" if found else "clang-tidy: no findings")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
