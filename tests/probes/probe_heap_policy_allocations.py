"""Real Windows comparison for allocations after enabling heap policy 2."""
import ctypes as c
import json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
kb=c.WinDLL("kernelbase",use_last_error=True)
nt=c.WinDLL("ntdll",use_last_error=True)
V=c.c_void_p;U=c.c_uint32;S=c.c_size_t
kb.HeapCreate.argtypes=[U,S,S];kb.HeapCreate.restype=V
kb.HeapDestroy.argtypes=[V];kb.HeapDestroy.restype=c.c_int
nt.RtlSetHeapInformation.argtypes=[V,c.c_int,V,S];nt.RtlSetHeapInformation.restype=c.c_long
nt.RtlAllocateHeap.argtypes=[V,U,S];nt.RtlAllocateHeap.restype=V
nt.RtlFreeHeap.argtypes=[V,U,V];nt.RtlFreeHeap.restype=c.c_ubyte
heap=kb.HeapCreate(0,4096,0)
if not heap: raise RuntimeError("HeapCreate failed")
result={"sizes":[8,16,32,64,128,512,4096,65536],"observations":[]}
try:
    mode=U(2)
    status=int(nt.RtlSetHeapInformation(heap,0,c.byref(mode),4))
    result["set_status"]=f"0x{status & 0xffffffff:08x}"
    if status!=0:raise RuntimeError(f"Set failed {status:#x}")
    for size in result["sizes"]:
        addr=nt.RtlAllocateHeap(heap,8,size)
        row={"size":size,"allocated":bool(addr)}
        if addr:
            data=(c.c_ubyte*size).from_address(addr)
            row["initial_zero"]=all(b==0 for b in data)
            data[size-1]=0x5a
            row["last_byte_after_write"]=data[size-1]
            row["freed"]=bool(nt.RtlFreeHeap(heap,0,addr))
        result["observations"].append(row)
finally:
    result["destroyed"]=bool(kb.HeapDestroy(heap))
out=root/"build"/"heap-policy-windows-allocations.json"
out.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,indent=2))
assert result["destroyed"] and all(r.get("allocated") and r.get("initial_zero") and r.get("freed") for r in result["observations"])
