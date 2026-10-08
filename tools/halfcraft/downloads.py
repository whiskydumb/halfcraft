"""downloads pinned by their hash: fetched once, checked every time."""

import hashlib
import time
import urllib.request
from pathlib import Path

from halfcraft import ToolError

SECONDS = 600
CHUNK = 1 << 20


def fetch(url: str, path: Path, algorithm: str, digest: str) -> None:
    """downloads url to path unless it's there already, then checks it against its pinned hash (sha256,
    sha512); a file that doesn't match goes.
    """
    if not url.startswith("https://"):
        raise ToolError(f"{url} isn't an https url")
    if not path.is_file():
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_name(f"{path.name}.part")
        deadline = time.monotonic() + SECONDS
        request = urllib.request.Request(url, headers={"User-Agent": "halfcraft-tools"})  # noqa: S310 https only, above
        try:
            with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as file:  # noqa: S310 https only, above
                while chunk := response.read(CHUNK):
                    if time.monotonic() > deadline:
                        raise ToolError(f"downloading {url} took over {SECONDS} s")
                    file.write(chunk)
        except OSError as error:
            raise ToolError(f"downloading {url} failed: {error}") from error
        partial.replace(path)
    with path.open("rb") as file:
        actual = hashlib.file_digest(file, algorithm).hexdigest()
    if actual != digest.lower():
        path.unlink()
        raise ToolError(f"{url} doesn't match its pinned {algorithm} hash (got {actual})")
