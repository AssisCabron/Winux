"""Synchronous NT byte I/O in a new private temporary file, removed afterward."""
import ctypes as C
import hashlib
import json
import tempfile
from pathlib import Path


def main():
    k = C.WinDLL('kernel32', use_last_error=True)
    n = C.WinDLL('ntdll')
    k.CreateFileW.argtypes = [C.c_wchar_p, C.c_uint, C.c_uint, C.c_void_p, C.c_uint, C.c_uint, C.c_void_p]
    k.CreateFileW.restype = C.c_void_p
    k.CloseHandle.argtypes = [C.c_void_p]
    for name in ('NtReadFile', 'NtWriteFile'):
        fn = getattr(n, name)
        fn.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p,
                       C.c_void_p, C.c_void_p, C.c_uint, C.POINTER(C.c_longlong), C.c_void_p]
        fn.restype = C.c_long
    records = []
    with tempfile.TemporaryDirectory(prefix='winux-io-') as directory:
        h = k.CreateFileW(str(Path(directory)/'fixture.bin'), 0xc0000000, 0, None, 2, 0, None)
        assert h and h != C.c_void_p(-1).value
        try:
            scenarios = [('write-initial', 'write', b'AB\0CXYZ', None, 7),
                         ('read-eof', 'read', b'', None, 4),
                         ('read-zero-eof', 'read', b'', None, 0),
                         ('read-start', 'read', b'', 0, 3),
                         ('read-current', 'read', b'', None, 8),
                         ('write-offset', 'write', b'12', 2, 2),
                         ('read-current-after-write', 'read', b'', None, 4),
                         ('read-final', 'read', b'', 0, 16),
                         ('read-zero-start', 'read', b'', 0, 0),
                         ('read-after-zero', 'read', b'', None, 2),
                         ('write-zero-offset', 'write', b'', 1, 0),
                         ('read-after-zero-write', 'read', b'', None, 2),
                         ('write-gap', 'write', b'Z', 10, 1),
                         ('read-gap', 'read', b'', 0, 16)]
            for label, operation, payload, offset, count in scenarios:
                buf = (C.c_ubyte * 32)(*([0xa5]*32))
                for i, byte in enumerate(payload):
                    buf[i] = byte
                iosb = (C.c_ubyte * 16)(*([0xa5]*16))
                position = C.c_longlong(offset or 0)
                fn = n.NtWriteFile if operation == 'write' else n.NtReadFile
                status = fn(h, None, None, None, iosb, buf, count,
                            None if offset is None else C.byref(position), None) & 0xffffffff
                assert status != 0x103, 'Unexpected asynchronous result'
                records.append(dict(step=label, operation=operation, payload=payload.hex(),
                                    offset=offset, count=count, status=status,
                                    iosb=bytes(iosb).hex(), buffer=bytes(buf).hex()))
        finally:
            assert k.CloseHandle(h)
    result = dict(ntdll_sha256=hashlib.sha256(Path('C:/Windows/System32/ntdll.dll').read_bytes()).hexdigest(),
                  scope='Private synchronous disk file, current/absolute offsets, no async or console', records=records)
    path = Path(__file__).resolve().parents[2]/'build/file-io-windows.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
