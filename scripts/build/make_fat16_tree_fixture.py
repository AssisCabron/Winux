"""Create a separate disposable FAT16 tree fixture, never modify earlier images.

Adds LOGS/REPORT.TXT (6 KiB scattered FAT chain), a malformed cyclic
LOGS/LOOP.TXT (must be rejected), plus LOGS/ARCHIVE/NOTES.TXT.
"""
from pathlib import Path
import hashlib, json,struct
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/"vfs-preview"
BASE=OUT/"fat16-test-disk.img"
TARGET=OUT/"fat16-tree-disk.img"
ORIGINAL_SHA="aebac5a28cadba1cba25d479412878aeaea8fc8a084aa2e3ee15c0fffed00c69"
assert BASE.is_file() and hashlib.sha256(BASE.read_bytes()).hexdigest()==ORIGINAL_SHA
if TARGET.exists():
    raise RuntimeError("Refusing to overwrite existing tree fixture: "+str(TARGET))
b=bytearray(BASE.read_bytes())
assert len(b)==32768*512
RES=32;FAT_SIZE=128;N_FATS=2;ROOT_START=RES+2*FAT_SIZE
DATA=ROOT_START+512*32//512
def fat(cluster,value):
    for i in range(N_FATS):
        start=(RES+i*FAT_SIZE)*512
        struct.pack_into("<H",b,start+2*cluster,value)
def entry(pos,name,attr,start,size):
    assert len(name)==11
    b[pos:pos+32]=bytes(32)
    b[pos:pos+11]=name
    b[pos+11]=attr
    struct.pack_into("<H",b,pos+26,start)
    struct.pack_into("<I",b,pos+28,size)
def cluster_offset(number):
    assert 2<=number<32768
    return (DATA+number-2)*512
root=ROOT_START*512
entry(root+2*32,b"LOGS       ",0x10,5,0)
fat(5,0xffff)
logs=cluster_offset(5)
entry(logs,b".          ",0x10,5,0)
entry(logs+32,b"..         ",0x10,0,0)
report=bytes(((i*37+11)&255) for i in range(6144))
report_chain=(6,8,10,7,9,11,13,12,14,16,15,17)
for i,k in enumerate(report_chain):
    fat(k,report_chain[i+1] if i+1<len(report_chain) else 0xffff)
    offset=cluster_offset(k)
    b[offset:offset+512]=report[i*512:(i+1)*512]
entry(logs+2*32,b"REPORT  TXT",0x20,6,len(report))
fat(23,24);fat(24,23)
entry(logs+3*32,b"LOOP    TXT",0x20,23,4096)
# Subdirectory under LOGS, also read-only.
fat(25,0xffff)
archive=cluster_offset(25)
entry(logs+4*32,b"ARCHIVE    ",0x10,25,0)
entry(archive,b".          ",0x10,25,0)
entry(archive+32,b"..         ",0x10,5,0)
nested=bytes(((i*13+29)&255) for i in range(1536))
for i,k in enumerate((26,28,27)):
    fat(k,(28,27,0xffff)[i])
    start=cluster_offset(k)
    b[start:start+512]=nested[i*512:(i+1)*512]
entry(archive+2*32,b"NOTES   TXT",0x20,26,len(nested))
# Preserve the original reserved-sector marker and both original files.
assert b[16*512:16*512+17]==b"WINUX-BLOCK-IO-V1"
TARGET.write_bytes(b)
record={
    "path":str(TARGET),"bytes":len(b),"sha256":hashlib.sha256(b).hexdigest(),
    "root_directory":"LOGS","report_size":len(report),
    "report_sha256":hashlib.sha256(report).hexdigest(),
    "report_chain":list(report_chain),
    "nested_path":"LOGS/ARCHIVE/NOTES.TXT",
    "nested_size":len(nested),"nested_sha256":hashlib.sha256(nested).hexdigest(),
    "malformed_chain":"LOGS/LOOP.TXT contains cyclic 23 -> 24 -> 23",
    "read_only":True
}
(OUT/"fat16-tree-fixture.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
print(json.dumps(record,indent=2))
