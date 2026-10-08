"""writes the sdk trees' edits to valve's code back into source\\sdk\\halfcraft-<engine>.patch. run it after
changing anything inside source-sdk-2013\\src or source-sdk-2013-sp\\sp\\src; tools/setup_sdk.ps1 applies them.

    python tools/update_patches.py                 both
    python tools/update_patches.py --engine hl2    source-sdk-2013-sp only
"""

import argparse

from halfcraft import commands, sdk


def main() -> None:
    parser = argparse.ArgumentParser(description="write the sdk trees' edits into halfcraft's patches")
    parser.add_argument("--engine", choices=["all", "hl2", "hl2dm"], default="all")
    options = parser.parse_args()

    for tree in sdk.trees(options.engine):
        # --output keeps the bytes as git writes them; its line-ending notes are switched off
        diff = ["-c", "core.safecrlf=false", "diff", "--binary", f"--output={tree.patch}", "--", *tree.edited()]
        commands.git(*diff, repo=tree.path, timeout=300, what=f"git diff in {tree.folder}")
        files = sum(1 for line in tree.patch.read_bytes().splitlines() if line.startswith(b"diff --git"))
        print(f"{tree.patch} ({files} files)")


if __name__ == "__main__":
    main()
