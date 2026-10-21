import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Probe NT memory queries on private allocations in this Python process only.
No writes to Windows binaries, remote processes, or persistent system state.
The normalized observations are compared with the kernel's vm-query-test.
"""
from pathlib import Path
import ctypes as c
import hashlib,json,sys

root=Path(__file__).resolve().parents[2]
nt=c.WinDLL('ntdll'); ptr=c.c_void_p; size=c.c_size_t; ulong=c.c_uint32
class MBI(c.Structure):
    _fields_=[('base',ptr),('allocation',ptr),('allocation_protect',ulong),
              ('partition',c.c_uint16),('region_size',size),('state',ulong),
              ('protect',ulong),('type',ulong)]
assert c.sizeof(MBI)==48
alloc=nt.NtAllocateVirtualMemory;alloc.restype=c.c_int32
alloc.argtypes=[ptr,c.POINTER(ptr),size,c.POINTER(size),ulong,ulong]
free=nt.NtFreeVirtualMemory;free.restype=c.c_int32
free.argtypes=[ptr,c.POINTER(ptr),c.POINTER(size),ulong]
protect=nt.NtProtectVirtualMemory;protect.restype=c.c_int32
protect.argtypes=[ptr,c.POINTER(ptr),c.POINTER(size),ulong,c.POINTER(ulong)]
query=nt.NtQueryVirtualMemory;query.restype=c.c_int32
query.argtypes=[ptr,ptr,ulong,ptr,size,c.POINTER(size)]
process=ptr(-1);base=ptr();length=size(4*4096);records=[];lines=[]
def check(status):
    if status<0:raise RuntimeError(f'NTSTATUS {status&0xffffffff:08x}')
def observe(label,offset,free_state=False):
    info=MBI();returned=size()
    check(query(process,ptr(base.value+offset),0,c.byref(info),c.sizeof(info),c.byref(returned)))
    assert returned.value==48
    if free_state:
        assert info.state==0x10000
        values=[info.state] # Other MEM_FREE fields and host extent are not compared.
    else:
        assert info.allocation==base.value
        values=[info.base-base.value,info.region_size,info.state,
                info.protect if info.state==0x1000 else 0,info.allocation_protect,info.type]
    line='VM_QUERY '+label+' '+','.join(map(str,values))
    lines.append(line);records.append({'label':label,'values':values,'return_length':returned.value})
def commit(offset,count):
    address=ptr(base.value+offset);n=size(count);check(alloc(process,c.byref(address),0,c.byref(n),0x1000,4))
def decommit(offset,count):
    address=ptr(base.value+offset);n=size(count);check(free(process,c.byref(address),c.byref(n),0x4000))
reserved=False;report={'passed':False}
try:
    check(alloc(process,c.byref(base),0,c.byref(length),0x2000,4));reserved=True
    assert length.value==16384
    observe('reserve',123)
    observe('reserve-interior',4096+7)
    commit(4096,8192)
    observe('prefix',0);observe('commit',4096+1);observe('commit-interior',8192+1);observe('suffix',12288)
    address=ptr(base.value+8192);n=size(4096);old=ulong()
    check(protect(process,c.byref(address),c.byref(n),2,c.byref(old)));assert old.value==4
    observe('split-write',4096);observe('split-read',8192)
    decommit(4096,8192);observe('decommit',0)
    commit(0,16384);observe('recommit',0)
    address=ptr(base.value);n=size();check(free(process,c.byref(address),c.byref(n),0x8000));reserved=False
    observe('release',0,True)
    report['passed']=True
finally:
    if reserved:
        address=ptr(base.value);n=size();check(free(process,c.byref(address),c.byref(n),0x8000))
    import os
    dll=Path(os.environ['SystemRoot'])/'System32/ntdll.dll'
    report.update(ntdll_sha256=hashlib.sha256(dll.read_bytes()).hexdigest(),observations=records,lines=lines,
                  limitations='Current process, x64, MemoryBasicInformation only; reserved Protect normalized because undefined; free extent not compared.')
    (root/'build/windows-nt-memory-test.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
sys.exit(0 if report['passed'] else 1)
