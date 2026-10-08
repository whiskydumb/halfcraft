"""builds both halves of halfcraft and packs a release into dist\\:

HalfCraft-<version>.zip       unpack anywhere, start HalfCraft.exe (package\\README.txt says the rest)
HalfCraft-<version>-pdb.zip   both engines' client.dll and server.dll debug symbols (hl2\\, hl2dm\\), for crash dumps

python tools/build/package.py [--no-build]

the zip's HalfCraft folder: HalfCraft.exe, game-hl2\\ and game-hl2dm\\ (the game folders of the two engines,
tools/halfcraft/game_folder.py), minecraft\\ (portable Prism Launcher with the HalfCraft instance and its mods,
from package\\minecraft), README.txt, LICENSE.txt, THIRD-PARTY-NOTICES.md.

building needs what tools/build/build_hl2.py needs plus JDK 25 (tools/halfcraft/gradle.py finds it). Prism Launcher
and Fabric API are downloaded once into .cache\\package and checked against the hashes pinned below.
"""

import argparse
import re
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, build, downloads, engines, folders, game_folder, gradle, shaders
from halfcraft.version import version

PRISM_VERSION = "11.1.1"
PRISM_ZIP = f"PrismLauncher-Windows-MSVC-Portable-{PRISM_VERSION}.zip"
PRISM_URL = f"https://github.com/PrismLauncher/PrismLauncher/releases/download/{PRISM_VERSION}/{PRISM_ZIP}"
PRISM_SHA256 = "ab35a770fb06d89d2ccc098079db5db329fb4e68f42b72babd8b095efde3d2d7"
PRISM_LICENSE_URL = f"https://raw.githubusercontent.com/PrismLauncher/PrismLauncher/{PRISM_VERSION}/LICENSE"
PRISM_LICENSE_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"
FABRIC_API_VERSION = "0.161.0+26.3"
FABRIC_API_URL = "https://cdn.modrinth.com/data/P7dR8mSH/versions/bNnaTiuM/fabric-api-0.161.0%2B26.3.jar"
FABRIC_API_SHA512 = "ed6b2586d6fde11fde8472f5a527c51e99b67026e46f94d4bfd85e7e28ce5ee299173ee16ad576ceb51f39f98d30a811086a6deb1a86a524859cc16e12da109d"
CACHE = REPO / ".cache/package"
DIST = REPO / "dist"


def gradle_property(name: str) -> str:
    properties = (gradle.MINECRAFT / "gradle.properties").read_text(encoding="utf-8")
    found = re.search(rf"(?m)^{re.escape(name)}=(.+)$", properties)
    if not found:
        raise ToolError(f"no {name} in minecraft\\gradle.properties")
    return found[1].strip()


def write_zip(path: Path, entries: dict[str, Path]) -> None:
    """entries named with forward slashes, as the zip format expects."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, file in entries.items():
            archive.write(file, name)


def folder_entries(folder: Path, prefix: str) -> dict[str, Path]:
    files = sorted((file for file in folder.rglob("*") if file.is_file()), key=lambda file: str(file).lower())
    return {prefix + file.relative_to(folder).as_posix(): file for file in files}


def copy_text(source: Path, target: Path, values: dict[str, str] | None = None) -> None:
    """text the player opens in notepad: crlf, placeholders ({KEY}) filled in."""
    text = source.read_text(encoding="utf-8-sig")
    for key, value in (values or {}).items():
        text = text.replace(f"{{{key}}}", value)
    target.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser(description="pack a halfcraft release into dist\\")
    parser.add_argument("--no-build", action="store_true", help="pack what's built")
    options = parser.parse_args()

    if gradle_property("fabric_api_version") != FABRIC_API_VERSION:
        raise ToolError(
            f"the mod builds against Fabric API {gradle_property('fabric_api_version')} but {FABRIC_API_VERSION} is pinned here: pin the new one's download and hash"
        )
    text = version()
    both = engines.chosen("all")
    if not options.no_build:
        gradle.run(["build"])
        build.build(both)

    jar = gradle.MINECRAFT / f"build/libs/halfcraft-{text}.jar"
    launcher = REPO / "build/launcher/HalfCraft.exe"
    for file in [jar, launcher, *(dll for engine in both for dll in engine.dlls()), *(shader.vcs for shader in shaders.SHADERS)]:
        if not file.is_file():
            raise ToolError(f"missing {file} (build first, or drop --no-build)")

    prism_zip = CACHE / PRISM_ZIP
    prism_license = CACHE / f"PrismLauncher-{PRISM_VERSION}-LICENSE.txt"
    fabric_api = CACHE / f"fabric-api-{FABRIC_API_VERSION}.jar"
    downloads.fetch(PRISM_URL, prism_zip, "sha256", PRISM_SHA256)
    downloads.fetch(PRISM_LICENSE_URL, prism_license, "sha256", PRISM_LICENSE_SHA256)
    downloads.fetch(FABRIC_API_URL, fabric_api, "sha512", FABRIC_API_SHA512)

    stage = DIST / "HalfCraft"
    DIST.mkdir(exist_ok=True)
    for old in DIST.glob("HalfCraft-*.zip"):
        old.unlink()
    folders.remove_tree(stage)
    stage.mkdir()

    # game-hl2\ and game-hl2dm\: one game folder per engine (their saves don't mix), laid out like the dev ones
    for engine in both:
        game_folder.copy_game_folder(engine, stage / f"game-{engine.name}")

    # minecraft\: portable prism with the halfcraft instance and its mods (names without versions, so
    # unpacking a new release over an old one replaces them)
    minecraft = stage / "minecraft"
    shutil.copytree(REPO / "package/minecraft", minecraft)
    with zipfile.ZipFile(prism_zip) as archive:
        archive.extractall(minecraft / "Prism")
    if not (minecraft / "Prism/prismlauncher.exe").is_file():
        raise ToolError(f"{PRISM_ZIP} isn't laid out the way it used to be: no prismlauncher.exe at its top")
    shutil.copy2(prism_license, minecraft / "Prism/LICENSE-PrismLauncher.txt")
    third_party = {"PRISM_VERSION": PRISM_VERSION, "FABRIC_API_VERSION": FABRIC_API_VERSION}
    copy_text(REPO / "package/minecraft/Prism/THIRD-PARTY.txt", minecraft / "Prism/THIRD-PARTY.txt", third_party)
    mods = minecraft / "Prism/instances/HalfCraft/.minecraft/mods"
    mods.mkdir(parents=True, exist_ok=True)
    shutil.copy2(jar, mods / "halfcraft.jar")
    shutil.copy2(fabric_api, mods / "fabric-api.jar")

    shutil.copy2(launcher, stage)
    copy_text(REPO / "package/README.txt", stage / "README.txt", {"VERSION": text})
    copy_text(REPO / "LICENSE", stage / "LICENSE.txt")
    copy_text(REPO / "THIRD-PARTY-NOTICES.md", stage / "THIRD-PARTY-NOTICES.md")

    write_zip(DIST / f"HalfCraft-{text}.zip", folder_entries(stage, "HalfCraft/"))
    symbols = {f"{engine.name}/{dll.with_suffix('.pdb').name}": dll.with_suffix(".pdb") for engine in both for dll in engine.dlls()}
    write_zip(DIST / f"HalfCraft-{text}-pdb.zip", symbols)
    folders.remove_tree(stage)

    for archive in sorted(DIST.glob("HalfCraft-*.zip")):
        print(f"{archive.name:<32} {archive.stat().st_size / 2**20:8,.1f} MB")


if __name__ == "__main__":
    main()
