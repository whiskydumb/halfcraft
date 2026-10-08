"""halfcraft's shared memory protocol: protocol\\schema\\*.toml read into its layout. tools/build/protocol.py
writes protocol\\halfcraft_protocol.h and minecraft's Proto.java from it (make proto); the debug tools read the
mapping with it.

    schema = protocol.load()
    schema.regions["HostState"].at                    0x100
    schema.structs["HostState"].read(link, 0x100)     {"seq": 7, "flags": 1, "posX": 12.5, ...}
    schema.value("kColRingDataOff")                   0x80

a schema file is one stretch of the mapping, a section of the generated files:

    title = "host -> MC state"                        its heading
    doc = "..."                                       what's there (every doc becomes a comment in both files)

    [regions.HostState]                               kOffHostState, OFF_HOST_STATE: at = 0x100, or after =
    at = 0x100                                        "OtherRegion"; what it holds: type = a struct (count =
    type = "HostState"                                that many in a row), holds = [{ type, at }] more inside it;
                                                      bytes = what it reserves (kHostStateBytes), else the
                                                      type's size; reserved = true: nothing there yet
    [constants]
    kWaterGridSize = { type = "u32", value = 16 }     a number, an expression ("kMaxOverlayW * kMaxOverlayH
                                                      * 4"), a string for a wstring; or bit = n for 1 << n.
                                                      u64s are written in hex, others when hex = true
    [enums.HostFlags]                                 type, doc, note (a comment after the items)
    [enums.HostFlags.items]
    kHostInGame = { bit = 0, doc = "..." }            value = n (or just n), or bit = n

    [structs.HostState]                               doc, note, size (checked); seqlock = true: its first
                                                      field, seq, is a seqlock's (the code around a read
                                                      or a write handles it); java_class = "class" or
                                                      "record": ProtoStructs.java has it, to read and write
                                                      whole (java_implements: interfaces a record takes on)
    [structs.HostState.fields]                        in order, laid out the way c lays them out
    seq = "u32"
    "posX, posY, posZ" = { type = "f64", doc = "..." }
    surface = "f32[kWaterGridSize * kWaterGridSize]"

types: u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 char, a struct's name, and arrays of them. a struct has no
gaps: padding is a field of its own (named pad*, *Pad or reserved*, which java and the debug tools skip).
a doc with a line break is a comment above what it's about, one without trails it. java's names come from
c++'s (kHostInGame: HOST_IN_GAME; HostState's posX: the struct's prefix, HS_POS_X), or java = "..." (a list
for a line of fields) where they can't.
"""

import ast
import hashlib
import itertools
import json
import math
import operator
import re
import struct
import tomllib
from collections.abc import Buffer, Callable
from dataclasses import dataclass, field
from pathlib import Path

from halfcraft import REPO, ToolError

SCHEMA = REPO / "protocol" / "schema"


@dataclass(frozen=True)
class Scalar:
    size: int
    cpp: str
    java: str
    code: str  # the struct module's

    @property
    def signed(self) -> bool:
        return self.code in "bhiq"

    @property
    def floating(self) -> bool:
        return self.code in "fd"


