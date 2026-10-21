import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'probes'))
"""Compare the original bootstrap builder with retained Windows observations."""
import ctypes as C
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path
from probe_process_parameters import UnicodeString

ROOT = Path(__file__).resolve().parents[2]


class Input(C.Structure):
    _fields_ = [('strings', UnicodeString * 8), ('environment', C.c_void_p),
                ('environment_bytes', C.c_size_t), ('handles', C.c_uint64 * 3),
                ('console_flags', C.c_uint32), ('flags', C.c_uint32)]


def main():
    source = ROOT/'tests/host/process_parameters_host.c'
    dll = ROOT/'build/process_parameters_host.dll'
    subprocess.run([str(ROOT/'tools/zig-x86_64-windows-0.14.1/zig.exe'), 'cc', '-target',
                    'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-shared', str(source), '-o', str(dll)], check=True, timeout=180)
    lib = C.CDLL(str(dll))
    build = lib.build_parameters
    build.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Input), C.POINTER(C.c_size_t)]
    build.restype = C.c_uint32
    ntdll = C.WinDLL('ntdll')
    normalize = ntdll.RtlNormalizeProcessParams
    normalize.argtypes = [C.c_void_p]
    normalize.restype = C.c_void_p
    oracle = json.loads((ROOT/'build/process-parameters-windows.json').read_text())
    assert oracle['passed']
    assert hashlib.sha256((Path(os.environ['SystemRoot'])/'System32/ntdll.dll').read_bytes()).hexdigest()==oracle['ntdll_sha256']
    values = [r'C:\WinuxTest\cmd.exe', r'C:\WinuxTest', 'C:\\WinuxTest\\',
              'cmd.exe /c echo WinuxFixture', 'WinuxFixture', 'WinSta0\\Default', '', '']
    owners = [C.create_string_buffer(s.encode('utf-16-le') + b'\0\0') for s in values]
    specs = [UnicodeString(len(s.encode('utf-16-le')), len(s.encode('utf-16-le'))+2, C.addressof(p)) for s,p in zip(values,owners)]
    environment_bytes = 'WINUX_FIXTURE=1\0WINUX_TEXT=hello\0\0'.encode('utf-16-le')
    environment = C.create_string_buffer(environment_bytes)
    request = Input((UnicodeString*8)(*specs), C.addressof(environment), len(environment_bytes), (C.c_uint64*3)(11,12,13), 0, 1)
    matched = []
    for expected in oracle['records']:
        request.flags = expected['input_flags']
        required = C.c_size_t()
        assert build(None,0,C.byref(request),C.byref(required)) == 0xc0000023
        storage = C.create_string_buffer(b'\xa5'*(required.value+32))
        base = C.addressof(storage)+16
        assert build(base,required.value,C.byref(request),C.byref(required)) == 0
        assert storage.raw[:16] == b'\xa5'*16 and storage.raw[16+required.value:32+required.value] == b'\xa5'*16
        before = C.string_at(base, required.value)
        assert struct.unpack_from('<I',before,8)[0] == request.flags
        assert normalize(base)==base
        data = C.string_at(base,required.value)
        assert normalize(base)==base and data==C.string_at(base,required.value)
        assert struct.unpack_from('<II',data)==(expected['maximum'],expected['length'])
        assert struct.unpack_from('<QQQ',data,0x20)==(11,12,13)
        for observed, index in zip(expected['strings'],[2,1,0,3,4,5,6,7]):
            offset=int(observed['offset'],16)
            length,maximum=struct.unpack_from('<HH',data,offset)
            pointer=struct.unpack_from('<Q',data,offset+8)[0]
            assert (length,maximum,pointer-base if pointer else None)==(observed['length'],observed['capacity'],observed['buffer_offset'])
            if length: assert C.string_at(pointer,length)==values[index].encode('utf-16-le')
        ep=struct.unpack_from('<Q',data,0x80)[0]
        assert base<=ep and ep+len(environment_bytes)<=base+required.value
        assert C.string_at(ep,len(environment_bytes))==environment_bytes
        assert struct.unpack_from('<Q',data,0x3f0)[0]==expected['environment_size']
        matched.append(request.flags)
    # Failures must leave the destination unchanged (required size is separate).
    request.flags=1
    scratch=C.create_string_buffer(b'\xa5'*8192);base=C.addressof(scratch)
    def reject(status,capacity=8192):
        before=scratch.raw;size=C.c_size_t()
        assert build(base,capacity,C.byref(request),C.byref(size))==status
        assert scratch.raw==before
    reject(0xc0000023,1)
    request.flags=2;reject(0xc000000d);request.flags=1
    request.strings[0].length=3;reject(0xc000000d);request.strings[0]=specs[0]
    request.strings[0].maximum=2;reject(0xc000000d);request.strings[0]=specs[0]
    request.strings[2].length=0;reject(0xc000000d);request.strings[2]=specs[2]
    request.environment_bytes-=2;reject(0xc000000d);request.environment_bytes+=2
    request.strings[0].buffer=base;reject(0xc000000d);request.strings[0]=specs[0]
    required=C.c_size_t()
    assert build(base+1,8191,C.byref(request),C.byref(required))==0xc000000d
    assert build(base,8192,C.byref(request),C.cast(base,C.POINTER(C.c_size_t)))==0xc000000d
    prior=owners[0].raw
    assert build(base,8192,C.byref(request),C.cast(owners[0],C.POINTER(C.c_size_t)))==0xc000000d
    assert owners[0].raw==prior
    assert scratch.raw[:-1]==b'\xa5'*8192
    # Non-terminated current directory receives one trailing backslash.
    request.strings[2].length-=2
    assert build(base,8192,C.byref(request),C.byref(required))==0
    result_string=UnicodeString.from_address(base+0x38)
    assert C.string_at(result_string.buffer,result_string.length)==values[2].encode('utf-16-le')
    result={'passed':True,'native_fixture_flags_matched':matched,
            'original_normalizer_accepts_both_layouts':True,
            'checks':['strings and capacities','environment','explicit handles','size query','short buffer unchanged',
                      'invalid flags/length/directory','unterminated environment','input/output alias rejection',
                      'alignment','output size alias rejection','trailing directory slash'],
            'core_sha256':hashlib.sha256((ROOT/'src/process/process_parameters.h').read_bytes()).hexdigest(),
            'oracle_ntdll_sha256':oracle['ntdll_sha256'],
            'limitations':'Host builder test only. Not linked to guest PEB, heap, console objects or mutable environment. Inputs are trusted readable spans.'}
    (ROOT/'build/process-parameters-host-test.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
