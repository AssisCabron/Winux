"""Read-only observation of KnownDlls object lookup used by local NTDLL.

Opens existing objects with query/traverse rights; creates or modifies none.
All acquired handles are closed. This does not test guest compatibility.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path


class UnicodeString(C.Structure):
    _fields_ = [("Length", C.c_uint16), ("MaximumLength", C.c_uint16), ("Buffer", C.c_void_p)]


class ObjectAttributes(C.Structure):
    _fields_ = [("Length", C.c_uint32), ("RootDirectory", C.c_void_p),
                ("ObjectName", C.POINTER(UnicodeString)), ("Attributes", C.c_uint32),
                ("SecurityDescriptor", C.c_void_p), ("SecurityQualityOfService", C.c_void_p)]


def main():
    assert C.sizeof(UnicodeString) == 16 and C.sizeof(ObjectAttributes) == 48
    dll = C.WinDLL("ntdll")
    open_dir, open_link = dll.NtOpenDirectoryObject, dll.NtOpenSymbolicLinkObject
    for fn in (open_dir, open_link):
        fn.argtypes = [C.POINTER(C.c_void_p), C.c_uint32, C.POINTER(ObjectAttributes)]
        fn.restype = C.c_int32
    query = dll.NtQuerySymbolicLinkObject
    query.argtypes = [C.c_void_p, C.POINTER(UnicodeString), C.POINTER(C.c_uint32)]
    query.restype = C.c_int32
    close = dll.NtClose
    close.argtypes = [C.c_void_p]
    close.restype = C.c_int32
    rows, owned = [], []

    def record(operation, s, **extra):
        rows.append(dict(operation=operation, status=f"{s & 0xffffffff:08x}", **extra))

    def open_object(fn, name, access, root=None):
        buf = C.create_unicode_buffer(name)
        us = UnicodeString(len(name.encode("utf-16-le")), C.sizeof(buf), C.addressof(buf))
        oa = ObjectAttributes(48, root, C.pointer(us), 0x40, None, None)
        h = C.c_void_p()
        s = fn(C.byref(h), access, C.byref(oa))
        record(fn.__name__, s, name=name, relative=root is not None, access=access)
        if s >= 0 and h.value:
            owned.append(h)
        return s, h

    try:
        s, directory = open_object(open_dir, "\\KnownDlls", 3)
        if s < 0:
            raise RuntimeError("KnownDlls directory unavailable")
        s, link = open_object(open_link, "KnownDllPath", 1, directory.value)
        if s < 0:
            raise RuntimeError("KnownDllPath link unavailable")
        # Include the 0x30 initial capacity used by the observed loader.
        for capacity in (0, 2, 38, 39, 40, 0x30, 512):
            buf = C.create_string_buffer(max(capacity, 2))
            us = UnicodeString(0, capacity, C.addressof(buf))
            required = C.c_uint32(0xffffffff)
            s = query(link, C.byref(us), C.byref(required))
            value = C.string_at(us.Buffer, us.Length).decode("utf-16-le") if s >= 0 else None
            record("query", s, capacity=capacity, length=us.Length,
                   required=required.value, value=value)
        open_object(open_link, "Winux_Probe_Missing_Link_6c82791a", 1, directory.value)
    finally:
        for h in reversed(owned):
            record("close", close(h))
    result = dict(scope="Read-only host namespace observation; not guest execution",
                  ntdll_sha256=hashlib.sha256(Path("C:/Windows/System32/ntdll.dll").read_bytes()).hexdigest(),
                  records=rows, limitations=["One existing directory/link and one absent name only.",
                                            "No full object-manager, security or namespace contract established."])
    out = Path(__file__).resolve().parents[2] / "build/nt-namespace-windows.json"
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
