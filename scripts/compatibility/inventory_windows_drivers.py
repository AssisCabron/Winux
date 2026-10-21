"""Read-only PE import inventory for local, version-pinned Windows .sys drivers.
Does not load, execute, patch or redistribute driver binaries.
"""
from pathlib import Path
import hashlib, json, struct

ROOT=Path(__file__).resolve().parents[2]
DRIVERS=Path("C:/Windows/System32/drivers")
NAMES=("stornvme.sys","storport.sys","disk.sys","partmgr.sys","pci.sys")
OUT=ROOT/"build"/"driver-compat-imports.json"

def u16(data,pos):
    if pos<0 or pos+2>len(data):raise ValueError(f"u16 out of range: {pos}")
    return struct.unpack_from("<H",data,pos)[0]
def u32(data,pos):
    if pos<0 or pos+4>len(data):raise ValueError(f"u32 out of range: {pos}")
    return struct.unpack_from("<I",data,pos)[0]
def u64(data,pos):
    if pos<0 or pos+8>len(data):raise ValueError(f"u64 out of range: {pos}")
    return struct.unpack_from("<Q",data,pos)[0]
def cstring(data,pos,max_len=512):
    if pos<0 or pos>=len(data):raise ValueError("Invalid string offset")
    end=data.find(b"\0",pos,min(pos+max_len,len(data)))
    if end<0:raise ValueError("Unterminated PE symbol string")
    return data[pos:end].decode("ascii",errors="strict")
def inspect(path):
    data=path.read_bytes()
    if data[:2]!=b"MZ":raise ValueError("Not a DOS MZ binary")
    pe=u32(data,0x3c)
    if pe+24>len(data) or data[pe:pe+4]!=b"PE\0\0":raise ValueError("Bad PE signature")
    coff=pe+4
    machine=u16(data,coff)
    count=u16(data,coff+2)
    opt_bytes=u16(data,coff+16)
    opt=coff+20
    if machine!=0x8664 or u16(data,opt)!=0x20b:raise ValueError("Not x64 PE32+")
    if not count or count>96 or opt+opt_bytes>len(data):raise ValueError("Invalid PE sections")
    header_size=u32(data,opt+60)
    if opt_bytes<128:raise ValueError("No data directories")
    sections=[]
    for i in range(count):
        s=opt+opt_bytes+i*40
        if s+40>len(data):raise ValueError("Truncated section header")
        sections.append((u32(data,s+12),u32(data,s+8),u32(data,s+20),u32(data,s+16)))
    def at(rva):
        if rva<header_size and rva<len(data):return rva
        for va,vs,raw,raw_size in sections:
            if va<=rva<va+max(vs,raw_size):
                delta=rva-va
                if delta>=raw_size or raw+delta>=len(data):
                    raise ValueError(f"RVA 0x{rva:x} not file backed")
                return raw+delta
        raise ValueError(f"Unmapped RVA 0x{rva:x}")
    imp=u32(data,opt+112+8)
    size=u32(data,opt+112+12)
    imports={}
    if imp:
        off=at(imp)
        for i in range(min(512,max(1,size//20+1))):
            desc=off+i*20
            if desc+20>len(data):raise ValueError("Truncated import descriptor")
            original,time_stamp,forward,name,first=struct.unpack_from("<5I",data,desc)
            if (original,time_stamp,forward,name,first)==(0,0,0,0,0):break
            dll=cstring(data,at(name)).lower()
            thunk=at(original or first)
            values=[]
            for k in range(8192):
                value=u64(data,thunk+8*k)
                if not value:break
                if value&(1<<63):
                    values.append(f"#{value&65535}")
                else:
                    values.append(cstring(data,at(value&0xffffffff)+2))
            else:raise ValueError("Excessive import thunk count")
            imports.setdefault(dll,[]).extend(values)
        else:raise ValueError("Excessive descriptor count")
    imports={key:sorted(set(vals)) for key,vals in sorted(imports.items())}
    return {
        "name":path.name,"sha256":hashlib.sha256(data).hexdigest(),
        "size_bytes":len(data),"pe_machine":f"0x{machine:04x}",
        "subsystem":u16(data,opt+68),
        "imports":imports,
        "unique_imports":sum(len(v) for v in imports.values()),
        "dependencies":list(imports)
    }

def main():
    records=[]
    for name in NAMES:
        path=DRIVERS/name
        if not path.is_file():
            records.append({"name":name,"present":False})
        else:
            records.append({"present":True,**inspect(path)})
    output={
        "source":"Read-only static PE imports from the local Windows drivers",
        "analysis_mode":"static; no driver execution or extraction",
        "records":records,
        "limitations":["These are symbol names, not implemented kernel contracts",
                       "Import names differ by OS build and hardware device",
                       "Original Windows SYS binaries were not copied to the Winux ISO"]
    }
    OUT.parent.mkdir(exist_ok=True,parents=True)
    OUT.write_text(json.dumps(output,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    for info in records:
        if not info.get("present"):
            print(f"MISSING {info['name']}")
        else:
            print(f"{info['name']}: {info['unique_imports']} unique imports, "
                  f"dependencies {','.join(info['dependencies'])}")
    print(f"INVENTORY_SAVED {OUT}")
if __name__=="__main__":main()
