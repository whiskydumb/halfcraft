"""other programs: every one runs with a timeout, and one that runs over goes with everything it started."""

import subprocess
from collections.abc import Mapping, Sequence
from pathlib import Path

from halfcraft import REPO, ToolError


def run(
    command: Sequence[str | Path],
    *,
    timeout: float | None,
    what: str | None = None,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    """runs a command and waits for it. timeout=None only for what a player ends (a minecraft client).

    what names it in errors (the program's name by default). check: a non-zero exit is an error.
    capture: stdout and stderr come back as text instead of going to the console.
    """
    what = what or Path(command[0]).name
    pipe = subprocess.PIPE if capture else None
    try:
        process = subprocess.Popen([str(part) for part in command], cwd=cwd, env=env, stdout=pipe, stderr=pipe, text=True, encoding="utf-8", errors="replace")
    except FileNotFoundError:
        raise ToolError(f"{what}: {command[0]} not found") from None
    with process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            kill_tree(process.pid)
            process.communicate()
            raise ToolError(f"{what} took over {timeout:g} s") from None
    if check and process.returncode != 0:
        said = f": {stderr.strip()}" if capture and stderr.strip() else ""
        raise ToolError(f"{what} failed ({process.returncode}){said}")
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)


def git(*arguments: str | Path, repo: Path = REPO, timeout: float = 60, what: str | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    """git in a repo (halfcraft's by default), its output captured."""
    return run(["git", "-C", repo, *arguments], timeout=timeout, what=what or f"git {arguments[0]}", check=check, capture=True)


def kill_tree(pid: int) -> None:
    """ends a process and every process it started (a .bat's cmd.exe leaves its java behind otherwise)."""
    subprocess.run(["taskkill", "/T", "/F", "/PID", str(pid)], capture_output=True, timeout=30, check=False)
