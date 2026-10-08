"""what windows knows about other processes and the files and mutexes they hold, through its own api."""

import ctypes
import msvcrt
import os
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_TH32CS_SNAPPROCESS = 0x2
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_SYNCHRONIZE = 0x00100000
_GENERIC_READ = 0x80000000
_FILE_SHARE_ALL = 0x1 | 0x2 | 0x4  # read, write, delete
_OPEN_EXISTING = 3
_ERROR_FILE_NOT_FOUND = 2
_ERROR_PATH_NOT_FOUND = 3
_ERROR_SHARING_VIOLATION = 32
_ERROR_LOCK_VIOLATION = 33
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
_kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_kernel32.OpenProcess.restype = wintypes.HANDLE
_FILETIME_P = ctypes.POINTER(wintypes.FILETIME)
_kernel32.GetProcessTimes.argtypes = (wintypes.HANDLE, _FILETIME_P, _FILETIME_P, _FILETIME_P, _FILETIME_P)
_kernel32.OpenMutexW.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR)
_kernel32.OpenMutexW.restype = wintypes.HANDLE
_kernel32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
_kernel32.CreateFileW.restype = wintypes.HANDLE
_kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


@dataclass(frozen=True)
class Process:
    pid: int
    name: str  # its exe's file name, e.g. hl2.exe
    started: int | None  # when, in windows' 100 ns ticks; None when windows won't say


def _started(pid: int) -> int | None:
    handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        created, ended, kernel, user = (wintypes.FILETIME() for _ in range(4))
        if not _kernel32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel), ctypes.byref(user)):
            return None
        return created.dwHighDateTime << 32 | created.dwLowDateTime
    finally:
        _kernel32.CloseHandle(handle)


def processes(*names: str) -> list[Process]:
    """the running processes whose exe has one of these names (any case), oldest first."""
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
                found.append(Process(entry.th32ProcessID, entry.szExeFile, _started(entry.th32ProcessID)))
            more = _kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        _kernel32.CloseHandle(snapshot)
    return sorted(found, key=lambda process: (process.started is None, process.started or 0))


def mutex_exists(name: str) -> bool:
    """whether some process holds a named mutex (e.g. Local\\HalfCraft_v1_minecraft)."""
    handle = _kernel32.OpenMutexW(_SYNCHRONIZE, False, name)
    if handle:
        _kernel32.CloseHandle(handle)
        return True
    return ctypes.get_last_error() != _ERROR_FILE_NOT_FOUND  # there, but not ours to open


def _open(path: Path, share: int) -> int | None:
    """a handle to an existing file, None when it isn't there; OSError when windows won't open it."""
    handle = _kernel32.CreateFileW(str(path), _GENERIC_READ, share, None, _OPEN_EXISTING, 0, None)
    if handle == _INVALID_HANDLE:
        error = ctypes.get_last_error()
        if error in (_ERROR_FILE_NOT_FOUND, _ERROR_PATH_NOT_FOUND):
            return None
        raise ctypes.WinError(error)
    return handle


def in_use(path: Path) -> bool:
    """whether another process has the file open in a way that keeps others out (a writer, mostly)."""
    try:
        handle = _open(path, 0)
    except OSError as error:
        if error.winerror in (_ERROR_SHARING_VIOLATION, _ERROR_LOCK_VIOLATION):
            return True
        raise
    if handle is not None:
        _kernel32.CloseHandle(handle)
    return False


def open_shared(path: Path) -> BinaryIO | None:
    """a file opened for reading that its writer can still write, rename or delete (a game's log), or None."""
    handle = _open(path, _FILE_SHARE_ALL)
    if handle is None:
        return None
    return os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY), "rb")
