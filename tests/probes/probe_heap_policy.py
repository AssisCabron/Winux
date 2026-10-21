"""Probe CRT-requested private-heap compatibility policy on the local Windows host.
Only touches a heap created and destroyed inside this process; does not edit the OS.
"""
import ctypes as c
import hashlib
import json
import platform
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
kernel32 = c.WinDLL("kernel32", use_last_error=True)
ntdll = c.WinDLL("ntdll", use_last_error=True)
PVOID = c.c_void_p
DWORD = c.c_uint32
SIZE = c.c_size_t
kernel32.HeapCreate.argtypes = [DWORD, SIZE, SIZE]
kernel32.HeapCreate.restype = PVOID
kernel32.HeapDestroy.argtypes = [PVOID]
kernel32.HeapDestroy.restype = c.c_int
kernel32.HeapQueryInformation.argtypes = [PVOID, c.c_int, PVOID, SIZE, c.POINTER(SIZE)]
kernel32.HeapQueryInformation.restype = c.c_int
kernel32.HeapSetInformation.argtypes = [PVOID, c.c_int, PVOID, SIZE]
kernel32.HeapSetInformation.restype = c.c_int
ntdll.RtlSetHeapInformation.argtypes = [PVOID, c.c_int, PVOID, SIZE]
ntdll.RtlSetHeapInformation.restype = c.c_long
def hex_status(value):
    return f"0x{value & 0xffffffff:08x}"
def query(heap):
    value = DWORD(0xa5a5a5a5)
    returned = SIZE(0)
    c.set_last_error(0)
    ok = bool(kernel32.HeapQueryInformation(heap, 0, c.byref(value), c.sizeof(value), c.byref(returned)))
    return dict(success=ok, information_class=0, value=value.value if ok else None, returned=returned.value, last_error=c.get_last_error())
def set_win32(heap, value, length=4):
    arg = DWORD(value)
    c.set_last_error(0)
    ok = bool(kernel32.HeapSetInformation(heap, 0, c.byref(arg), length))
    return dict(success=ok, status_or_error=c.get_last_error())
def set_nt(heap, value, length=4):
    arg = DWORD(value)
    status = int(ntdll.RtlSetHeapInformation(heap, 0, c.byref(arg), length))
    return dict(status=hex_status(status), success=status >= 0)
def main():
    heap = kernel32.HeapCreate(0, 4096, 0)
    if not heap:
        raise RuntimeError(f"HeapCreate failed: {c.get_last_error()}")
    result = dict(source="local Windows private heap", windows=platform.version(), heap_create=(0,4096,0))
    try:
        result["initial"] = query(heap)
        result["invalid_nt_length_zero"] = set_nt(heap, 2, 0)
        result["invalid_nt_value_one"] = set_nt(heap, 1, 4)
        result["after_invalid_nt"] = query(heap)
        result["first_win32_set_2"] = set_win32(heap, 2)
        result["after_first"] = query(heap)
        result["second_nt_set_2"] = set_nt(heap, 2)
        result["after_second"] = query(heap)
        result["invalid_win32_length_zero"] = set_win32(heap, 2, 0)
        result["after_invalid"] = query(heap)
    finally:
        result["destroy_success"] = bool(kernel32.HeapDestroy(heap))
    system = Path("C:/Windows/System32/ntdll.dll")
    result["ntdll_sha256"] = hashlib.sha256(system.read_bytes()).hexdigest()
    output = ROOT / "build" / "heap-policy-windows-probe.json"
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    if not result["destroy_success"]:
        raise RuntimeError("HeapDestroy failed")
if __name__ == "__main__":
    main()
