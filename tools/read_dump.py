"""Prints where a crashed game died, from its minidump (no debugger needed).

Reads the exception and module list from the dump, then names the crashing function (and the
likely callers found on the crashing thread's stack) with dbghelp and the build's .pdb files.

    python tools/read_dump.py <dump.dmp> [pdb dir ...]
"""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from pathlib import Path

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_EXCEPTION = 6


def read_streams(data):
    signature, _version, count, directory = struct.unpack_from("<IIII", data, 0)
    if signature != 0x504D444D:  # "MDMP"
        raise SystemExit("not a minidump")
    streams = {}
    for i in range(count):
        kind, size, rva = struct.unpack_from("<III", data, directory + i * 12)
        streams[kind] = (rva, size)
    return streams


def read_utf16(data, rva):
    length = struct.unpack_from("<I", data, rva)[0]
    return data[rva + 4 : rva + 4 + length].decode("utf-16-le")


def read_modules(data, streams):
    rva, _ = streams[STREAM_MODULE_LIST]
    count = struct.unpack_from("<I", data, rva)[0]
    modules = []
    for i in range(count):
        base, size, _checksum, _stamp, name_rva = struct.unpack_from("<QIIII", data, rva + 4 + i * 108)
        modules.append((base, size, read_utf16(data, name_rva)))
    return modules


def read_exception(data, streams):
    rva, _ = streams[STREAM_EXCEPTION]
    thread_id = struct.unpack_from("<I", data, rva)[0]
    code, flags, _record, address, nparams = struct.unpack_from("<IIQQI", data, rva + 8)
    params = struct.unpack_from("<15Q", data, rva + 8 + 32)[:nparams]
    return thread_id, code, address, params


def thread_stack(data, streams, thread_id):
    rva, _ = streams[STREAM_THREAD_LIST]
    count = struct.unpack_from("<I", data, rva)[0]
    for i in range(count):
        entry = rva + 4 + i * 48
        tid = struct.unpack_from("<I", data, entry)[0]
        if tid != thread_id:
            continue
        start, size, mem_rva = struct.unpack_from("<QII", data, entry + 24)
        return start, data[mem_rva : mem_rva + size]
    return None, b""


STREAM_MEMORY_LIST = 5
STREAM_MEMORY64_LIST = 9


def memory_ranges(data, streams):
    """[(start, bytes)] of every memory block the dump captured."""
    ranges = []
    if STREAM_MEMORY64_LIST in streams:
        rva, _ = streams[STREAM_MEMORY64_LIST]
        count, base_rva = struct.unpack_from("<QQ", data, rva)
        offset = base_rva
        for i in range(count):
            start, size = struct.unpack_from("<QQ", data, rva + 16 + i * 16)
            ranges.append((start, data[offset : offset + size]))
            offset += size
    if STREAM_MEMORY_LIST in streams:
        rva, _ = streams[STREAM_MEMORY_LIST]
        count = struct.unpack_from("<I", data, rva)[0]
        for i in range(count):
            start, size, mem_rva = struct.unpack_from("<QII", data, rva + 4 + i * 16)
            ranges.append((start, data[mem_rva : mem_rva + size]))
    return ranges


def thread_context(data, streams, thread_id):
    # the exception's own context first: the thread list holds the thread as the dump was written
    if STREAM_EXCEPTION in streams:
        rva, _ = streams[STREAM_EXCEPTION]
        if struct.unpack_from("<I", data, rva)[0] == thread_id:
            size, ctx_rva = struct.unpack_from("<II", data, rva + 160)
            if size:
                return data[ctx_rva : ctx_rva + size]
    rva, _ = streams[STREAM_THREAD_LIST]
    count = struct.unpack_from("<I", data, rva)[0]
    for i in range(count):
        entry = rva + 4 + i * 48
        if struct.unpack_from("<I", data, entry)[0] == thread_id:
            size, ctx_rva = struct.unpack_from("<II", data, entry + 40)
            return data[ctx_rva : ctx_rva + size]
    return None


