"""Query owned disk, NUL, pipe and event handles; no writes to files or devices."""
import ctypes as C
import hashlib
import json
from pathlib import Path


def main():
    k = C.WinDLL('kernel32', use_last_error=True)
    n = C.WinDLL('ntdll')
    k.CreateFileW.argtypes = [C.c_wchar_p, C.c_uint, C.c_uint, C.c_void_p, C.c_uint, C.c_uint, C.c_void_p]
    k.CreateFileW.restype = C.c_void_p
    k.CloseHandle.argtypes = [C.c_void_p]
    k.GetFileType.argtypes = [C.c_void_p]
    k.GetFileType.restype = C.c_uint
    k.CreatePipe.argtypes = [C.POINTER(C.c_void_p), C.POINTER(C.c_void_p), C.c_void_p, C.c_uint]
    k.CreateEventW.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_wchar_p]
    k.CreateEventW.restype = C.c_void_p
    q = n.NtQueryVolumeInformationFile
    q.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint, C.c_uint]
    q.restype = C.c_long
    handles = []
    records = []
    try:
        for label, path in [('disk', 'C:/Windows/System32/ntdll.dll'), ('nul', 'NUL')]:
            h = k.CreateFileW(path, 0x80000000, 7, None, 3, 0, None)
            assert h and h != C.c_void_p(-1).value, (path, C.get_last_error())
            handles.append((label, h))
        a, b = C.c_void_p(), C.c_void_p()
        assert k.CreatePipe(C.byref(a), C.byref(b), None, 0)
        handles.extend([('pipe-read', a.value), ('pipe-write', b.value)])
        event = k.CreateEventW(None, 0, 0, None)
        assert event
        handles.append(('event', event))
        for label, handle in [*handles, ('null', 0), ('process-pseudo', C.c_void_p(-1).value),
                              ('invalid', 0x12345678)]:
            C.set_last_error(0x1234)
            file_type = k.GetFileType(handle)
            last_error = C.get_last_error()
            for size in (0, 7, 8, 16):
                iosb = (C.c_ubyte * 16)(*([0xa5]*16))
                out = (C.c_ubyte * 24)(*([0xa5]*24))
                status = q(handle, iosb, out, size, 4) & 0xffffffff
                assert bytes(out)[max(8,size):] == bytes([0xa5])*(24-max(8,size))
                records.append(dict(handle=label, size=size, status=status,
                                    iosb=bytes(iosb).hex(), output=bytes(out).hex(),
                                    file_type=file_type, last_error=last_error))
    finally:
        for _, handle in reversed(handles):
            assert k.CloseHandle(handle)
    source = Path('C:/Windows/System32/ntdll.dll')
    result = dict(ntdll_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  scope='Owned synchronous handles; class 4 only. NUL is not a console handle.',
                  records=records)
    path = Path(__file__).resolve().parents[2] / 'build/volume-device-windows.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
