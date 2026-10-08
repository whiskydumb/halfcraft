"""prints where a crashed game died, from its minidump (no debugger needed).

reads the exception and module list from the dump, then names the crashing function (and the likely callers
found on the crashing thread's stack) with dbghelp and the build's .pdb files. works for both engines: 64-bit
hl2mp_win64.exe and 32-bit hl2.exe dumps.

python tools/debug/read_dump.py <dump.dmp> [pdb dir ...]
"""

from __future__ import annotations  # ctypes' pointer types take no subscript at run time

import bisect
import ctypes
import struct
import sys
from ctypes import wintypes as wt
from dataclasses import dataclass, field
from pathlib import Path

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_MEMORY_LIST = 5
STREAM_EXCEPTION = 6
STREAM_SYSTEM_INFO = 7
STREAM_MEMORY64_LIST = 9
MAX_FRAMES = 64
MAX_STACK_SCAN = 25


@dataclass(frozen=True)
class Module:
    base: int
    size: int
    path: str

    def holds(self, address: int) -> bool:
        return self.base <= address < self.base + self.size


@dataclass(frozen=True)
class Dump:
    data: bytes
    streams: dict[int, tuple[int, int]]  # stream type: (rva, size)
    modules: list[Module]
    x86: bool

    def stream(self, kind: int) -> int:
        return self.streams[kind][0]

    def module_of(self, address: int) -> Module | None:
        return next((module for module in self.modules if module.holds(address)), None)


class Address64(ctypes.Structure):
    _fields_ = (("Offset", ctypes.c_uint64), ("Segment", wt.WORD), ("Mode", wt.DWORD))


class StackFrame64(ctypes.Structure):
    _fields_ = (
        ("AddrPC", Address64),
        ("AddrReturn", Address64),
        ("AddrFrame", Address64),
        ("AddrStack", Address64),
        ("AddrBStore", Address64),
        ("FuncTableEntry", ctypes.c_void_p),
        ("Params", ctypes.c_uint64 * 4),
        ("Far", wt.BOOL),
        ("Virtual", wt.BOOL),
        ("Reserved", ctypes.c_uint64 * 3),
        ("KdHelp", ctypes.c_byte * 256),
    )


class SymbolInfo(ctypes.Structure):
    _fields_ = (
        ("SizeOfStruct", wt.ULONG),
        ("TypeIndex", wt.ULONG),
        ("Reserved", ctypes.c_uint64 * 2),
        ("Index", wt.ULONG),
        ("Size", wt.ULONG),
        ("ModBase", ctypes.c_uint64),
        ("Flags", wt.ULONG),
        ("Value", ctypes.c_uint64),
        ("Address", ctypes.c_uint64),
        ("Register", wt.ULONG),
        ("Scope", wt.ULONG),
        ("Tag", wt.ULONG),
        ("NameLen", wt.ULONG),
        ("MaxNameLen", wt.ULONG),
        ("Name", ctypes.c_wchar * 512),
    )


class Line(ctypes.Structure):
    _fields_ = (("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD), ("FileName", wt.LPWSTR), ("Address", ctypes.c_uint64))


ReadMemory = ctypes.WINFUNCTYPE(wt.BOOL, wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD))
FunctionTableAccess = ctypes.WINFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_uint64)
ModuleBase = ctypes.WINFUNCTYPE(ctypes.c_uint64, wt.HANDLE, ctypes.c_uint64)


def read_streams(data: bytes) -> dict[int, tuple[int, int]]:
    signature, _version, count, directory = struct.unpack_from("<IIII", data, 0)
    if signature != 0x504D444D:  # "MDMP"
        raise SystemExit("not a minidump")
    streams = {}
    for i in range(count):
        kind, size, rva = struct.unpack_from("<III", data, directory + i * 12)
        streams[kind] = (rva, size)
    return streams


def is_x86(data: bytes, streams: dict[int, tuple[int, int]]) -> bool:
    """whether the dumped process was 32-bit (PROCESSOR_ARCHITECTURE_INTEL)."""
    if STREAM_SYSTEM_INFO not in streams:
        return False
    return struct.unpack_from("<H", data, streams[STREAM_SYSTEM_INFO][0])[0] == 0


def read_utf16(data: bytes, rva: int) -> str:
    length = struct.unpack_from("<I", data, rva)[0]
    return data[rva + 4 : rva + 4 + length].decode("utf-16-le")


