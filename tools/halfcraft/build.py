"""halfcraft's half-life 2 side for one or both engines: client.dll + server.dll, two world shaders, the dev
game folders build\\game-<engine>, then HalfCraft.exe, a release's launcher (build\\launcher).

it needs both patched sdk trees (make setup: the shaders always come from source-sdk-2013) and visual studio
2022+ with its x64 and x86 compilers. the games have to be closed: their dlls get replaced. halfcraft's own
code (source\\) builds without warnings: one there fails the build (the sdk's are valve's).
"""

import os
import re
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO, ToolError, commands, folders, game_folder, sdk, shaders, version, visual_studio, windows
from halfcraft.engines import ENGINES, Engine

LOGS = REPO / "build/logs"
VPC_SECONDS = 600
MSBUILD_SECONDS = 3600
COMPILE_SECONDS = 600
# msbuild /m puts its node's number in front of each line
NODE = re.compile(r"^\s*\d+>")
PROJECT = re.compile(r" \[[^\]]+\]$")


@dataclass(frozen=True)
class Toolchain:
    vs: Path
    msbuild: Path
    toolset: str
    windows_sdk: str
    configuration: str  # Release or Debug


def toolchain(configuration: str) -> Toolchain:
    vs = visual_studio.installation("Microsoft.Component.MSBuild")
    if not vs:
        raise ToolError("visual studio with msbuild not found")
    # the sdks' projects ask for vs2013's and vs2022's toolsets; build with whichever one this visual studio has
    toolsets = sorted(
        {folder.name for folder in (vs / "MSBuild/Microsoft/VC").rglob("v14*") if folder.is_dir() and folder.parent.name.lower() == "platformtoolsets"}
    )
    if not toolsets:
        raise ToolError(f"no v14x platform toolset in {vs}")
    toolset = "v143" if "v143" in toolsets else toolsets[-1]
    # the 2015 tree's vs2013 projects name no windows sdk; give them the newest one installed
    include = Path(os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")) / "Windows Kits/10/Include"
    kits = [kit.name for kit in include.glob("*") if re.fullmatch(r"\d+(\.\d+)+", kit.name) and (kit / "um/windows.h").is_file()]
    if not kits:
        raise ToolError("no windows 10/11 sdk found")
    windows_sdk = max(kits, key=lambda kit: tuple(map(int, kit.split("."))))
    return Toolchain(vs, vs / "MSBuild/Current/Bin/MSBuild.exe", toolset, windows_sdk, configuration)


def msbuild(chain: Toolchain, project: str, platform: str, cwd: Path, target: str = "Build") -> None:
    """builds a project or solution, then fails on any warning in halfcraft's own files (the sdk compiles them
    too, with each of its projects).
    """
    warnings = LOGS / f"{Path(project).stem}-{platform}-warnings.log"
    command = [
        chain.msbuild,
        project,
        f"/t:{target}",
        "/m",
        "/nologo",
        "/v:minimal",
        f"/p:Configuration={chain.configuration}",
        f"/p:Platform={platform}",
        f"/p:PlatformToolset={chain.toolset}",
        f"/p:WindowsTargetPlatformVersion={chain.windows_sdk}",
        f"/flp:LogFile={warnings};WarningsOnly",
    ]
    commands.run(command, timeout=MSBUILD_SECONDS, what=f"build of {project}", cwd=cwd)
    ours = str(REPO / "source").lower() + "\\"
    found = set()
    for logged in warnings.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        line = NODE.sub("", logged)
        if "warning" in line.lower() and line.lower().startswith(ours):
            found.add(PROJECT.sub("", line))
    if found:
        print("\n".join(sorted(found, key=str.lower)))
        raise ToolError(f"halfcraft's code has {len(found)} warnings (above): fix them")


def build_hl2dm(chain: Toolchain, *, projects: bool) -> None:
    """half-life 2: deathmatch's 64-bit engine: hl2dm-sp, valve's 2025 tree."""
    src = sdk.TREES["hl2dm"].source()
    if projects:
        commands.run(
            [src / "devtools/bin/vpc.exe", "/episodic", "/define:SOURCESDK", "+game", "/mksln", "games_episodic.sln"], timeout=VPC_SECONDS, what="vpc", cwd=src
        )
    msbuild(chain, "games_episodic.sln", "win64", src)


def build_hl2(chain: Toolchain, *, projects: bool) -> None:
    """half-life 2's own 32-bit engine: valve's singleplayer tree of 2015. its vpc (2014) writes the projects
    but no .sln without vs2013's registry key, so they're built one by one: the libraries the game links first.
    """
    tree = sdk.TREES["hl2"]
    src = tree.source()
    if projects:
        commands.run([src / "devtools/bin/vpc.exe", "/episodic", "+game", "/f"], timeout=VPC_SECONDS, what="vpc", cwd=src)
    # every configuration of these writes the same tracked file, sp\src\lib\public\<name>.lib, and msbuild
    # calls it up to date while it's newer than the objects. so they're rebuilt when it's still valve's
    # vs2013 copy (setup, a git restore) or the last build was another configuration
    stamp = REPO / "build/hl2-libs-configuration.txt"
    rebuild = not stamp.is_file() or stamp.read_text(encoding="ascii").strip().lower() != chain.configuration.lower()
    for lib in tree.built():
        if commands.git("diff", "--quiet", "--", lib, repo=tree.path, check=False).returncode == 0:
            rebuild = True
    for project in tree.libs:
        msbuild(chain, str(Path(project)), "Win32", src, "Rebuild" if rebuild else "Build")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_bytes(f"{chain.configuration}\r\n".encode("ascii"))
    for project in ("game/client/client_episodic.vcxproj", "game/server/server_episodic.vcxproj"):
        msbuild(chain, str(Path(project)), "Win32", src)


def build_shaders() -> None:
    """compiled in a copy of the sdk's shader folder: ShaderCompile2 also writes .inc files next to its input,
    which would land in the sdk's tree. the 2015 tree has neither the compiler nor flashlight_ps2x.fxc, so they
    always come from source-sdk-2013.
    """
    src = sdk.TREES["hl2dm"].source()
    stdshaders = src / "materialsystem/stdshaders"
    stage = REPO / "build/shaders"
    folders.remove_tree(stage)
    stage.mkdir(parents=True)
    for header in stdshaders.glob("*.h"):
        shutil.copy2(header, stage)
    shaders.OUT.mkdir()
    for shader in shaders.SHADERS:
        shutil.copy2(stdshaders / shader.source, stage)
        command = [src / "devtools/bin/ShaderCompile2.exe", "-ver", "20b", "-shaderpath", stage, shader.source]
        compiled = commands.run(command, timeout=COMPILE_SECONDS, what=f"ShaderCompile2 on {shader.source}", cwd=REPO, check=False, capture=True)
        vcs = stage / f"shaders/fxc/{shader.name}.vcs"
        if compiled.returncode != 0 or not vcs.is_file():
            raise ToolError(f"ShaderCompile2 failed on {shader.source} ({compiled.returncode})")
        if struct.unpack_from("<iii", vcs.read_bytes()) != (6, shader.combos, shader.dynamic):  # version, combos, dynamic combos
            raise ToolError(f"{shader.name}.vcs doesn't have the stock combo layout")
        shutil.copy2(vcs, shaders.OUT)


def build_launcher(vs: Path) -> Path:
    """HalfCraft.exe, a release's launcher (source\\launcher), with halfcraft's version."""
    source = REPO / "source/launcher"
    out = REPO / "build/launcher"
    out.mkdir(parents=True, exist_ok=True)
    text = version.version()
    numbers = ",".join(map(str, version.numbers(text)))
    (out / "version.h").write_bytes(f'#define HC_VERSION {numbers}\r\n#define HC_VERSION_TEXT "{text}"\r\n'.encode("ascii"))
    cl = (
        f'cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /I "{REPO}\\source\\src" /I "{REPO}\\protocol" /Fo"{out}\\\\" '
        f'/Fe"{out}\\HalfCraft.exe" halfcraft_launcher.cpp "{REPO}\\source\\src\\core\\hc_prism.cpp" "{out}\\halfcraft.res" '
        "/link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib advapi32.lib comctl32.lib || exit /b 1"
    )
    lines = [f'cd /d "{source}"', f'rc /nologo /I "{out}" /fo "{out}\\halfcraft.res" halfcraft.rc || exit /b 1', cl]
    visual_studio.run_script(vs, out / "build.cmd", lines, timeout=COMPILE_SECONDS, what="launcher build")
    return out / "HalfCraft.exe"


def build(engines: list[Engine], *, projects: bool = True, launcher: bool = True, configuration: str = "Release") -> None:
    """builds the engines' dlls, the shaders, their dev game folders and (launcher) HalfCraft.exe.
    projects: run vpc first (skip it when no .vpc file changed).
    """
    if running := windows.processes(*(engine.exe for engine in ENGINES.values())):
        raise ToolError(f"close the game first ({', '.join(process.name for process in running)} runs): its dlls get replaced")
    chain = toolchain(configuration)
    LOGS.mkdir(parents=True, exist_ok=True)
    # the shaders and the game folders' campaign files come from source-sdk-2013 whichever engine is built:
    # missing, it should stop the build before anything compiles, not after
    sdk.TREES["hl2dm"].source()
    for engine in engines:
        (build_hl2 if engine.name == "hl2" else build_hl2dm)(chain, projects=projects)
    build_shaders()

    # the dev game folders: laid out like a release's, plus the symbols
    for engine in engines:
        game_folder.copy_game_folder(engine, engine.game, symbols=True)
        print(f"built {engine.name} with {chain.toolset}; game folder: {engine.game}")
    if launcher:
        print(f"launcher: {build_launcher(chain.vs)}")
