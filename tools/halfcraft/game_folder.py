"""a game folder (what -game points at) for one engine: make build's dev ones in build\\, make package's for a
release, so both look the same.

a game folder holds half-life 2, episode one, episode two and lost coast as one game, as valve's
hl2_complete does for the first three. its layers, later ones win: hl2dm-sp's files of the campaigns
(commentary, hud scripts, fonts, ...; the later campaign's over the earlier's), the chapters of all of them
numbered on (their cfg, picture and title) with the campaigns' strings merged, then source\\mod\\common, then
source\\mod\\<engine> (gameinfo.txt), then the engine's dlls and the world shaders. files a running game
writes (save\\, cfg\\config.cfg, screenshots\\) are left alone.
"""

import codecs
import re
import shutil
from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO, ToolError, commands, folders, shaders
from halfcraft.engines import Engine

SDK = REPO / "source-sdk-2013"  # hl2dm-sp: the campaigns' files, whichever engine
MANIFEST = "halfcraft-files.txt"

# hl2dm-sp's files that only fit half-life 2: deathmatch's engine: its steam.inf (app 320), its gameui option
# layouts (they'd replace half-life 2's own) and copies of strings half-life 2 has itself. the particle
# manifest is the 2025 tree's; half-life 2 brings its own
HL2DM_ONLY = frozenset(
    (
        "steam.inf",
        "resource/optionssubmouse.res",
        "resource/optionssubvideo.res",
        "resource/optionssubvideoadvanceddlg.res",
        "resource/gameui_english.txt",
        "resource/valve_english.txt",
        "particles/particles_manifest.txt",
    )
)
# hl2dm-sp's files a game folder makes its own: the chapters and their titles (numbered on across the
# campaigns), the strings (merged) and the skill manifest (source\mod\common has it: half-life 2's skill.cfg
# on every map, and server.dll adds an episode's skill cfg on its maps). skill.cfg itself comes from
# hl2dm-sp: hl2:dm's engine only runs cfg files in the game folder
GENERATED = re.compile(r"^(gameinfo\.txt|cfg/chapter[0-9a-z]+\.cfg|cfg/skill_manifest\.cfg|resource/mod_[a-z0-9]+_english\.txt)$", re.IGNORECASE)
MOD_TITLE = re.compile(r"^mod_[a-z0-9]+_Chapter[0-9a-z]+_Title$", re.IGNORECASE)
TOKEN = re.compile(r'^\s*"([^"]+)"\s+"')
VALUE = re.compile(r'^\s*"[^"]+"\s+"(.*)"')
QUOTED = re.compile(r'"[^"]+"')
LINE_END = re.compile(rb"\r\n|\r|\n")
PICTURE = """"UnlitGeneric"
{{
\t"$baseTexture" "vgui/{pictures}/chapters/chapter{chapter}"
\t"$vertexalpha" 1
\t"$gammaColorRead" "1"
\t"$linearWrite" "1"
}}
"""


@dataclass(frozen=True)
class Campaign:
    mod: str  # hl2dm-sp's mod folder
    pictures: str  # the folder of its chapter pictures in valve's hl2_complete_misc.vpk (its own vgui\chapters would overwrite the others')
    path_id: str  # its search path id (core/hc_campaign.h), which prefixes its own value of a string the campaigns differ in
    skill: str | None = None  # the name its skill_episodic.cfg gets (server.dll's hc_campaign.cpp runs it on its maps)
    files: re.Pattern[str] | None = None  # which of its files come, if not all


# in play order. lost coast's hud, particle and skill files are half-life 2's from before the episodes: only
# its commentary comes
CAMPAIGNS = (
    Campaign("mod_hl2", "hl2", "hc_hl2"),
    Campaign("mod_ep1", "episodic", "hc_ep1", skill="skill_ep1.cfg"),
    Campaign("mod_ep2", "ep2", "hc_ep2", skill="skill_ep2.cfg"),
    Campaign("mod_lostcoast", "lostcoast", "hc_lc", files=re.compile(r"^maps/", re.IGNORECASE)),
)