def read_modules(data: bytes, streams: dict[int, tuple[int, int]]) -> list[Module]:
    rva = streams[STREAM_MODULE_LIST][0]
    count = struct.unpack_from("<I", data, rva)[0]
    modules = []
    for i in range(count):
        base, size, _checksum, _stamp, name_rva = struct.unpack_from("<QIIII", data, rva + 4 + i * 108)
        modules.append(Module(base, size, read_utf16(data, name_rva)))
    return modules


def read_dump(path: Path) -> Dump:
    data = path.read_bytes()
    streams = read_streams(data)
    return Dump(data, streams, read_modules(data, streams), is_x86(data, streams))


def read_exception(dump: Dump) -> tuple[int, int, int, tuple[int, ...]]:
    """the crashing thread, the exception code, where it happened and its parameters."""
    rva = dump.stream(STREAM_EXCEPTION)
    thread_id = struct.unpack_from("<I", dump.data, rva)[0]
    code, _flags, _record, address, count = struct.unpack_from("<IIQQI", dump.data, rva + 8)
    params = struct.unpack_from("<15Q", dump.data, rva + 8 + 32)[:count]
    return thread_id, code, address, params


def thread_stack(dump: Dump, thread_id: int) -> bytes:
    rva = dump.stream(STREAM_THREAD_LIST)
    for i in range(struct.unpack_from("<I", dump.data, rva)[0]):
        entry = rva + 4 + i * 48
        if struct.unpack_from("<I", dump.data, entry)[0] == thread_id:
            _start, size, memory_rva = struct.unpack_from("<QII", dump.data, entry + 24)
            return dump.data[memory_rva : memory_rva + size]
    return b""


def memory_ranges(dump: Dump) -> list[tuple[int, bytes]]:
    """(start, bytes) of every memory block the dump captured."""
    data, ranges = dump.data, []
    if STREAM_MEMORY64_LIST in dump.streams:
        rva = dump.stream(STREAM_MEMORY64_LIST)
        count, offset = struct.unpack_from("<QQ", data, rva)
        for i in range(count):
            start, size = struct.unpack_from("<QQ", data, rva + 16 + i * 16)
            ranges.append((start, data[offset : offset + size]))
            offset += size
    if STREAM_MEMORY_LIST in dump.streams:
        rva = dump.stream(STREAM_MEMORY_LIST)
        for i in range(struct.unpack_from("<I", data, rva)[0]):
            start, size, memory_rva = struct.unpack_from("<QII", data, rva + 4 + i * 16)
            ranges.append((start, data[memory_rva : memory_rva + size]))
    return ranges


def thread_context(dump: Dump, thread_id: int) -> bytes | None:
    data = dump.data
    # the exception's own context first: the thread list holds the thread as the dump was written
    if STREAM_EXCEPTION in dump.streams:
        rva = dump.stream(STREAM_EXCEPTION)
        if struct.unpack_from("<I", data, rva)[0] == thread_id:
            size, context_rva = struct.unpack_from("<II", data, rva + 160)
            if size:
                return data[context_rva : context_rva + size]
    rva = dump.stream(STREAM_THREAD_LIST)
    for i in range(struct.unpack_from("<I", data, rva)[0]):
        entry = rva + 4 + i * 48
        if struct.unpack_from("<I", data, entry)[0] == thread_id:
            size, context_rva = struct.unpack_from("<II", data, entry + 40)
            return data[context_rva : context_rva + size]
    return None


def load_image(path: str) -> bytes:
    """a pe file laid out as it is in memory (sections at their virtual addresses)."""
    raw = Path(path).read_bytes()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    sections, optional_size = struct.unpack_from("<H", raw, pe + 6)[0], struct.unpack_from("<H", raw, pe + 20)[0]
    image_size = struct.unpack_from("<I", raw, pe + 24 + 56)[0]
    headers_size = struct.unpack_from("<I", raw, pe + 24 + 60)[0]
    image = bytearray(image_size)
    image[:headers_size] = raw[:headers_size]
    table = pe + 24 + optional_size
    for i in range(sections):
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", raw, table + i * 40 + 8)
        length = min(raw_size, virtual_size or raw_size)
        image[virtual_address : virtual_address + length] = raw[raw_pointer : raw_pointer + length]
    return bytes(image)


