"""keeps test sessions away from the player's own games: half-life's saves and settings, and minecraft's world.

python tools/game/test_session.py start [--engine hl2dm]   puts build\\game-<engine>\\save and cfg\\config.cfg aside
                                                      (build\\test-session\\<engine>): the test starts without saves
python tools/game/test_session.py stop [--engine hl2dm]    puts them back; the test's own saves and settings go to
                                                      build\\test-session\\<engine>-last
python tools/game/test_session.py status                   what's set aside, and which games run
python tools/game/test_session.py minecraft                the minecraft test client (make mc-test): its own run
                                                      folder (minecraft\\run\\test) and world (HalfCraftTest)
python tools/game/test_session.py dev-client               the player's minecraft dev client (make mc-run)

half-life's saves carry checkpoints of the player's minecraft world, and every changelevel autosaves over them,
so a test never plays with them. the game folder itself stays: its strings file is named after it
(resource\\<folder>_english.txt). only one minecraft can hold the link, so neither client starts next to another
one. the test client only starts while every engine has a test session on, the player's only while none has,
and a session neither starts nor stops while a minecraft runs: otherwise one world plays against the other's
saves (it follows their loads and rolls back, and their autosaves land on the wrong saves). --engine all (the
default) does both engines.
"""

import argparse
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import REPO, ToolError, engines, folders, gradle, windows

SESSIONS = REPO / "build/test-session"
# what minecraft (HostLink.announceRunning) holds while it runs: the dev client, the test client or a release's
MINECRAFT_MUTEX = "Local\\HalfCraft_v1_minecraft"
PLAYER_LOG = REPO / "minecraft/run/logs/latest.log"
TEST_LOG = REPO / "minecraft/run/test/logs/latest.log"


@dataclass(frozen=True)
class Session:
    """where one engine's saves and settings are, and where a test session puts them."""

    engine: engines.Engine

    @property
    def save(self) -> Path:
        return self.engine.game / "save"

    @property
    def config(self) -> Path:
        return self.engine.game / "cfg/config.cfg"

    @property
    def keep(self) -> Path:  # the player's, while a session is on
        return SESSIONS / self.engine.name

    @property
    def last(self) -> Path:  # the last session's own
        return SESSIONS / f"{self.engine.name}-last"

    @property
    def notes(self) -> Path:  # when it started, what it set aside, and whether stop parked the test's
        return self.keep / "session.txt"

    def since(self) -> str:
        return self.notes.read_text(encoding="ascii").splitlines()[0]

    def note(self, *lines: str) -> None:
        with self.notes.open("a", encoding="ascii", newline="\r\n") as notes:
            notes.writelines(f"{line}\n" for line in lines)


def minecraft_client() -> str:
    """ "none", "test" (make mc-test's), "player" (the dev client in minecraft\\run) or "other" (a release's). a
    running minecraft keeps its latest.log open for writing: that tells which run folder it plays from.
    """
    if not windows.mutex_exists(MINECRAFT_MUTEX):
        return "none"
    if windows.in_use(TEST_LOG):
        return "test"
    if windows.in_use(PLAYER_LOG):
        return "player"
    return "other"


def ensure_game_stopped() -> None:
    if running := engines.running():
        raise ToolError(f"{running[1].name} runs: quit it first (it writes save\\ and cfg\\config.cfg)")


def ensure_minecraft_stopped(why: str) -> None:
    if (client := minecraft_client()) != "none":
        raise ToolError(f"the {client} minecraft runs: {why}")


def with_session(sessions: list[Session], on: bool) -> list[str]:
    """the engines (with a game folder) that have a test session on, or haven't."""
    return [session.engine.name for session in sessions if session.engine.game.is_dir() and session.keep.is_dir() == on]


def start(sessions: list[Session]) -> None:
    ensure_game_stopped()
    ensure_minecraft_stopped("its world would follow the test's loads. quit it, start the session, then the test client (make mc-test)")
    todo = []
    for session in sessions:
        if not session.engine.game.is_dir():
            print(f"{session.engine.name}: no game folder ({session.engine.game}); skipped")
        elif session.keep.is_dir():
            raise ToolError(f"{session.engine.name}: a test session is already on (since {session.since()}); stop it first")
        else:
            todo.append(session)
    for session in todo:
        session.keep.mkdir(parents=True)
        session.note(datetime.now().strftime("%Y-%m-%dT%H:%M:%S"))
        had_save, had_config = session.save.is_dir(), session.config.is_file()
        if had_save:
            shutil.move(session.save, session.keep / "save")
        if had_config:
            shutil.copy2(session.config, session.keep / "config.cfg")
        session.note(f"save={had_save}", f"config={had_config}")
        print(f"{session.engine.name}: test session on; the player's saves and settings are in {session.keep}")


def stop(sessions: list[Session]) -> None:
    ensure_game_stopped()
    ensure_minecraft_stopped("quit it first: with the player's saves back, its world would play against them")
    for session in sessions:
        name = session.engine.name
        if not session.keep.is_dir():
            print(f"{name}: no test session")
            continue
        # the test's own saves and settings go aside once. "parked" says they went, so a stop run again after
        # one that broke off never takes the player's (already back) for the test's
        parked = session.notes.is_file() and "parked" in (line.lower() for line in session.notes.read_text(encoding="ascii").splitlines())
        if not parked:
            folders.remove_tree(session.last)
            session.last.mkdir()
            if session.save.exists():
                shutil.move(session.save, session.last / "save")
            if session.config.exists():
                shutil.move(session.config, session.last / "config.cfg")
            session.note("parked")
        # whatever is still in the session folder is the player's: it goes back, never over anything
        for kept, back in ((session.keep / "save", session.save), (session.keep / "config.cfg", session.config)):
            if not kept.exists():
                continue
            if back.exists():
                raise ToolError(f"{name}: {back} came back since the last stop; move it away and stop again (the player's is in {kept})")
            shutil.move(kept, back)
        session.notes.unlink()
        session.keep.rmdir()
        print(f"{name}: test session off; the player's saves and settings are back, the test's are in {session.last}")


def status(sessions: list[Session]) -> None:
    for session in sessions:
        print(f"{session.engine.name}: test session on since {session.since()}" if session.keep.is_dir() else f"{session.engine.name}: no test session")
    running = engines.running()
    print(f"half-life: {f'{running[1].name} runs' if running else 'not running'}")
    print(f"minecraft: {minecraft_client()}")


def test_client(sessions: list[Session]) -> None:
    ensure_minecraft_stopped("two would fight over the one link")
    if missing := with_session(sessions, on=False):
        raise ToolError(f"no test session for {', '.join(missing)}: the test world would autosave over the player's saves. make test-start first")
    gradle.run(["runTestClient"])


def dev_client(sessions: list[Session]) -> None:
    ensure_minecraft_stopped("two would fight over the one link")
    if on := with_session(sessions, on=True):
        raise ToolError(f"a test session is on for {', '.join(on)}: the player's world would play against the test's saves. make test-stop first")
    gradle.run(["runClient"])


ACTIONS = {"start": start, "stop": stop, "status": status, "minecraft": test_client, "dev-client": dev_client}


def main() -> None:
    parser = argparse.ArgumentParser(description="keep test sessions away from the player's own games")
    parser.add_argument("action", choices=list(ACTIONS))
    parser.add_argument("--engine", choices=["all", *engines.ENGINES], default="all")
    options = parser.parse_args()
    ACTIONS[options.action]([Session(engine) for engine in engines.chosen(options.engine)])


if __name__ == "__main__":
    main()
