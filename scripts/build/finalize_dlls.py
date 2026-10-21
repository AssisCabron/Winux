"""Build explicit export sections in our original DLLs; never accepts Windows DLLs.
Zig 0.14's export directive path loses ordinal/forwarder metadata, so construct
the PE export directory from the original code RVAs and the declared test ABI.
"""
from pathlib import Path
import argparse,struct
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('name',choices=['winuxrt','winuxbridge']);args=p.parse_args()
path=root/'build'/f'{args.name}.dll';b=bytearray(path.read_bytes())
u16=lambda o:struct.unpack_from('<H',b,o)[0]
u32=lambda o:struct.unpack_from('<I',b,o)[0]
pe=u32(60);op=pe+24;ns=u16(pe+6);table=op+u16(pe+20)
assert b[:2]==b'MZ' and b[pe:pe+4]==b'PE\0\0' and u16(pe+22)&0x2000
sections=[(u32(table+i*40+12),u32(table+i*40+16),u32(table+i*40+20)) for i in range(ns)]
def offset(r):
    for va,n,at in sections:
        if va<=r<va+n:return at+r-va
    raise ValueError(hex(r))
def string(r):
    at=offset(r);return bytes(b[at:b.index(0,at)]).decode('ascii')
e=offset(u32(op+112));base,nf,nn,fa,na,oa=[u32(e+x) for x in [16,20,24,28,32,36]]
old={string(u32(offset(na)+i*4)):u32(offset(fa)+u16(offset(oa)+i*2)*4) for i in range(nn)}
if args.name=='winuxrt':
    assert set(old)=={'WinuxAdd','WinuxValue','WinuxAttached','OrdinalOnly','_tls_index','_tls_used','dll_tls_end','dll_tls_start','dll_tls_value','dll_tls_zero'}
    exports=[('WinuxAdd',7,old['WinuxAdd']),('WinuxValue',11,old['WinuxValue']),('WinuxAttached',13,old['WinuxAttached']),(None,17,old['OrdinalOnly'])]
else:
    assert set(old)=={'BridgeAdd'}
    exports=[('BridgeAdd',1,old['BridgeAdd']),('ForwardAdd',2,'winuxrt.WinuxAdd'),('ForwardData',3,'winuxrt.WinuxValue'),('ForwardOrdinal',4,'winuxrt.#7')]
align=lambda n,a:(n+a-1)//a*a
sa,fa_align=u32(op+32),u32(op+36);va=align(u32(op+56),sa);raw=align(len(b),fa_align)
assert table+(ns+1)*40<=u32(op+60)
first=min(x[1] for x in exports);count=max(x[1] for x in exports)-first+1
named=sorted((x for x in exports if x[0]),key=lambda x:x[0])
data=bytearray(40+count*4+len(named)*6)
def add_string(s):
    r=va+len(data);data.extend(s.encode('ascii')+b'\0');return r
name=add_string(args.name+'.dll');functions=va+40;names=functions+count*4;ordinals=names+len(named)*4
struct.pack_into('<IIHHIIIIIII',data,0,0,0,0,0,name,first,count,len(named),functions,names,ordinals)
for symbol,ordinal,value in exports:
    target=add_string(value) if isinstance(value,str) else value
    struct.pack_into('<I',data,40+(ordinal-first)*4,target)
for i,(symbol,ordinal,value) in enumerate(named):
    struct.pack_into('<I',data,names-va+i*4,add_string(symbol))
    struct.pack_into('<H',data,ordinals-va+i*2,ordinal-first)
size=len(data);rawsize=align(size,fa_align)
header=bytearray(40);header[:8]=b'.wxexp\0\0';struct.pack_into('<IIII',header,8,size,va,rawsize,raw);struct.pack_into('<I',header,36,0x40000040)
b[table+ns*40:table+(ns+1)*40]=header
struct.pack_into('<H',b,pe+6,ns+1);struct.pack_into('<I',b,op+56,align(va+size,sa));struct.pack_into('<II',b,op+112,va,size)
struct.pack_into('<I',b,op+8,u32(op+8)+rawsize);struct.pack_into('<I',b,op+64,0)
b.extend(b'\0'*(raw-len(b)));b.extend(data);b.extend(b'\0'*(rawsize-size));path.write_bytes(b)
print(f'{path.name}: explicit exports, ordinal base {first}, {count} slots')
