"""what windows knows about other processes, through its own api."""

import ctypes
from ctypes import wintypes
from dataclasses import dataclass

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_TH32CS_SNAPPROCESS = 0x2
_INVALID_HANDLE = ctypes.c_void_p(-1).value


class _ProcessEntry(ctypes.Structure):
    _fields_ = (
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    )


_kernel32.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
_kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
_kernel32.Process32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.Process32NextW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


@dataclass(frozen=True)
class Process:
    pid: int
    name: str  # its exe's file name, e.g. hl2.exe


def processes(*names: str) -> list[Process]:
    """the running processes whose exe has one of these names (any case)."""
    wanted = {name.lower() for name in names}
    snapshot = _kernel32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snapshot == _INVALID_HANDLE:
        raise ctypes.WinError(ctypes.get_last_error())
    found = []
    try:
        entry = _ProcessEntry(dwSize=ctypes.sizeof(_ProcessEntry))
        more = _kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            if entry.szExeFile.lower() in wanted:
                found.append(Process(entry.th32ProcessID, entry.szExeFile))
            more = _kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        _kernel32.CloseHandle(snapshot)
    return found
