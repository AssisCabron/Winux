"""Build private process-parameter blocks from synthetic inputs; never launch a process."""
import ctypes as C
import hashlib
import json
import os
import struct
from pathlib import Path


class UnicodeString(C.Structure):
    _fields_ = [('length', C.c_ushort), ('maximum', C.c_ushort), ('buffer', C.c_void_p)]


def main():
    assert C.sizeof(C.c_void_p) == 8 and C.sizeof(UnicodeString) == 16
    ntdll = C.WinDLL('ntdll')
    create = ntdll.RtlCreateProcessParametersEx
    create.argtypes = [C.POINTER(C.c_void_p)] + [C.POINTER(UnicodeString)] * 4 + [C.c_void_p] + [C.POINTER(UnicodeString)] * 4 + [C.c_uint]
    create.restype = C.c_long
    destroy = ntdll.RtlDestroyProcessParameters
    destroy.argtypes = [C.c_void_p]
    destroy.restype = None
    normalize = ntdll.RtlNormalizeProcessParams
    normalize.argtypes = [C.c_void_p]
    normalize.restype = C.c_void_p
    values = [r'C:\WinuxTest\cmd.exe', r'C:\WinuxTest', 'C:\\WinuxTest\\',
              'cmd.exe /c echo WinuxFixture', 'WinuxFixture', 'WinSta0\\Default', '', '']
    owners = [C.create_string_buffer(v.encode('utf-16-le') + b'\0\0') for v in values]
    strings = [UnicodeString(len(v.encode('utf-16-le')), len(v.encode('utf-16-le')) + 2, C.addressof(b)) for v, b in zip(values, owners)]
    env_bytes = 'WINUX_FIXTURE=1\0WINUX_TEXT=hello\0\0'.encode('utf-16-le')
    env = C.create_string_buffer(env_bytes)
    records = []
    for flags in [0, 1]:
        address = C.c_void_p()
        status = create(C.byref(address), *(C.byref(x) for x in strings[:4]),
                        C.addressof(env), *(C.byref(x) for x in strings[4:]), flags)
        if status < 0 or not address.value:
            raise RuntimeError(f'Create failed: {status & 0xffffffff:08x}')
        try:
            base = address.value
            maximum, length, actual_flags = struct.unpack('<III', C.string_at(base, 12))
            assert 0x430 <= length <= maximum <= 1024 * 1024
            assert actual_flags & 1 == flags
            before = C.string_at(base, length)
            assert normalize(base) == base
            first = C.string_at(base, length)
            assert normalize(base) == base and C.string_at(base, length) == first
            observed = []
            for offset, index in [(0x38, 2), (0x50, 1), (0x60, 0), (0x70, 3),
                                  (0xb0, 4), (0xc0, 5), (0xd0, 6), (0xe0, 7)]:
                count, capacity = struct.unpack_from('<HH', first, offset)
                pointer = struct.unpack_from('<Q', first, offset + 8)[0]
                expected = values[index].encode('utf-16-le')
                assert count == len(expected) and count <= capacity
                if count:
                    assert base <= pointer and pointer + count <= base + maximum
                    assert C.string_at(pointer, count) == expected
                prior = struct.unpack_from('<Q', before, offset + 8)[0]
                if prior:
                    assert pointer == (prior if flags else prior + base)
                observed.append({'offset': hex(offset), 'length': count, 'capacity': capacity,
                                 'buffer_offset': pointer - base if pointer else None})
            ep = struct.unpack_from('<Q', first, 0x80)[0]
            assert ep and C.string_at(ep, len(env_bytes)) == env_bytes
            records.append({'input_flags': flags, 'maximum': maximum, 'length': length,
                            'normalized': bool(struct.unpack_from('<I', first, 8)[0] & 1),
                            'normalization_idempotent': True, 'strings': observed,
                            'explicit_environment_preserved': True,
                            'environment_size': struct.unpack_from('<Q', first, 0x3f0)[0]})
        finally:
            destroy(address)
    invalid_image = UnicodeString(4, 2, C.addressof(owners[0]))
    negative = []
    for label, image, directory, flag in [
            ('unknown-flags', strings[0], strings[2], 2),
            ('length-exceeds-maximum', invalid_image, strings[2], 1),
            ('empty-current-directory', strings[0], strings[6], 1)]:
        address = C.c_void_p()
        status = create(C.byref(address), C.byref(image), C.byref(strings[1]), C.byref(directory), C.byref(strings[3]),
                        C.addressof(env), *(C.byref(x) for x in strings[4:]), flag)
        try:
            assert status & 0xffffffff == 0xc000000d and not address.value
            negative.append({'case': label, 'status': status & 0xffffffff, 'output_null': True})
        finally:
            if address.value:
                destroy(address)
    result = {'passed': True, 'negative': negative, 'ntdll_sha256': hashlib.sha256((Path(os.environ['SystemRoot'])/'System32/ntdll.dll').read_bytes()).hexdigest(),
              'records': records, 'limits': 'Private blocks only; no process launch, no PEB replacement, no guest execution. Handle values and host environment are not recorded.'}
    output = Path(__file__).parent/'build/process-parameters-windows.json'
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
