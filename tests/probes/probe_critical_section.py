"""Observe private, uncontended critical sections; do not modify global locks."""
import ctypes as C
import hashlib
import json
import os
import struct
from pathlib import Path


def main():
    n = C.WinDLL('ntdll')
    init = n.RtlInitializeCriticalSectionEx
    init.argtypes = [C.c_void_p, C.c_uint, C.c_uint]
    init.restype = C.c_long
    enter, leave, delete = n.RtlEnterCriticalSection, n.RtlLeaveCriticalSection, n.RtlDeleteCriticalSection
    for f in [enter, leave, delete]:
        f.argtypes = [C.c_void_p]
        f.restype = C.c_long
    tid = C.WinDLL('kernel32').GetCurrentThreadId()
    records = []
    for flags in [0, 0x10000000]:
        storage = C.create_string_buffer(b'\xa5' * 72)
        base = C.addressof(storage) + 16
        status = init(base, 0, flags)
        assert status >= 0
        held = 0
        def sample(label, status):
            raw = C.string_at(base, 40)
            debug, lock_count, recursion, owner, event, spin = struct.unpack('<QiiQQQ', raw)
            assert recursion == held and (owner == tid if held else owner == 0)
            assert storage.raw[:16] == b'\xa5'*16 and storage.raw[56:72] == b'\xa5'*16
            records.append({'flags': flags, 'step': label, 'status': status & 0xffffffff,
                            'lock_count': lock_count, 'recursion': recursion,
                            'owner_matches': owner == tid if held else owner == 0,
                            'debug_info_kind': 'none' if debug in [0, 0xffffffffffffffff] else 'present',
                            'event_created': bool(event), 'spin': spin})
        try:
            sample('initialize', status)
            for label in ['enter', 'recursive-enter']:
                status = enter(base)
                if status < 0: raise RuntimeError('Enter failed')
                held += 1
                sample(label, status)
            for label in ['recursive-leave', 'leave']:
                status = leave(base)
                if status < 0: raise RuntimeError('Leave failed')
                held -= 1
                sample(label, status)
        finally:
            while held:
                leave(base)
                held -= 1
            status = delete(base)
            assert status >= 0
            records.append({'flags':flags,'step':'delete','status':status & 0xffffffff})
    invalid = []
    for spin, flags, expected in [(0x1000000,0,0xc00000f0),(0,0xe0000000,0xc00000f1),(0,0x11000000,0xc00000f1)]:
        storage = C.create_string_buffer(b'\xa5'*72)
        before = storage.raw
        status = init(C.addressof(storage)+16,spin,flags) & 0xffffffff
        assert status == expected and storage.raw == before
        invalid.append({'spin':spin,'flags':flags,'status':status,'unchanged':True})
    source = Path(os.environ['SystemRoot'])/'System32/ntdll.dll'
    result = {'passed':True,'ntdll_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'records':records,'invalid':invalid,
              'limits':'Private host locks, one thread, no contention. Spin configuration reflects host processor count; not a guest constant. No global PEB/NTDLL locks changed.'}
    output = Path(__file__).parent/'build/critical-section-windows.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':main()
