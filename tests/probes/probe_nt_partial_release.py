"""Observe partial MEM_RELEASE on this process's own private reservations."""
from pathlib import Path
import ctypes as c,hashlib,json,os
root=Path(__file__).resolve().parents[2]
nt=c.WinDLL('ntdll');P=c.c_void_p;N=c.c_size_t;U=c.c_uint32
class MBI(c.Structure):
    _fields_=[('base',P),('allocation',P),('allocation_protect',U),('partition',c.c_uint16),
              ('size',N),('state',U),('protect',U),('type',U)]
alloc=nt.NtAllocateVirtualMemory;alloc.restype=c.c_int32;alloc.argtypes=[P,c.POINTER(P),N,c.POINTER(N),U,U]
free=nt.NtFreeVirtualMemory;free.restype=c.c_int32;free.argtypes=[P,c.POINTER(P),c.POINTER(N),U]
query=nt.NtQueryVirtualMemory;query.restype=c.c_int32;query.argtypes=[P,P,U,P,N,c.POINTER(N)]
records=[]
def check(s):
    if s<0:raise RuntimeError(hex(s&0xffffffff))
def info(address):
    result=MBI();check(query(P(-1),P(address),0,c.byref(result),c.sizeof(result),None));return result
for label,offset,length in [('prefix',0,4096),('middle',8192,8192),('suffix',28672,4096),('unaligned',4097,1)]:
    base=P();n=N(32768);check(alloc(P(-1),c.byref(base),0,c.byref(n),0x3000,4));origin=base.value
    record={'case':label,'input_offset':offset,'input_size':length}
    try:
        # Record actual page data preservation in surviving pages, not just metadata.
        for i in range(8):c.c_ubyte.from_address(origin+i*4096).value=40+i
        address=P(origin+offset);size=N(length)
        status=free(P(-1),c.byref(address),c.byref(size),0x8000)
        record.update(status=status&0xffffffff,output_offset=address.value-origin,output_size=size.value,pages=[])
        for i in range(8):
            q=info(origin+i*4096)
            row={'page':i,'state':q.state}
            if q.state!=0x10000:
                assert origin<=q.allocation<origin+32768
                row.update(allocation_offset=q.allocation-origin,base_offset=q.base-origin,size=q.size,protect=q.protect,
                           byte=c.c_ubyte.from_address(origin+i*4096).value)
                assert row['byte']==40+i
            record['pages'].append(row)
        records.append(record)
    finally:
        live={q.allocation for i in range(8) if (q:=info(origin+i*4096)).state!=0x10000}
        for address in live:
            assert origin<=address<origin+32768
            pointer=P(address);size=N();check(free(P(-1),c.byref(pointer),c.byref(size),0x8000))
lines=[]
for r in records:
    lines.append('NT_PARTIAL '+r['case']+' '+','.join(map(str,[r['status'],r['output_offset'],r['output_size']])))
    for p in r['pages']:
        values=[p['page'],p['state']]
        if p['state']!=0x10000:values += [p['allocation_offset'],p['base_offset'],p['size'],p['protect'],p['byte']]
        lines.append('NT_PARTIAL_PAGE '+r['case']+' '+','.join(map(str,values)))
report={'passed':True,'lines':lines,'ntdll_sha256':hashlib.sha256((Path(os.environ['SystemRoot'])/'System32/ntdll.dll').read_bytes()).hexdigest(),
        'scope':'Four successful/failed partial-release observations on own allocations; not a general Windows memory specification.',
        'records':records}
(root/'build/windows-partial-release-probe.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