SCALARS = {
    "u8": Scalar(1, "std::uint8_t", "int", "B"),
    "i8": Scalar(1, "std::int8_t", "int", "b"),
    "char": Scalar(1, "char", "int", "c"),
    "u16": Scalar(2, "std::uint16_t", "int", "H"),
    "i16": Scalar(2, "std::int16_t", "int", "h"),
    "u32": Scalar(4, "std::uint32_t", "int", "I"),
    "i32": Scalar(4, "std::int32_t", "int", "i"),
    "u64": Scalar(8, "std::uint64_t", "long", "Q"),
    "i64": Scalar(8, "std::int64_t", "long", "q"),
    "f32": Scalar(4, "float", "float", "f"),
    "f64": Scalar(8, "double", "double", "d"),
}
WSTRING = "wstring"
NUMBERS = [name for name in SCALARS if name != "char"]
WHOLE = [name for name in NUMBERS if not SCALARS[name].floating]
NAME = re.compile(r"[A-Za-z_]\w*")
PADDING = re.compile(r"^(pad|reserved)|Pad$")
TYPE = re.compile(r"^\s*(\w+)\s*((?:\[[^\[\]]+\]\s*)*)$")
DIMENSION = re.compile(r"\[([^\[\]]+)\]")
BINARY: dict[type, Callable[[int, int], int]] = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.floordiv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}
UNARY: dict[type, Callable[[int], int]] = {ast.USub: operator.neg, ast.UAdd: operator.pos, ast.Invert: operator.invert}
FILE_KEYS = {"title", "doc", "note", "regions", "constants", "enums", "structs"}
REGION_KEYS = {"at", "after", "type", "count", "bytes", "holds", "reserved", "doc"}
HOLD_KEYS = {"type", "at"}
CONSTANT_KEYS = {"type", "value", "bit", "hex", "doc", "java"}
ENUM_KEYS = {"type", "doc", "note", "items"}
ITEM_KEYS = {"value", "bit", "doc", "java"}
STRUCT_KEYS = {"doc", "note", "size", "java", "java_size", "seqlock", "java_class", "java_implements", "fields"}
FIELD_KEYS = {"type", "doc", "java"}


def upper_snake(name: str) -> str:
    """java's spelling of a c++ name: posX is POS_X, OverlaySlotHdr OVERLAY_SLOT_HDR, Smg1 SMG1."""
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).upper()


def java_constant(name: str) -> str:
    """kHostInGame: HOST_IN_GAME."""
    return upper_snake(name.removeprefix("k"))


@dataclass
class Constant:
    name: str
    type: str
    value: int | float | str
    expression: str  # what the schema wrote, when not a plain number
    bit: int | None
    hex: bool
    doc: str
    java: str


@dataclass
class Item:
    name: str
    value: int
    bit: int | None
    doc: str
    java: str


@dataclass
class Enum:
    name: str
    type: str
    items: list[Item]
    doc: str
    note: str


@dataclass
class Field:
    name: str
    type: str  # a scalar's name or a struct's
    struct: "Struct | None"
    dimensions: tuple[int, ...]
    written: tuple[str, ...]  # the dimensions as the schema wrote them: c++ keeps those
    offset: int
    size: int
    line: int  # the fields of one schema line (posX, posY, posZ) share it
    doc: str
    java: str

    @property
    def padding(self) -> bool:
        return bool(PADDING.search(self.name))

    @property
    def element(self) -> int:
        return self.struct.size if self.struct else SCALARS[self.type].size

    def read(self, buffer: Buffer, at: int) -> object:
        """its value at `at`: a number, a dict for a struct, a str for chars, lists for an array."""
        if self.type == "char":
            raw = bytes(memoryview(buffer)[at : at + self.size])
            return raw.split(b"\0", 1)[0].decode("utf-8", "replace")
        return self._read(buffer, at, self.dimensions)

    def _read(self, buffer: Buffer, at: int, dimensions: tuple[int, ...]) -> object:
        if not dimensions:
            return self.struct.read(buffer, at) if self.struct else struct.unpack_from("<" + SCALARS[self.type].code, buffer, at)[0]
        if len(dimensions) == 1 and not self.struct:
            return list(struct.unpack_from(f"<{dimensions[0]}{SCALARS[self.type].code}", buffer, at))
        stride = self.element * math.prod(dimensions[1:])
        return [self._read(buffer, at + i * stride, dimensions[1:]) for i in range(dimensions[0])]


@dataclass
class Struct:
    name: str
    fields: list[Field]
    size: int
    align: int
    doc: str
    note: str
    java: str  # the prefix of its fields' java names
    java_size: str
    seqlock: bool  # its first field, seq, is a seqlock's
    java_class: str  # "class" or "record" when ProtoStructs.java has it, else ""
    java_implements: list[str]

    def field(self, name: str) -> Field:
        found = next((each for each in self.fields if each.name == name), None)
        if found is None:
            raise ToolError(f"{self.name} has no field {name}")
        return found

    def read(self, buffer: Buffer, at: int = 0) -> dict[str, object]:
        """its fields at `at` in `buffer` (bytes, an mmap), by name; padding left out."""
        return {each.name: each.read(buffer, at + each.offset) for each in self.fields if not each.padding}


