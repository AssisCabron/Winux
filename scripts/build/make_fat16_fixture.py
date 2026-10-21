"""One-time 16 MiB FAT16 filesystem fixture, read-only in QEMU.

Cluster chain for HELLO.TXT is deliberately noncontiguous 2 -> 4 -> EOF.
The otherwise-unused reserved sector 16 contains the legacy block-test
marker so the same image supports PCI/DMA lifecycle regressions.
"""
from pathlib import Path
import hashlib,json,struct
R=Path(__file__).resolve().parents[2]
OUT=R/"build"/"vfs-preview"
OUT.mkdir(parents=True,exist_ok=True)
TARGET=OUT/"fat16-test-disk.img"
if TARGET.exists():raise RuntimeError("Refuse to overwrite existing FAT16 test volume")
N=32768;BPS=512;RES=32;FATS=2;FAT_SIZE=128;ROOT_ENTRIES=512
ROOT_SECTORS=ROOT_ENTRIES*32//BPS
DATA=RES+FATS*FAT_SIZE+ROOT_SECTORS
b=bytearray(N*BPS)
boot=memoryview(b)[:512]
boot[:3]=b"\xeb\x3c\x90";boot[3:11]=b"WINUXFAT"
struct.pack_into("<H",boot,11,BPS)
boot[13]=1
struct.pack_into("<H",boot,14,RES)
boot[16]=FATS
struct.pack_into("<H",boot,17,ROOT_ENTRIES)
struct.pack_into("<H",boot,19,N)
boot[21]=0xf8
struct.pack_into("<H",boot,22,FAT_SIZE)
struct.pack_into("<H",boot,24,63)
struct.pack_into("<H",boot,26,255)
boot[36]=0x80;boot[38]=0x29
struct.pack_into("<I",boot,39,0x20261021)
boot[43:54]=b"WINUX-VOLUM"
boot[54:62]=b"FAT16   "
boot[510:512]=b"\x55\xaa"
marker=b"WINUX-BLOCK-IO-V1"
b[16*512:16*512+len(marker)]=marker
for fat in range(FATS):
    addr=(RES+fat*FAT_SIZE)*BPS
    for cluster,value in ((0,0xfff8),(1,0xffff),(2,4),(3,0xffff),(4,0xffff)):
        struct.pack_into("<H",b,addr+2*cluster,value)
message=(b"WINUX FAT16 FILE OK: scattered cluster chain 2 -> 4; "
         b"this content comes from the real VirtIO block device after ExitBootServices.\n")
payload=(message*12)[:900]
assert len(payload)==900
status=b"WINUX_FAT16_RUNTIME_READ_OK\n"
def entry(index,name,cluster,content):
    root=(RES+FATS*FAT_SIZE)*BPS+index*32
    assert len(name)==11
    b[root:root+11]=name
    b[root+11]=0x20
    struct.pack_into("<H",b,root+26,cluster)
    struct.pack_into("<I",b,root+28,len(content))
entry(0,b"HELLO   TXT",2,payload)
entry(1,b"STATUS  TXT",3,status)
b[(DATA+0)*BPS:(DATA+0)*BPS+512]=payload[:512]
b[(DATA+1)*BPS:(DATA+1)*BPS+len(status)]=status
b[(DATA+2)*BPS:(DATA+2)*BPS+len(payload[512:])]=payload[512:]
TARGET.write_bytes(b)
record={"path":str(TARGET),"sha256":hashlib.sha256(b).hexdigest(),
        "total_sectors":N,"sector_bytes":BPS,"root_lba":RES+FATS*FAT_SIZE,
        "data_lba":DATA,"fat_size_sectors":FAT_SIZE,
        "files":{"HELLO.TXT":{"size":len(payload),"sha256":hashlib.sha256(payload).hexdigest()},
                 "STATUS.TXT":{"size":len(status),"sha256":hashlib.sha256(status).hexdigest()}}}
(OUT/"fat16-fixture.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
print(json.dumps(record,indent=2))