def chapter_order(name: str) -> int:
    """a chapter's name ("9a") as a sortable key: its number, then its letter."""
    number, letter = re.match(r"^(\d*)(.*)$", name).groups()
    return int(number or 0) * 100 + (ord(letter[0]) if letter else 0)


def crlf_lines(lines: list[bytes]) -> bytes:
    return b"".join(line + b"\r\n" for line in lines)


def split_lines(data: bytes) -> list[bytes]:
    """a text file's lines: on crlf, lf or cr, no empty last one, without a utf-8 byte order mark."""
    lines = LINE_END.split(data.removeprefix(codecs.BOM_UTF8))
    return lines[:-1] if lines and not lines[-1] else lines


def read_tokens(path: Path) -> dict[str, tuple[str, str]]:
    """the tokens of a "lang" file (utf-16, one token per line, the way valve writes them) in file order, by
    their key in lower case: (the key, the whole line). keys are case-blind, so the first of a key wins.
    """
    tokens: dict[str, tuple[str, str]] = {}
    # read_text turns crlf and cr into lf; splitlines would split on form feeds and the like too
    for line in path.read_text(encoding="utf-16").split("\n"):
        token = TOKEN.match(line)
        if token and token[1].lower() != "language":
            tokens.setdefault(token[1].lower(), (token[1], line))
    return tokens


def value_of(line: str) -> str:
    found = VALUE.match(line)
    return found[1] if found else ""


def renamed(line: str, key: str) -> str:
    """a token's line under another key."""
    return QUOTED.sub(lambda _: f'"{key}"', line, count=1)


def write_campaigns(destination: Path, folder: str) -> None:
    """the chapters of the campaigns as one list (cfg, picture, title), and their strings merged into
    resource\\<game folder>_english.txt: the engine reads a mod's strings from that file and the new game
    dialog's titles from "<game folder>_ChapterN_Title". where the campaigns' strings differ (episode two's
    game over lines), the earlier campaign's value is the string and a later one's is "<its path id>_<key>"
    too, which client.dll's titles.txt messages of that campaign look for first (hc_campaign_text.cpp).
    """
    offset = 0
    titles: list[str] = []
    strings: dict[str, tuple[str, str]] = {}
    variants: list[str] = []
    for campaign in CAMPAIGNS:
        mod = SDK / "game" / campaign.mod
        tokens = read_tokens(mod / f"resource/{campaign.mod}_english.txt")
        chapters = sorted(
            (cfg.stem[len("chapter") :] for cfg in (mod / "cfg").iterdir() if cfg.is_file() and re.fullmatch(r"chapter.*\.cfg", cfg.name, re.IGNORECASE)),
            key=chapter_order,
        )
        last = 0
        for chapter in chapters:
            number = re.match(r"\d*", chapter)[0]
            name = chapter if offset == 0 else f"{int(number) + offset}{chapter[len(number) :]}"
            last = max(last, int(number) + offset)
            # the menu's new game runs the chapter's cfg: hc_new_game first tells halfcraft it's no console `map`
            cfg = (mod / f"cfg/chapter{chapter}.cfg").read_bytes().removeprefix(codecs.BOM_UTF8)
            (destination / f"cfg/chapter{name}.cfg").write_bytes(b"hc_new_game\n" + cfg)
            picture = destination / f"materials/vgui/chapters/chapter{name}.vmt"
            picture.parent.mkdir(parents=True, exist_ok=True)
            picture.write_bytes(PICTURE.format(pictures=campaign.pictures, chapter=chapter).replace("\n", "\r\n").encode("ascii"))
            if title := tokens.get(f"{campaign.mod}_Chapter{chapter}_Title".lower()):
                titles.append(renamed(title[1], f"{folder}_Chapter{name}_Title"))
        offset = last
        for lowered, (key, line) in tokens.items():
            # the mod folders' own chapter titles are written above under this folder's name; the
            # campaigns' (HL2_, episodic_, ep2_) stay: their titles.txt messages show them
            if MOD_TITLE.match(key):
                continue
            if lowered not in strings:
                strings[lowered] = (key, line)
            elif value_of(strings[lowered][1]) != value_of(line):
                variants.append(renamed(line, f"{campaign.path_id}_{key}"))
    lines = ['"lang"', "{", '"Language" "English"', '"Tokens"', "{", *titles, *(line for _, line in strings.values()), *variants, "}", "}"]
    text = "".join(f"{line}\r\n" for line in lines)
    (destination / f"resource/{folder}_english.txt").write_bytes(codecs.BOM_UTF16_LE + text.encode("utf-16-le"))


