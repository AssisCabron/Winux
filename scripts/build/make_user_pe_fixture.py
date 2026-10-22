"""Build isolated FAT16 PE32+ fixtures; never touch the existing images.
The .text section is genuine external PE file content, not guest kernel code.
"""
from pathlib import Path
import hashlib,struct,json
R=Path(__file__).resolve().parents[2]
B=R/'build'/'vfs-preview'
SRC=B/'fat16-tree-disk.img'
OUT=R/'build'/'user-pe-preview'
OUT.mkdir(parents=True,exist_ok=True)
TARGET=OUT/'user-pe-fat16.img'
GOLD='f88540f059eab658ab51d02e313a3847dd14d55f3c9184a7aa6ed190ba6748da'
assert hashlib.sha256(SRC.read_bytes()).hexdigest()==GOLD
if TARGET.exists():raise RuntimeError('Never overwrite an existing user PE test disk')
data=bytearray(SRC.read_bytes())
assert len(data)==32768*512
RES=32;FATS=2;FATSEC=128;ROOT=RES+FATS*FATSEC;DATA=ROOT+32
IMAGE_BASE=0x10efff000
CODE_VA=IMAGE_BASE+0x1000
def fnv(buf):
    a=14695981039346656037
    for c in buf:a=((a^c)*1099511628211)&0xffffffffffffffff
    return a
def write_fat(cluster,nxt):
    for n in range(FATS):
        struct.pack_into('<H',data,(RES+n*FATSEC)*512+cluster*2,nxt)
def coff(name):
    assert len(name)==11,name
    return name
def directory_entry(pos,name,attr,cluster,size):
    data[pos:pos+32]=bytes(32)
    data[pos:pos+11]=coff(name)
    data[pos+11]=attr
    struct.pack_into('<H',data,pos+26,cluster)
    struct.pack_into('<I',data,pos+28,size)
def pos(cluster):return (DATA+cluster-2)*512
def program(kind):
    image=bytearray(1024)
    image[0:2]=b'MZ'
    struct.pack_into('<I',image,0x3c,0x80)
    p=0x80
    image[p:p+4]=b'PE\x00\x00'
    struct.pack_into('<HHIIIHH',image,p+4,0x8664,1,0,0,0,240,0x22)
    o=p+24
    struct.pack_into('<H',image,o,0x20b)
    image[o+2]=1
    struct.pack_into('<I',image,o+4,512)
    struct.pack_into('<I',image,o+16,0x1000) # Entry point
    struct.pack_into('<I',image,o+20,0x1000) # BaseOfCode
    struct.pack_into('<Q',image,o+24,IMAGE_BASE)
    struct.pack_into('<II',image,o+32,0x1000,0x200)
    struct.pack_into('<HHHHHH',image,o+40,6,0,0,0,6,0)
    struct.pack_into('<I',image,o+56,0x2000)
    struct.pack_into('<I',image,o+60,0x200)
    struct.pack_into('<HH',image,o+68,3,0x100)
    struct.pack_into('<QQQQ',image,o+72,0x1000,0x1000,0x1000,0x1000)
    struct.pack_into('<I',image,o+108,16) # data dirs count; all zero
    s=o+240
    image[s:s+8]=b'.text\x00\x00\x00'
    struct.pack_into('<IIIIIIHHI',image,s+8,0x200,0x1000,0x200,0x200,0,0,0,0,0x60000020)
    code=bytearray(512)
    if kind=='user':
        # Native console_write(1, code+0x100, len); exit(0).
        msg=b'USER_PE_EXEC_OK: external PE32+ on CPL3 via FAT16\n'
        machine=(b'\xb8\x01\x00\x00\x00'+b'\xbf\x01\x00\x00\x00'+
                 b'\x48\xbe'+struct.pack('<Q',CODE_VA+0x100)+
                 b'\xba'+struct.pack('<I',len(msg))+b'\xcd\x80'+
                 b'\x31\xc0\x31\xff\xcd\x80\x0f\x0b')
        code[:len(machine)]=machine
        code[0x100:0x100+len(msg)]=msg
    elif kind=='write':
        # movabs rax,CODE_VA; mov byte [rax],0x90 -> supervisor #PF P/W/U
        machine=b'\x48\xb8'+struct.pack('<Q',CODE_VA)+b'\xc6\x00\x90\x0f\x0b'
        code[:len(machine)]=machine
    elif kind=='nx':
        # movabs rax,USER_STACK; jmp rax -> NX #PF
        stack=CODE_VA+0x1000
        machine=b'\x48\xb8'+struct.pack('<Q',stack+0x100)+b'\xff\xe0\x0f\x0b'
        code[:len(machine)]=machine
    elif kind=='bad':
        code[:4]=b'\xcc\xcc\xcc\xcc'
        struct.pack_into('<I',image,0x3c,0x7fffffff)
    else:raise ValueError(kind)
    image[512:1024]=code
    return bytes(image)
write_fat(40,0xffff)
base=pos(40)
directory_entry(ROOT*512+3*32,b'APPS       ',0x10,40,0)
directory_entry(base,b'.          ',0x10,40,0)
directory_entry(base+32,b'..         ',0x10,0,0)
manifest={}
specs=[
 ('USER64.EXE',b'USER64  EXE','user',(41,43)),
 ('BAD64.EXE',b'BAD64   EXE','bad',(45,47)),
 ('WRITE64.EXE',b'WRITE64 EXE','write',(49,51)),
 ('NX64.EXE',b'NX64    EXE','nx',(53,55)),
]
for n,(name,dos,kind,chain) in enumerate(specs):
    content=program(kind)
    write_fat(chain[0],chain[1])
    write_fat(chain[1],0xffff)
    for i,cluster in enumerate(chain):
        data[pos(cluster):pos(cluster)+512]=content[i*512:(i+1)*512]
    directory_entry(base+(n+2)*32,dos,0x20,chain[0],len(content))
    manifest[name]={'sha256':hashlib.sha256(content).hexdigest(),
                    'fnv64':f'{fnv(content):016x}','size':len(content),
                    'clusters':list(chain),'kind':kind}
TARGET.write_bytes(data)
result={'disk':str(TARGET),'disk_sha256':hashlib.sha256(data).hexdigest(),
        'parent_sha256':GOLD,'image_base':hex(IMAGE_BASE),'files':manifest}
(OUT/'user-pe-fixture.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
header=OUT/'user-pe-allowlist.h'
header.write_text('#ifndef WINUX_USER_PE_ALLOWLIST_H\n#define WINUX_USER_PE_ALLOWLIST_H\n'
                  +''.join(f'#define WX_PE_HASH_{kind.upper()} 0x{manifest[name]["fnv64"]}ULL\n'
                           for name,_,kind,_ in specs)
                  +'#endif\n',encoding='utf-8')
print(json.dumps(result,indent=2))
