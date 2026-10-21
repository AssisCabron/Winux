"""Host Windows KernelBase FLS wrapper contract; owns only two allocated slots."""
import ctypes as c
import json
from pathlib import Path
kb=c.WinDLL("kernelbase",use_last_error=True)
CALLBACK=c.WINFUNCTYPE(None,c.c_void_p)
seen=[]
@CALLBACK
def on_cleanup(value):
    seen.append(int(value or 0))
kb.FlsAlloc.argtypes=[CALLBACK];kb.FlsAlloc.restype=c.c_uint32
kb.FlsSetValue.argtypes=[c.c_uint32,c.c_void_p];kb.FlsSetValue.restype=c.c_int
kb.FlsGetValue.argtypes=[c.c_uint32];kb.FlsGetValue.restype=c.c_void_p
kb.FlsFree.argtypes=[c.c_uint32];kb.FlsFree.restype=c.c_int
def step(fun,*args):
    c.set_last_error(0)
    val=fun(*args)
    return {"value":int(val or 0),"last_error":c.get_last_error()}
rows={}
slot=kb.FlsAlloc(on_cleanup)
if slot==0xffffffff:raise RuntimeError("FlsAlloc failed")
rows["allocated_index"]=int(slot)
try:
    rows["initialize_empty"]=step(kb.FlsSetValue,slot,None)
    rows["empty_get"]=step(kb.FlsGetValue,slot)
    rows["set_first"]=step(kb.FlsSetValue,slot,c.c_void_p(0x1111))
    rows["read_first"]=step(kb.FlsGetValue,slot)
    rows["set_second"]=step(kb.FlsSetValue,slot,c.c_void_p(0x2222))
    rows["read_second"]=step(kb.FlsGetValue,slot)
    rows["callbacks_before_free"]=list(seen)
    rows["free"]=step(kb.FlsFree,slot)
    rows["callbacks_after_free"]=list(seen)
finally:
    if not rows.get("free",{}).get("value"): kb.FlsFree(slot)
rows["invalid_get"]=step(kb.FlsGetValue,0xffffffff)
rows["invalid_set"]=step(kb.FlsSetValue,0xffffffff,c.c_void_p(0x1111))
rows["invalid_free"]=step(kb.FlsFree,0xffffffff)
root=Path(__file__).resolve().parents[2]
(root/"build"/"kernelbase-fls-windows.json").write_text(json.dumps(rows,indent=2)+"\n",encoding="utf-8")
print(json.dumps(rows,indent=2))
assert rows["empty_get"]=={"value":0,"last_error":0}
assert rows["read_second"]=={"value":0x2222,"last_error":0}
assert rows["callbacks_before_free"]==[] and rows["callbacks_after_free"]==[0x2222]
assert rows["invalid_get"]["last_error"]==87
