"""Read only selected heap-policy fields from this process's own PEB."""
import ctypes as C
import hashlib
import json
import os
import struct
from pathlib import Path


def main():
    assert C.sizeof(C.c_void_p) == 8
    n = C.WinDLL('ntdll')
    query = n.NtQueryInformationProcess
    query.argtypes = [C.c_void_p,C.c_uint,C.c_void_p,C.c_uint,C.POINTER(C.c_uint)]
    query.restype = C.c_long
    info = C.create_string_buffer(48)
    returned = C.c_uint()
    status = query(C.c_void_p(-1),0,info,48,C.byref(returned))
    if status < 0 or returned.value != 48:
        raise RuntimeError(f'Own ProcessBasicInformation failed: {status & 0xffffffff:08x}')
    peb = struct.unpack_from('<Q',info.raw,8)[0]
    assert peb
    fields = {}
    for name,offset in [('segment_reserve',0xc8),('segment_commit',0xd0),
                        ('decommit_total_free_threshold',0xd8),('decommit_free_block_threshold',0xe0)]:
        fields[name] = {'offset':hex(offset),'bytes':struct.unpack('<Q',C.string_at(peb+offset,8))[0]}
    source = Path(os.environ['SystemRoot'])/'System32/ntdll.dll'
    result = {'query_status':status & 0xffffffff,'query_returned':returned.value,
              'ntdll_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'fields':fields,
              'limits':'Own host process only; no pointers, PIDs, other processes or PEB writes. These observations are not a universal or mandatory guest policy.'}
    output = Path(__file__).parent/'build/peb-heap-defaults-windows.json'
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':main()
