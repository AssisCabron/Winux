"""Version-pinned Windows host probe: genuine HeapSetInformation + NTDLL realloc."""
import ctypes as c
import json
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
kb = c.WinDLL("kernelbase", use_last_error=True)
nt = c.WinDLL("ntdll", use_last_error=True)
V=c.c_void_p; U=c.c_uint32; Z=c.c_size_t
kb.HeapCreate.argtypes=[U,Z,Z];kb.HeapCreate.restype=V
kb.HeapDestroy.argtypes=[V];kb.HeapDestroy.restype=c.c_int
kb.HeapSetInformation.argtypes=[V,c.c_int,V,Z];kb.HeapSetInformation.restype=c.c_int
kb.HeapQueryInformation.argtypes=[V,c.c_int,V,Z,c.POINTER(Z)];kb.HeapQueryInformation.restype=c.c_int
nt.RtlReAllocateHeap.argtypes=[V,U,V,Z];nt.RtlReAllocateHeap.restype=V
nt.RtlAllocateHeap.argtypes=[V,U,Z];nt.RtlAllocateHeap.restype=V
nt.RtlFreeHeap.argtypes=[V,U,V];nt.RtlFreeHeap.restype=c.c_ubyte
nt.RtlSizeHeap.argtypes=[V,U,V];nt.RtlSizeHeap.restype=Z

def policy(heap):
    current=U(0);length=Z(0)
    c.set_last_error(0)
    success=bool(kb.HeapQueryInformation(heap,0,c.byref(current),4,c.byref(length)))
    return {"success":success,"value":current.value,"length":length.value,"error":c.get_last_error()}
def setpolicy(heap, value, length=4):
    mode=U(value)
    c.set_last_error(0)
    success=bool(kb.HeapSetInformation(heap,0,c.byref(mode),length))
    return {"success":success,"error":c.get_last_error()}
def realloc_probe(heap, first, second):
    ptr=nt.RtlAllocateHeap(heap,8,first)
    if not ptr: return {"first":first,"second":second,"allocated":False}
    current=ptr
    result={"first":first,"second":second,"allocated":True}
    try:
        before=(c.c_ubyte*first).from_address(ptr)
        result["initial_zero"]=all(b==0 for b in before)
        for i in range(first):before[i]=(i*13+7)&255
        grown=nt.RtlReAllocateHeap(heap,8,ptr,second)
        result["realloc_success"]=bool(grown)
        if grown:
            current=grown
            after=(c.c_ubyte*second).from_address(grown)
            result["prefix_preserved"]=all(after[i]==((i*13+7)&255) for i in range(min(first,second)))
            result["extension_zero"]=all(after[i]==0 for i in range(first,second)) if second>first else True
            result["reported_size"]=int(nt.RtlSizeHeap(heap,0,grown))
    finally:
        result["freed"]=bool(nt.RtlFreeHeap(heap,0,current))
    return result

heap=kb.HeapCreate(0,4096,0)
if not heap: raise RuntimeError(f"HeapCreate failed: {c.get_last_error()}")
rows={"windows_ntdll_sha256":hashlib.sha256(Path("C:/Windows/System32/ntdll.dll").read_bytes()).hexdigest()}
try:
    rows["before"]=policy(heap)
    rows["invalid_length"]=setpolicy(heap,2,0)
    rows["invalid_value"]=setpolicy(heap,1,4)
    rows["unchanged_after_invalid"]=policy(heap)
    rows["enable"]=setpolicy(heap,2)
    rows["after_enable"]=policy(heap)
    rows["repeat"]=setpolicy(heap,2)
    rows["after_repeat"]=policy(heap)
    rows["realloc"]=[realloc_probe(heap,a,b) for a,b in ((32,128),(128,2048),(4096,8192),(128,16))]
finally:
    rows["destroyed"]=bool(kb.HeapDestroy(heap))
target=ROOT/"build"/"heap-wrapper-realloc-windows.json"
target.write_text(json.dumps(rows,indent=2)+"\n",encoding="utf-8")
print(json.dumps(rows,indent=2))
assert rows["destroyed"] and rows["enable"]["success"] and rows["after_enable"]["value"]==2
assert all(x.get("allocated") and x.get("realloc_success") and x.get("prefix_preserved") and x.get("extension_zero") and x.get("freed") for x in rows["realloc"])
