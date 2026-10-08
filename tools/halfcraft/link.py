"""the running game's shared memory for the debug tools (tools\\debug), read-only: the mapping, laid out as
protocol\\schema says, and what its collision ring still holds."""

import mmap
import struct
from dataclasses import dataclass

from halfcraft import ToolError
from halfcraft.protocol import Schema

MESSAGE_HEADER = struct.Struct("<II")  # ColMsgHeader: type, payload bytes


def open_link(schema: Schema, name: str | None = None) -> mmap.mmap:
    """the mapping (kMappingName, or `name`) read-only: an error when no game has it up, or one built from
    another schema does."""
    name = name or str(schema.constants["kMappingName"].value)
    try:
        # a name nobody holds gets a new, empty mapping: the magic tells
        link = mmap.mmap(-1, schema.mapping_bytes, tagname=name, access=mmap.ACCESS_READ)
    except OSError as error:
        raise ToolError(f"can't map {name} ({error}): a game built from another schema, with another size, has it up?") from error
    header = schema.structs["Header"].read(link, schema.regions["Header"].at)
    if header["magic"] != schema.value("kMagic"):
        link.close()
        raise ToolError(f"no link in shared memory ({name}): is the game running?")
    if header["layout"] != schema.layout:
        link.close()
        raise ToolError(
            f"the game runs with protocol layout {header['layout']:#010x}, protocol/schema here is {schema.layout:#010x}: "
            "read it with the tools of the commit it was built from"
        )
    return link


@dataclass(frozen=True)
class Message:
    type: int  # ColType
    at: int  # its payload, in the mapping
    size: int  # the payload's bytes


class CollisionRing:
    """the collision ring as it is now. the host writes it a lap at a time from the ring's start (a message that
    won't fit before the end leaves a kColPad there), so a lap's start is a message's: the messages since the
    last one read from there, and what the lap before still holds from the first message that leads on to the
    lap's end. older ones are gone (minecraft still has them)."""

    def __init__(self, link: mmap.mmap, schema: Schema) -> None:
        ring = schema.regions["CollisionRing"].at
        self.link = link
        self.data = ring + int(schema.value("kColRingDataOff"))
        self.size = int(schema.value("kColRingDataBytes"))
        self.head = struct.unpack_from("<Q", link, ring + int(schema.value("kColRingHeadOff")))[0]
        self.tail = struct.unpack_from("<Q", link, ring + int(schema.value("kColRingTailOff")))[0]
        self.pad, self.clear, self.region, self.tris = (int(schema.value(name)) for name in ("kColPad", "kColClear", "kColRegion", "kColTris"))
        region = schema.structs["ColRegion"]
        self.region_bytes, self.count_at = region.size, region.field("count").offset
        self.item_bytes = {self.region: schema.structs["ColBlock"].size, self.tris: schema.structs["ColTri"].size}
        self.kept_from = 0  # where what it holds starts (head - kept_from bytes of messages)

    def plausible(self, offset: int) -> int | None:
        """the bytes the message at `offset` in the ring takes up to the next one, when its header makes sense."""
        if offset + MESSAGE_HEADER.size > self.size:
            return None
        kind, size = MESSAGE_HEADER.unpack_from(self.link, self.data + offset)
        if kind == self.pad:
            return self.size - offset if size == 0 else None
        if offset + MESSAGE_HEADER.size + size > self.size:
            return None
        if kind == self.clear:
            fits = size == 4
        elif kind in self.item_bytes:
            count = struct.unpack_from("<I", self.link, self.data + offset + MESSAGE_HEADER.size + self.count_at)[0] if size >= self.region_bytes else -1
            fits = size == self.region_bytes + count * self.item_bytes[kind]
        else:
            fits = False
        return (MESSAGE_HEADER.size + size + 7) & ~7 if fits else None

    def walk(self, start: int, end: int) -> list[Message] | None:
        """the messages from `start` to `end` (bytes the host wrote in all), or None unless they lead there."""
        found: list[Message] = []
        position = start
        while position < end:
            offset = position % self.size
            step = self.plausible(offset)
            if step is None:
                return None
            kind, size = MESSAGE_HEADER.unpack_from(self.link, self.data + offset)
            if kind != self.pad:
                found.append(Message(kind, self.data + offset + MESSAGE_HEADER.size, size))
            position += step
        return found if position == end else None

    def messages(self) -> list[Message]:
        """what it holds, oldest first."""
        lap = self.head - self.head % self.size
        current = self.walk(lap, self.head)
        if current is None:
            raise ToolError(f"the collision ring doesn't read from its lap's start ({lap}) to its head ({self.head}): did the host write while it was read?")
        self.kept_from = lap
        if lap == 0:
            return current
        # the lap before, from where this one's writing ends: its first message that leads on to its end
        for start in range((self.head - self.size + 7) & ~7, lap, 8):
            if self.plausible(start % self.size) is not None and (older := self.walk(start, lap)) is not None:
                self.kept_from = start
                return older + current
        return current

    def epoch(self, message: Message) -> int:
        """a kColClear's."""
        return struct.unpack_from("<I", self.link, message.at)[0]