@dataclass
class Hold:
    type: Struct
    at: int  # from the region's start


@dataclass
class Region:
    name: str
    at: int
    extent: int  # the bytes it covers
    bytes: int | None  # what it reserves, when the schema says
    expression: str  # the schema's bytes, when an expression
    type: Struct | None
    count: int
    holds: list[Hold]
    reserved: bool
    doc: str

    @property
    def end(self) -> int:
        return self.at + self.extent


@dataclass
class Section:
    name: str  # the schema file's stem
    title: str
    doc: str
    note: str
    constants: list[Constant]
    enums: list[Enum]
    structs: list[Struct]
    regions: list[Region]

    @property
    def at(self) -> int:
        """where its first region starts: the generated files go in that order."""
        return min((region.at for region in self.regions), default=1 << 64)


@dataclass
class Schema:
    sections: list[Section]
    constants: dict[str, Constant]
    enums: dict[str, Enum]
    items: dict[str, Item]
    structs: dict[str, Struct]
    regions: dict[str, Region]

    @property
    def mapping_bytes(self) -> int:
        return max(region.end for region in self.regions.values())

    @property
    def layout(self) -> int:
        """a hash of everything but the docs and java's names: two games link only when theirs agree."""
        canonical = {
            "constants": {c.name: [c.type, c.value] for c in self.constants.values()},
            "enums": {e.name: [e.type, [[i.name, i.value] for i in e.items]] for e in self.enums.values()},
            "structs": {s.name: [s.size, [[f.name, f.type, list(f.dimensions), f.offset] for f in s.fields]] for s in self.structs.values()},
            "regions": {
                r.name: [r.at, r.extent, r.type.name if r.type else None, r.count, [[h.type.name, h.at] for h in r.holds], r.reserved]
                for r in self.regions.values()
            },
        }
        text = json.dumps(canonical, sort_keys=True, separators=(",", ":"))
        return int.from_bytes(hashlib.sha256(text.encode("utf-8")).digest()[:4], "big")

    def value(self, name: str) -> int | float:
        """a constant's, an enum item's or a region's (kOffHostState, kHostDebugBytes) number."""
        if name in self.constants and not isinstance(self.constants[name].value, str):
            return self.constants[name].value
        if name in self.items:
            return self.items[name].value
        for region in self.regions.values():
            if name == f"kOff{region.name}" and not region.reserved:
                return region.at
            if name == f"k{region.name}Bytes" and region.bytes is not None and not region.reserved:
                return region.bytes
        raise ToolError(f"the protocol has no number named {name}")


def _table(where: str, value: object, allowed: set[str]) -> dict:
    if not isinstance(value, dict):
        raise ToolError(f"{where}: expected a table, got {value!r}")
    if unknown := sorted(set(value) - allowed):
        raise ToolError(f"{where}: unknown {', '.join(unknown)} (allowed: {', '.join(sorted(allowed))})")
    return value


def _text(where: str, value: object) -> str:
    if not isinstance(value, str):
        raise ToolError(f"{where}: expected a string, got {value!r}")
    return value


@dataclass
class _File:
    name: str
    title: str
    doc: str
    note: str
    constants: list[str] = field(default_factory=list)
    enums: list[str] = field(default_factory=list)
    structs: list[str] = field(default_factory=list)
    regions: list[str] = field(default_factory=list)