def walk_stack(data, streams, thread_id, symbols, modules):
    """Unwinds the crashing thread with dbghelp's StackWalk64 over the dump's memory."""
    ctx_bytes = thread_context(data, streams, thread_id)
    if not ctx_bytes:
        return []
    ranges = memory_ranges(data, streams)
    images = {}

    def read(address, size):
        for start, block in ranges:
            if start <= address and address + size <= start + len(block):
                return block[address - start : address - start + size]
        for base, length, name in modules:  # code and data straight from the module files
            if base <= address < base + length:
                if name not in images:
                    try:
                        images[name] = load_image(name)
                    except OSError:
                        images[name] = None
                image = images[name]
                if image is not None:
                    offset = address - base
                    return image[offset : offset + size]
        return None

    # x64 CONTEXT: 1232 bytes, 16-byte aligned (the dump may hold an extended one after it)
    raw_ctx = (ctypes.c_byte * (1232 + 16))()
    aligned = (ctypes.addressof(raw_ctx) + 15) & ~15
    ctypes.memmove(aligned, bytes(ctx_bytes[:1232]), 1232)
    ctx = ctypes.c_void_p(aligned)
    rip, rsp, rbp = struct.unpack_from("<Q", ctx_bytes, 0xF8)[0], struct.unpack_from("<Q", ctx_bytes, 0x98)[0], struct.unpack_from("<Q", ctx_bytes, 0xA0)[0]

    class Address64(ctypes.Structure):
        _fields_ = [("Offset", ctypes.c_uint64), ("Segment", wt.WORD), ("Mode", wt.DWORD)]

    class StackFrame64(ctypes.Structure):
        _fields_ = [("AddrPC", Address64), ("AddrReturn", Address64), ("AddrFrame", Address64), ("AddrStack", Address64),
                    ("AddrBStore", Address64), ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_uint64 * 4), ("Far", wt.BOOL),
                    ("Virtual", wt.BOOL), ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", ctypes.c_byte * 256)]

    READ = ctypes.WINFUNCTYPE(wt.BOOL, wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD))

    @READ
    def read_memory(_process, address, buffer, size, read_count):
        chunk = read(address, size)
        if not chunk:
            return False
        ctypes.memmove(buffer, chunk, len(chunk))
        if read_count:
            read_count[0] = len(chunk)
        return True

    dbghelp = symbols.dbghelp
    dbghelp.SymFunctionTableAccess64.restype = ctypes.c_void_p
    dbghelp.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    dbghelp.SymGetModuleBase64.restype = ctypes.c_uint64
    dbghelp.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    table_access = ctypes.WINFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_uint64)(dbghelp.SymFunctionTableAccess64)
    module_base = ctypes.WINFUNCTYPE(ctypes.c_uint64, wt.HANDLE, ctypes.c_uint64)(dbghelp.SymGetModuleBase64)

    frame = StackFrame64()
    frame.AddrPC.Offset, frame.AddrPC.Mode = rip, 3
    frame.AddrStack.Offset, frame.AddrStack.Mode = rsp, 3
    frame.AddrFrame.Offset, frame.AddrFrame.Mode = rbp, 3
    frames = []
    for _ in range(64):
        if not dbghelp.StackWalk64(0x8664, symbols.process, wt.HANDLE(0), ctypes.byref(frame), ctx, read_memory, table_access, module_base, None):
            break
        if frame.AddrPC.Offset == 0:
            break
        frames.append(frame.AddrPC.Offset)
    return frames


def load_image(path):
    """A pe file laid out as it is in memory (sections at their virtual addresses)."""
    raw = Path(path).read_bytes()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    sections, opt_size = struct.unpack_from("<H", raw, pe + 6)[0], struct.unpack_from("<H", raw, pe + 20)[0]
    image_size = struct.unpack_from("<I", raw, pe + 24 + 56)[0]
    headers_size = struct.unpack_from("<I", raw, pe + 24 + 60)[0]
    image = bytearray(image_size)
    image[:headers_size] = raw[:headers_size]
    table = pe + 24 + opt_size
    for i in range(sections):
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", raw, table + i * 40 + 8)
        image[vaddr : vaddr + min(rsize, vsize or rsize)] = raw[rptr : rptr + min(rsize, vsize or rsize)]
    return bytes(image)


