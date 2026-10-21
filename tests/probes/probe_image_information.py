"""Measure NtQueryVirtualMemory class 6 on owned process addresses only."""
import ctypes as C
import hashlib
import json
from pathlib import Path


def main():
    n = C.WinDLL('ntdll')
    query = n.NtQueryVirtualMemory
    query.argtypes = [C.c_void_p, C.c_void_p, C.c_uint, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    query.restype = C.c_long
    owned = C.create_string_buffer(64)
    records = []
    for label, address in [('image-base', n._handle), ('image-interior', n._handle+16),
                           ('private-buffer', C.addressof(owned)), ('null', 0)]:
        for size in (0, 23, 24, 32):
            out = (C.c_ubyte * 40)(*([0xa5]*40))
            returned = C.c_size_t(0xaaaa)
            status = query(C.c_void_p(-1), address, 6, out, size, C.byref(returned)) & 0xffffffff
            raw = bytes(out)
            assert raw[max(size,24):] == bytes([0xa5])*(40-max(size,24))
            row = dict(step=label, size=size, status=status, returned=returned.value,
                       untouched=raw==bytes([0xa5])*40)
            if status == 0:
                base = int.from_bytes(raw[:8], 'little')
                row.update(base_is_ntdll=base==n._handle, base_is_zero=base==0,
                           image_size=int.from_bytes(raw[8:16], 'little'),
                           flags=int.from_bytes(raw[16:20], 'little'),
                           padding=raw[20:24].hex())
            records.append(row)
    k = C.WinDLL('kernel32')
    k.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
    k.VirtualAlloc.restype = C.c_void_p
    k.VirtualFree.argtypes = [C.c_void_p, C.c_size_t, C.c_uint]
    private_regions = []
    allocation = k.VirtualAlloc(None, 4096, 0x2000, 1)
    assert allocation
    try:
        for label in ('reserved', 'committed-noaccess'):
            if label == 'committed-noaccess':
                assert k.VirtualAlloc(allocation, 4096, 0x1000, 1) == allocation
            out = (C.c_ubyte * 24)(*([0xa5]*24))
            returned = C.c_size_t(0xaaaa)
            status = query(C.c_void_p(-1), allocation, 6, out, 24, C.byref(returned)) & 0xffffffff
            assert status == 0 and returned.value == 24 and bytes(out) == bytes(24)
            private_regions.append(dict(step=label, status=status, returned=returned.value,
                                        output=bytes(out).hex()))
    finally:
        assert k.VirtualFree(allocation, 0, 0x8000)
    source = Path('C:/Windows/System32/ntdll.dll')
    result = dict(ntdll_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  scope='Current process, class 6, owned addresses; not all flags or mappings',
                  records=records, private_regions=private_regions)
    path = Path(__file__).resolve().parents[2] / 'build/image-information-windows.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