@dataclass
class Memory:
    """the dumped process's memory: what the dump captured, and the code and data of its modules' files."""

    dump: Dump
    ranges: list[tuple[int, bytes]]
    images: dict[str, bytes | None] = field(default_factory=dict)  # by module path; None: unreadable

    def module_image(self, address: int) -> tuple[int, bytes | None]:
        """(base, image) of the module an address is in, laid out from its file; (0, None) outside them."""
        module = self.dump.module_of(address)
        if module is None:
            return 0, None
        if module.path not in self.images:
            try:
                self.images[module.path] = load_image(module.path)
            except OSError:
                self.images[module.path] = None
        return module.base, self.images[module.path]

    def read(self, address: int, size: int) -> bytes | None:
        for start, block in self.ranges:
            if start <= address and address + size <= start + len(block):
                return block[address - start : address - start + size]
        base, image = self.module_image(address)
        return image[address - base : address - base + size] if image is not None else None


class X64FunctionTable:
    """x64: a module's own .pdata entry for an address, from its file. dbghelp's SymFunctionTableAccess64 finds
    none without a live process, and the walk stops after the first frame.
    """

    def __init__(self, memory: Memory) -> None:
        self.memory = memory
        self.pdata: dict[int, list[tuple[int, int, int]]] = {}  # module base: (begin, end, unwind info) rvas, sorted
        self.entries: list[ctypes.Array[wt.DWORD]] = []  # the entries handed to dbghelp stay alive here
        self.callback = FunctionTableAccess(self.entry)

    def entry(self, _process: int, address: int) -> int | None:
        base, image = self.memory.module_image(address)
        if image is None:
            return None
        if base not in self.pdata:
            pe = struct.unpack_from("<I", image, 0x3C)[0]
            rva, size = struct.unpack_from("<II", image, pe + 24 + 112 + 3 * 8)  # pe32+ data directory 3: exceptions
            self.pdata[base] = [struct.unpack_from("<III", image, rva + i * 12) for i in range(size // 12)]
        table, target = self.pdata[base], address - base
        index = bisect.bisect_right(table, (target, 0xFFFFFFFF, 0xFFFFFFFF)) - 1
        if index < 0 or not table[index][0] <= target < table[index][1]:
            return None
        entry = (wt.DWORD * 3)(*table[index])
        self.entries.append(entry)
        return ctypes.addressof(entry)


class Symbols:
    def __init__(self, search_path: str | None) -> None:
        self.dbghelp = ctypes.WinDLL("dbghelp.dll")
        self.process = wt.HANDLE(0x1234)  # any unique value: no live process is attached
        self.dbghelp.SymSetOptions(0x2 | 0x10 | 0x4)  # undecorate, load lines, deferred loads
        self.dbghelp.SymInitializeW.argtypes = (wt.HANDLE, wt.LPCWSTR, wt.BOOL)
        if not self.dbghelp.SymInitializeW(self.process, search_path, False):
            raise SystemExit("SymInitialize failed")
        self.dbghelp.SymLoadModuleExW.argtypes = (wt.HANDLE, wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD)
        self.dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
        self.dbghelp.SymFunctionTableAccess64.restype = ctypes.c_void_p
        self.dbghelp.SymFunctionTableAccess64.argtypes = (wt.HANDLE, ctypes.c_uint64)
        self.dbghelp.SymGetModuleBase64.restype = ctypes.c_uint64
        self.dbghelp.SymGetModuleBase64.argtypes = (wt.HANDLE, ctypes.c_uint64)

    def load(self, module: Module) -> None:
        self.dbghelp.SymLoadModuleExW(self.process, None, module.path, None, module.base, module.size, None, 0)

    def name(self, address: int) -> str | None:
        info = SymbolInfo(SizeOfStruct=88, MaxNameLen=512)
        displacement = ctypes.c_uint64()
        if not self.dbghelp.SymFromAddrW(self.process, ctypes.c_uint64(address), ctypes.byref(displacement), ctypes.byref(info)):
            return None
        text = f"{info.Name}+0x{displacement.value:x}"
        line = Line(SizeOfStruct=ctypes.sizeof(Line))
        line_displacement = wt.DWORD()
        if self.dbghelp.SymGetLineFromAddrW64(self.process, ctypes.c_uint64(address), ctypes.byref(line_displacement), ctypes.byref(line)):
            text += f"  ({line.FileName}:{line.LineNumber})"
        return text


def initial_frame(context: bytes, x86: bool) -> tuple[StackFrame64, ctypes.Array[ctypes.c_byte], int, int]:
    """the walk's first frame from the thread's registers, and the context dbghelp walks with: a 16-byte aligned
    copy inside the buffer returned with it (keep it alive), its address and the machine type.
    """
    if x86:
        # x86 CONTEXT: 716 bytes; eip, esp, ebp
        size, machine = 716, 0x014C
        pc, stack, frame_pointer = (struct.unpack_from("<I", context, offset)[0] for offset in (0xB8, 0xC4, 0xB4))
    else:
        # x64 CONTEXT: 1232 bytes, 16-byte aligned (the dump may hold an extended one after it)
        size, machine = 1232, 0x8664
        pc, stack, frame_pointer = (struct.unpack_from("<Q", context, offset)[0] for offset in (0xF8, 0x98, 0xA0))
    buffer = (ctypes.c_byte * (size + 16))()
    aligned = (ctypes.addressof(buffer) + 15) & ~15
    ctypes.memmove(aligned, bytes(context[:size]), size)
    frame = StackFrame64()
    frame.AddrPC.Offset, frame.AddrPC.Mode = pc, 3
    frame.AddrStack.Offset, frame.AddrStack.Mode = stack, 3
    frame.AddrFrame.Offset, frame.AddrFrame.Mode = frame_pointer, 3
    return frame, buffer, aligned, machine


def walk_stack(dump: Dump, thread_id: int, symbols: Symbols) -> list[int]:
    """unwinds the crashing thread with dbghelp's StackWalk64 over the dump's memory."""
    context = thread_context(dump, thread_id)
    if not context:
        return []
    memory = Memory(dump, memory_ranges(dump))

    @ReadMemory
    def read_memory(_process: int, address: int, buffer: int, size: int, read_count: ctypes._Pointer[wt.DWORD]) -> bool:
        chunk = memory.read(address, size)
        if not chunk:
            return False
        ctypes.memmove(buffer, chunk, len(chunk))
        if read_count:
            read_count[0] = len(chunk)
        return True

    dbghelp = symbols.dbghelp
    x64_table = X64FunctionTable(memory)
    table_access = FunctionTableAccess(dbghelp.SymFunctionTableAccess64) if dump.x86 else x64_table.callback
    module_base = ModuleBase(dbghelp.SymGetModuleBase64)
    frame, _buffer, aligned, machine = initial_frame(context, dump.x86)
    frames = []
    for _ in range(MAX_FRAMES):
        walked = dbghelp.StackWalk64(
            machine, symbols.process, wt.HANDLE(0), ctypes.byref(frame), ctypes.c_void_p(aligned), read_memory, table_access, module_base, None
        )
        if not walked or frame.AddrPC.Offset == 0:
            break
        frames.append(frame.AddrPC.Offset)
    return frames


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    dump = read_dump(Path(sys.argv[1]))
    thread_id, code, address, params = read_exception(dump)
    pointer, digits = (4, 8) if dump.x86 else (8, 16)
    pdb_dirs = sys.argv[2:]
    symbols = Symbols(";".join(pdb_dirs) if pdb_dirs else None)
    for module in dump.modules:
        symbols.load(module)  # every module: unwinding needs their function tables

    def describe(where: int) -> str:
        module = dump.module_of(where)
        place = f"{Path(module.path).name}+0x{where - module.base:x}" if module else "?"
        name = symbols.name(where) if module else None
        return f"0x{where:0{digits}x} {place}" + (f"  {name}" if name else "")

    print(f"exception 0x{code:08x} on thread {thread_id}")
    if code == 0xC0000005 and len(params) >= 2:
        print(f"  {'write' if params[0] == 1 else 'read'} of address 0x{params[1]:x}")
    print("  at " + describe(address))

    print("call stack:")
    for frame in walk_stack(dump, thread_id, symbols):
        print("  " + describe(frame))

    # return addresses into our own modules, top of the stack first
    print("stack scan (may include stale frames):")
    stack = thread_stack(dump, thread_id)
    ours_names = {pdb.stem.lower() for folder in pdb_dirs for pdb in Path(folder).glob("*.pdb")}
    ours = [module for module in dump.modules if Path(module.path).stem.lower() in ours_names]
    shown = 0
    for offset in range(0, len(stack) - pointer + 1, pointer):
        value = struct.unpack_from("<I" if dump.x86 else "<Q", stack, offset)[0]
        if any(module.holds(value) for module in ours):
            text = describe(value)
            if "+0x0" not in text.split("  ")[-1]:
                print("  stack " + text)
                shown += 1
                if shown >= MAX_STACK_SCAN:
                    break


if __name__ == "__main__":
    main()
