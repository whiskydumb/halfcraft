"""checks out the source sdk 2013 trees halfcraft builds against (tools/halfcraft/sdk.py pins them) and applies
halfcraft's edits to valve's code (source\\sdk\\halfcraft-<engine>.patch, written by tools/build/update_patches.py).

python tools/build/setup_sdk.py                   both
python tools/build/setup_sdk.py --engine hl2dm    source-sdk-2013 only. --engine hl2 sets up both all the same: the hl2
                                            build takes the world shaders and the campaign files (chapters,
                                            strings) from source-sdk-2013 too

afterwards: make build
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools\, where the halfcraft package is
from halfcraft import ToolError, commands, sdk

FETCH_SECONDS = 3600
CHECKOUT_SECONDS = 600


def clone(tree: sdk.Tree) -> None:
    """just the pinned commit, without history."""
    commands.run(["git", "init", "--quiet", tree.path], timeout=60, what=f"git init in {tree.path}")
    commands.git("config", "core.longpaths", "true", repo=tree.path)
    commands.git("remote", "add", "origin", tree.url, repo=tree.path)
    if tree.sparse:
        commands.git("sparse-checkout", "set", tree.sparse, repo=tree.path)
    fetch = ["fetch", "--quiet", "--depth", "1", "origin", tree.commit]
    commands.git(*fetch, repo=tree.path, timeout=FETCH_SECONDS, what=f"fetching {tree.commit} from {tree.url}")


def applies(tree: sdk.Tree, *how: str) -> bool:
    """whether the patch would apply (or, with --reverse, is applied already)."""
    return commands.git("apply", *how, "--check", tree.patch, repo=tree.path, check=False).returncode == 0


def setup(tree: sdk.Tree) -> None:
    if not (tree.path / ".git").exists():  # a tree from before keeps its full clone
        clone(tree)
    commands.git("checkout", "--quiet", "--detach", tree.commit, repo=tree.path, timeout=CHECKOUT_SECONDS, what=f"checkout of {tree.commit} in {tree.path}")
    if applies(tree):
        commands.git("apply", "--whitespace=nowarn", tree.patch, repo=tree.path, timeout=CHECKOUT_SECONDS, what=f"applying {tree.patch}")
        print(f"{tree.engine} sdk ready at {tree.path}")
    elif applies(tree, "--reverse"):
        print(f"{tree.engine} sdk already patched at {tree.path}")
    else:
        raise ToolError(f"{tree.patch} neither applies nor is already applied; see git status in {tree.path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="check out the sdk trees and apply halfcraft's patches")
    parser.add_argument("--engine", choices=["all", *sdk.TREES], default="all")
    options = parser.parse_args()
    for tree in sdk.trees("hl2dm" if options.engine == "hl2dm" else "all"):
        setup(tree)


if __name__ == "__main__":
    main()