class _Loader:
    """reads the schema's files, then works its names out as they're asked for (one may use another)."""

    def __init__(self) -> None:
        self.files: list[_File] = []
        self.defined: dict[str, str] = {}  # c++ name: where
        self.constant_specs: dict[str, tuple[str, dict]] = {}
        self.item_specs: dict[str, tuple[str, str, object]] = {}  # item: where, its enum, spec
        self.enum_specs: dict[str, tuple[str, dict]] = {}
        self.struct_specs: dict[str, tuple[str, dict]] = {}
        self.region_specs: dict[str, tuple[str, dict]] = {}
        self.constants: dict[str, Constant] = {}
        self.items: dict[str, Item] = {}
        self.structs: dict[str, Struct] = {}
        self.regions: dict[str, Region] = {}
        self.working: list[str] = []

    def define(self, name: str, where: str) -> None:
        if not NAME.fullmatch(name):
            raise ToolError(f"{where}: {name!r} isn't a c++ name")
        if name in self.defined:
            raise ToolError(f"{where}: {name} is defined twice (also {self.defined[name]})")
        self.defined[name] = where

    def add(self, path: Path) -> None:
        where = path.relative_to(REPO).as_posix() if path.is_relative_to(REPO) else str(path)
        try:
            data = _table(where, tomllib.loads(path.read_text(encoding="utf-8")), FILE_KEYS)
        except tomllib.TOMLDecodeError as error:
            raise ToolError(f"{where}: {error}") from error
        title, doc, note = (_text(f"{where}: {key}", data.get(key, "")) for key in ("title", "doc", "note"))
        file = _File(path.stem, title or path.stem, doc, note)
        self.files.append(file)
        for name, spec in _table(f"{where}: constants", data.get("constants", {}), set(data.get("constants", {}))).items():
            self.define(name, f"{where}: constants")
            if not re.fullmatch(r"k[A-Z0-9]\w*", name):
                raise ToolError(f"{where}: constant {name} isn't named k<Name>")
            self.constant_specs[name] = (f"{where}: constants.{name}", _table(f"{where}: constants.{name}", spec, CONSTANT_KEYS))
            file.constants.append(name)
        for name, spec in _table(f"{where}: enums", data.get("enums", {}), set(data.get("enums", {}))).items():
            here = f"{where}: enums.{name}"
            self.define(name, here)
            self.enum_specs[name] = (here, _table(here, spec, ENUM_KEYS))
            items = spec.get("items")
            if not isinstance(items, dict) or not items:
                raise ToolError(f"{here} has no items")
            for item, item_spec in items.items():
                self.define(item, here)
                self.item_specs[item] = (f"{here}.items.{item}", name, item_spec)
            file.enums.append(name)
        for name, spec in _table(f"{where}: structs", data.get("structs", {}), set(data.get("structs", {}))).items():
            self.define(name, f"{where}: structs")
            self.struct_specs[name] = (f"{where}: structs.{name}", _table(f"{where}: structs.{name}", spec, STRUCT_KEYS))
            file.structs.append(name)
        for name, spec in _table(f"{where}: regions", data.get("regions", {}), set(data.get("regions", {}))).items():
            here = f"{where}: regions.{name}"
            self.region_specs[name] = (here, _table(here, spec, REGION_KEYS))
            if not spec.get("reserved"):
                self.define(f"kOff{name}", here)
                if "bytes" in spec:
                    self.define(f"k{name}Bytes", here)
            file.regions.append(name)

    def enter(self, name: str, where: str) -> None:
        if name in self.working:
            raise ToolError(f"{where}: {name} depends on itself ({' -> '.join([*self.working, name])})")
        self.working.append(name)

    # ---- numbers

    def evaluate(self, text: str, where: str) -> int | float:
        try:
            tree = ast.parse(text.strip(), mode="eval")
        except SyntaxError as error:
            raise ToolError(f"{where}: {text!r} isn't an expression ({error.msg})") from error
        return self.node(tree.body, text, where)

    def node(self, node: ast.expr, text: str, where: str) -> int | float:
        match node:
            case ast.Constant(value=int() | float() as number) if not isinstance(number, bool):
                return number
            case ast.Name(id=name):
                return self.value(name, where)
            case ast.Call(func=ast.Name(id="sizeof"), args=[ast.Name(id=name)], keywords=[]):
                return self.struct(name, where).size
            case ast.BinOp(left=left, op=op, right=right) if type(op) in BINARY:
                return BINARY[type(op)](self.whole(left, text, where), self.whole(right, text, where))
            case ast.UnaryOp(op=op, operand=operand) if type(op) in UNARY:
                return UNARY[type(op)](self.whole(operand, text, where))
        raise ToolError(f"{where}: {text!r}: an expression takes numbers, the schema's names, sizeof(Struct) and + - * / % << >> | & ^ ~")

    def whole(self, node: ast.expr, text: str, where: str) -> int:
        result = self.node(node, text, where)
        if not isinstance(result, int):
            raise ToolError(f"{where}: {text!r}: arithmetic takes whole numbers")
        return result

    def number(self, value: object, where: str) -> int:
        """a whole number the schema gave as a number or an expression."""
        result = self.evaluate(value, where) if isinstance(value, str) else value
        if not isinstance(result, int) or isinstance(result, bool):
            raise ToolError(f"{where}: {value!r} isn't a whole number")
        return result

    def value(self, name: str, where: str) -> int | float:
        if name in self.constant_specs:
            constant = self.constant(name)
            if isinstance(constant.value, str):
                raise ToolError(f"{where}: {name} is a string, not a number")
            return constant.value
        if name in self.item_specs:
            return self.item(name).value
        region = re.fullmatch(r"kOff(\w+)", name) or re.fullmatch(r"k(\w+)Bytes", name)
        if region and region[1] in self.region_specs:
            found = self.region(region[1])
            if name.startswith("kOff"):
                return found.at
            if found.bytes is not None:
                return found.bytes
        raise ToolError(f"{where}: no constant, enum item or region named {name}")

    @staticmethod
    def fit(value: object, kind: str, where: str) -> int | float:
        """the value as a `kind` holds it: an error when it can't."""
        scalar = SCALARS[kind]
        if isinstance(value, bool) or not isinstance(value, int | float):
            raise ToolError(f"{where}: {value!r} isn't a number")
        if scalar.floating:
            return float(value)
        if not isinstance(value, int):
            raise ToolError(f"{where}: {value!r} isn't a whole number, as a {kind} needs")
        bits = scalar.size * 8
        if not scalar.signed and value < 0:
            value %= 1 << bits  # ~0 is all ones, as in c
        low, high = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if scalar.signed else (0, (1 << bits) - 1)
        if not low <= value <= high:
            raise ToolError(f"{where}: {value} doesn't fit a {kind}")
        return value

    def constant(self, name: str) -> Constant:
        if name in self.constants:
            return self.constants[name]
        where, spec = self.constant_specs[name]
        kind = _text(f"{where}: type", spec.get("type"))
        if kind != WSTRING and kind not in NUMBERS:
            raise ToolError(f"{where}: type {kind!r} isn't one of {', '.join(NUMBERS)}, {WSTRING}")
        if ("value" in spec) == ("bit" in spec):
            raise ToolError(f"{where}: give value or bit")
        self.enter(name, where)
        raw = spec.get("value")
        expression = raw if isinstance(raw, str) and kind != WSTRING else ""
        bit = self.number(spec["bit"], where) if "bit" in spec else None
        if kind == WSTRING:
            value: int | float | str = _text(f"{where}: value", raw)
        else:
            value = self.fit(1 << bit if bit is not None else self.evaluate(expression, where) if expression else raw, kind, where)
        self.working.pop()
        made = Constant(name, kind, value, expression, bit, bool(spec.get("hex")), spec.get("doc", ""), spec.get("java", java_constant(name)))
        self.constants[name] = made
        return made

    def item(self, name: str) -> Item:
        if name in self.items:
            return self.items[name]
        where, enum, spec = self.item_specs[name]
        kind = self.enum_specs[enum][1].get("type")
        if kind not in WHOLE:
            raise ToolError(f"{self.enum_specs[enum][0]}: type {kind!r} isn't one of {', '.join(WHOLE)}")
        spec = _table(where, spec if isinstance(spec, dict) else {"value": spec}, ITEM_KEYS)
        if ("value" in spec) == ("bit" in spec):
            raise ToolError(f"{where}: give value or bit")
        self.enter(name, where)
        bit = self.number(spec["bit"], where) if "bit" in spec else None
        value = int(self.fit(1 << bit if bit is not None else self.number(spec["value"], where), kind, where))
        self.working.pop()
        made = Item(name, value, bit, spec.get("doc", ""), spec.get("java", java_constant(name)))
        self.items[name] = made
        return made

    # ---- structs

    def struct(self, name: str, where: str) -> Struct:
        if name in self.structs:
            return self.structs[name]
        if name not in self.struct_specs:
            raise ToolError(f"{where}: no struct named {name}")
        here, spec = self.struct_specs[name]
        lines = spec.get("fields")
        if not isinstance(lines, dict) or not lines:
            raise ToolError(f"{here} has no fields")
        self.enter(name, here)
        prefix = spec.get("java", upper_snake(name))
        fields: list[Field] = []
        offset, align = 0, 1
        for line, (names_text, raw) in enumerate(lines.items()):
            at = f"{here}.fields.{names_text}"
            line_spec = _table(at, raw if isinstance(raw, dict) else {"type": raw}, FIELD_KEYS)
            names = [each.strip() for each in names_text.split(",")]
            java = line_spec.get("java", [f"{prefix}_{upper_snake(each)}" for each in names])
            java = [java] if isinstance(java, str) else java
            if len(java) != len(names):
                raise ToolError(f"{at}: {len(java)} java names for {len(names)} fields")
            parsed = TYPE.match(_text(f"{at}: type", line_spec.get("type")))
            if not parsed:
                raise ToolError(f"{at}: {line_spec['type']!r} isn't a type (u32, f32[3], ActorRecord[kMaxActors], ...)")
            base, written = parsed[1], tuple(each.strip() for each in DIMENSION.findall(parsed[2]))
            inner = None if base in SCALARS else self.struct(base, at)
            dimensions = tuple(self.number(each, at) for each in written)
            if any(each <= 0 for each in dimensions):
                raise ToolError(f"{at}: an array needs a length above 0")
            element, element_align = (inner.size, inner.align) if inner else (SCALARS[base].size, SCALARS[base].size)
            for each, java_name in zip(names, java, strict=True):
                if not NAME.fullmatch(each) or any(other.name == each for other in fields):
                    raise ToolError(f"{at}: {each!r} isn't a field name, or {name} has it twice")
                if offset % element_align:
                    raise ToolError(f"{at}: c would leave {element_align - offset % element_align} bytes unnamed before {each} at {offset:#x}: add a pad field")
                size = element * math.prod(dimensions)
                fields.append(Field(each, base, inner, dimensions, written, offset, size, line, line_spec.get("doc", ""), java_name))
                offset += size
                align = max(align, element_align)
        if offset % align:
            raise ToolError(f"{here}: c would pad it from {offset:#x} to {offset + align - offset % align:#x}: add a pad field at its end")
        if "size" in spec and spec["size"] != offset:
            raise ToolError(f"{here}: its fields take {offset:#x} bytes, not the {spec['size']:#x} its size says")
        self.working.pop()
        java_size = spec.get("java_size", f"{prefix}_BYTES")
        made = Struct(name, fields, offset, align, spec.get("doc", ""), spec.get("note", ""), prefix, java_size, *self.uses(here, spec, fields))
        self.structs[name] = made
        return made

    @staticmethod
    def uses(here: str, spec: dict, fields: list[Field]) -> tuple[bool, str, list[str]]:
        """a struct's seqlock, java_class and java_implements, checked."""
        seqlock, java_class = bool(spec.get("seqlock")), spec.get("java_class", "")
        if seqlock and (fields[0].name != "seq" or fields[0].type != "u32" or fields[0].dimensions):
            raise ToolError(f"{here}: a seqlock's struct starts with u32 seq")
        if java_class not in ("", "class", "record"):
            raise ToolError(f"{here}: java_class is class or record, not {java_class!r}")
        if spec.get("java_implements") and java_class != "record":
            raise ToolError(f"{here}: java_implements is for a record")
        return seqlock, java_class, list(spec.get("java_implements", []))

    # ---- regions

    def region(self, name: str) -> Region:
        if name in self.regions:
            return self.regions[name]
        where, spec = self.region_specs[name]
        if ("at" in spec) == ("after" in spec):
            raise ToolError(f"{where}: give at or after")
        self.enter(f"region {name}", where)
        at = self.place(where, spec)
        kind = self.struct(_text(f"{where}: type", spec["type"]), where) if "type" in spec else None
        count = self.number(spec.get("count", 1), where)
        if count < 1 or ("count" in spec and kind is None):
            raise ToolError(f"{where}: count needs a type, and 1 or more")
        holds = []
        for raw in spec.get("holds", []):
            hold = _table(f"{where}: holds", raw, HOLD_KEYS)
            holds.append(Hold(self.struct(_text(f"{where}: holds", hold.get("type")), where), self.number(hold.get("at"), where)))
        expression = spec["bytes"] if isinstance(spec.get("bytes"), str) else ""
        reserves = self.number(spec["bytes"], where) if "bytes" in spec else None
        reserved = bool(spec.get("reserved"))
        if reserved and (kind or holds or reserves is None):
            raise ToolError(f"{where}: a reserved region has bytes and holds nothing")
        if kind is None and reserves is None:
            raise ToolError(f"{where}: give its type or its bytes")
        used = [(0, kind.size * count, kind)] if kind else []
        used += [(hold.at, hold.type.size, hold.type) for hold in holds]
        extent = reserves if reserves is not None else used[0][1]
        self.fits(where, at, extent, used)
        self.working.pop()
        made = Region(name, at, extent, reserves, expression, kind, count, holds, reserved, spec.get("doc", ""))
        self.regions[name] = made
        return made

    def place(self, where: str, spec: dict) -> int:
        """where a region starts: its at, or the end of the region it comes after."""
        if "at" in spec:
            return self.number(spec["at"], where)
        after = _text(f"{where}: after", spec["after"])
        if after not in self.region_specs:
            raise ToolError(f"{where}: no region named {after}")
        return self.region(after).end

    @staticmethod
    def fits(where: str, at: int, extent: int, used: list[tuple[int, int, Struct]]) -> None:
        """the structs a region holds (start in it, size, struct) stay inside it, aligned, apart."""
        used = sorted(used, key=lambda each: each[0])
        for start, size, inner in used:
            if start + size > extent:
                raise ToolError(f"{where}: {inner.name} at {start:#x} takes {size:#x} bytes, past its {extent:#x}")
            if (at + start) % inner.align:
                raise ToolError(f"{where}: {inner.name} at {at + start:#x} isn't {inner.align}-byte aligned")
        for (start, size, inner), (next_start, _, next_inner) in itertools.pairwise(used):
            if start + size > next_start:
                raise ToolError(f"{where}: {inner.name} at {start:#x} runs into {next_inner.name} at {next_start:#x}")

    # ---- the whole

    def enum(self, name: str) -> Enum:
        where, spec = self.enum_specs[name]
        items = [self.item(item) for item in spec["items"]]
        by_value = sorted(items, key=lambda item: item.value)
        for first, second in itertools.pairwise(by_value):
            if first.value == second.value:
                raise ToolError(f"{where}: {first.name} and {second.name} are both {first.value}")
        return Enum(name, spec["type"], items, spec.get("doc", ""), spec.get("note", ""))

    def finish(self) -> Schema:
        constants = {name: self.constant(name) for name in self.constant_specs}
        enums = {name: self.enum(name) for name in self.enum_specs}
        structs = {name: self.struct(name, self.struct_specs[name][0]) for name in self.struct_specs}
        regions = {name: self.region(name) for name in self.region_specs}
        ordered = sorted(regions.values(), key=lambda region: region.at)
        for first, second in itertools.pairwise(ordered):
            if first.end > second.at:
                raise ToolError(f"region {first.name} ({first.at:#x}-{first.end - 1:#x}) runs into {second.name} at {second.at:#x}")
        sections = [
            Section(
                file.name,
                file.title,
                file.doc,
                file.note,
                [constants[name] for name in file.constants],
                [enums[name] for name in file.enums],
                [structs[name] for name in file.structs],
                [regions[name] for name in file.regions],
            )
            for file in self.files
        ]
        sections.sort(key=lambda section: (section.at, section.name))
        return Schema(sections, constants, enums, self.items, structs, regions)


def load(folder: Path = SCHEMA) -> Schema:
    """the protocol, from the schema's files in `folder`."""
    files = sorted(folder.glob("*.toml"))
    if not files:
        raise ToolError(f"no schema files (*.toml) in {folder}")
    loader = _Loader()
    for path in files:
        loader.add(path)
    return loader.finish()