class Symbols:
    def __init__(self, search_path):
        self.dbghelp = ctypes.WinDLL("dbghelp.dll")
        self.process = wt.HANDLE(0x1234)  # any unique value: no live process is attached
        self.dbghelp.SymSetOptions(0x2 | 0x10 | 0x4)  # undecorate, load lines, deferred loads
        self.dbghelp.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
        if not self.dbghelp.SymInitializeW(self.process, search_path, False):
            raise SystemExit("SymInitialize failed")
        self.dbghelp.SymLoadModuleExW.argtypes = [wt.HANDLE, wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD]
        self.dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64

    def load(self, path, base, size):
        self.dbghelp.SymLoadModuleExW(self.process, None, path, None, base, size, None, 0)

    def name(self, address):
        class SymbolInfo(ctypes.Structure):
            _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", wt.ULONG),
                        ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG), ("Value", ctypes.c_uint64),
                        ("Address", ctypes.c_uint64), ("Register", wt.ULONG), ("Scope", wt.ULONG), ("Tag", wt.ULONG),
                        ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG), ("Name", ctypes.c_wchar * 512)]

        class Line(ctypes.Structure):
            _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD), ("FileName", wt.LPWSTR), ("Address", ctypes.c_uint64)]

        info = SymbolInfo()
        info.SizeOfStruct = 88
        info.MaxNameLen = 512
        displacement = ctypes.c_uint64()
        if not self.dbghelp.SymFromAddrW(self.process, ctypes.c_uint64(address), ctypes.byref(displacement), ctypes.byref(info)):
            return None
        text = f"{info.Name}+0x{displacement.value:x}"
        line = Line()
        line.SizeOfStruct = ctypes.sizeof(Line)
        line_disp = wt.DWORD()
        if self.dbghelp.SymGetLineFromAddrW64(self.process, ctypes.c_uint64(address), ctypes.byref(line_disp), ctypes.byref(line)):
            text += f"  ({line.FileName}:{line.LineNumber})"
        return text


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    data = Path(sys.argv[1]).read_bytes()
    streams = read_streams(data)
    modules = read_modules(data, streams)
    thread_id, code, address, params = read_exception(data, streams)

    def module_of(addr):
        for base, size, name in modules:
            if base <= addr < base + size:
                return base, size, name
        return None

    pdb_dirs = sys.argv[2:]
    symbols = Symbols(";".join(pdb_dirs) if pdb_dirs else None)
    for base, size, name in modules:
        symbols.load(name, base, size)  # every module: unwinding needs their function tables

    def describe(addr):
        mod = module_of(addr)
        where = f"{Path(mod[2]).name}+0x{addr - mod[0]:x}" if mod else "?"
        sym = symbols.name(addr) if mod else None
        return f"0x{addr:016x} {where}" + (f"  {sym}" if sym else "")

    print(f"exception 0x{code:08x} on thread {thread_id}")
    if code == 0xC0000005 and len(params) >= 2:
        print(f"  {'write' if params[0] == 1 else 'read'} of address 0x{params[1]:x}")
    print("  at " + describe(address))

    print("call stack:")
    for frame in walk_stack(data, streams, thread_id, symbols, modules):
        print("  " + describe(frame))

    # return addresses into our own modules, top of the stack first
    print("stack scan (may include stale frames):")
    start, stack = thread_stack(data, streams, thread_id)
    ours = [m for m in modules if Path(m[2]).stem.lower() in {Path(p).stem.lower() for d in pdb_dirs for p in Path(d).glob("*.pdb")}]
    shown = 0
    for offset in range(0, len(stack) - 7, 8):
        value = struct.unpack_from("<Q", stack, offset)[0]
        if any(base <= value < base + size for base, size, _ in ours):
            text = describe(value)
            if "+0x0" not in text.split("  ")[-1]:
                print("  stack " + text)
                shown += 1
                if shown >= 25:
                    break


if __name__ == "__main__":
    main()