def campaign_files(engine: Engine, destination: Path) -> None:
    """hl2dm-sp's campaign files (what its git tracks, not what running it left behind), the later campaign's
    over the earlier's: the episodes' hud scripts, fonts and particles cover half-life 2's.
    """
    for campaign in CAMPAIGNS:
        prefix = f"game/{campaign.mod}/"
        for file in commands.git("ls-files", "-z", prefix, repo=SDK).stdout.split("\0"):
            relative = file.removeprefix(prefix)
            if not file or GENERATED.match(relative) or (engine.name != "hl2dm" and relative.lower() in HL2DM_ONLY):
                continue
            if campaign.files and not campaign.files.match(relative):
                continue
            if relative.lower() == "cfg/skill_episodic.cfg" and campaign.skill:
                relative = f"cfg/{campaign.skill}"
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if relative.lower().startswith("cfg/") and relative.lower().endswith(".cfg"):
                # the engine drops the line before a block of '#' comments (skill_episodic.cfg's
                # sk_dmg_take_scale3): the same comments as '//' cost nothing
                lines = split_lines((SDK / file).read_bytes())
                target.write_bytes(crlf_lines([b"//" + line[1:] if line.startswith(b"#") else line for line in lines]))
            else:
                shutil.copy2(SDK / file, target)


def write_game_folder(engine: Engine, destination: Path, folder: str, *, symbols: bool) -> None:
    """lays out a game folder for one engine into an empty destination; folder is the name it will have."""
    campaign_files(engine, destination)
    write_campaigns(destination, folder)
    for layer in ("common", engine.name):
        shutil.copytree(REPO / "source/mod" / layer, destination, dirs_exist_ok=True)

    game_bin = destination / engine.game_bin
    game_bin.mkdir(parents=True, exist_ok=True)
    for dll in engine.dlls():
        if not dll.is_file():
            raise ToolError(f"missing {dll} (build the {engine.name} engine's dlls first)")
        shutil.copy2(dll, game_bin)
        if symbols:
            shutil.copy2(dll.with_suffix(".pdb"), game_bin)

    fxc = destination / "shaders/fxc"
    fxc.mkdir(parents=True, exist_ok=True)
    for shader in shaders.SHADERS:
        if not shader.vcs.is_file():
            raise ToolError(f"missing {shader.vcs} (build the shaders first)")
        shutil.copy2(shader.vcs, fxc)


def copy_game_folder(engine: Engine, destination: Path, *, symbols: bool = False) -> None:
    """lays out a game folder into destination. symbols: the .pdb next to each dll (dev folders:
    tools/debug/read_dump.py finds them there).
    """
    # laid out afresh in build\stage, then synced over: a file halfcraft shipped before and doesn't now
    # (halfcraft-files.txt lists the last layout's) goes, what the game wrote stays
    stage = REPO / f"build/stage/game-{engine.name}"
    folders.remove_tree(stage)
    stage.mkdir(parents=True)
    write_game_folder(engine, stage, destination.name, symbols=symbols)

    files = sorted((str(path.relative_to(stage)) for path in stage.rglob("*") if path.is_file()), key=str.lower)
    manifest = destination / MANIFEST
    if manifest.is_file():
        kept = {file.lower() for file in files}
        for gone in manifest.read_text(encoding="utf-8-sig").splitlines():
            if gone and gone.lower() not in kept and (destination / gone).is_file():
                (destination / gone).unlink()
    shutil.copytree(stage, destination, dirs_exist_ok=True)
    manifest.write_bytes(codecs.BOM_UTF8 + "".join(f"{file}\r\n" for file in files).encode("utf-8"))
