"""Create ONLY a new, file-backed FAT16 disk image with a Winux ownership tag.
No physical drive or existing image is ever formatted or overwritten.
"""
from pathlib import Path
import struct,sys,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/"ring3-preview"/"winux-data.img"
PART_START=2048
SECTORS=61440
SECTOR=512
FAT_SECTORS=120
ROOT_SECTORS=32
DATA_START=1+FAT_SECTORS*2+ROOT_SECTORS
MAGIC=b"WINUX-DATA-V1"

def main():
    OUT.parent.mkdir(parents=True,exist_ok=True)
    if OUT.exists():raise SystemExit("Refuse to overwrite an existing data volume: "+str(OUT))
    data=bytearray((PART_START+SECTORS)*SECTOR)
    data[0:3]=b"\xeb\x3c\x90"
    data[440:444]=struct.pack("<I",0x20261021)
    off=446
    data[off+4]=0x06 # FAT16
    data[off+8:off+12]=struct.pack("<I",PART_START)
    data[off+12:off+16]=struct.pack("<I",SECTORS)
    data[510:512]=b"\x55\xaa"
    base=PART_START*SECTOR
    data[base:base+11]=b"\xeb\x3c\x90WINUXOS "
    struct.pack_into("<H",data,base+11,512)
    data[base+13]=2 # sectors/cluster
    struct.pack_into("<H",data,base+14,1) # reserved sectors
    data[base+16]=2 # two FATs
    struct.pack_into("<H",data,base+17,512) # root dir entries
    struct.pack_into("<H",data,base+19,SECTORS)
    data[base+21]=0xf8
    struct.pack_into("<H",data,base+22,FAT_SECTORS)
    struct.pack_into("<H",data,base+24,63)
    struct.pack_into("<H",data,base+26,255)
    struct.pack_into("<I",data,base+28,PART_START)
    data[base+36:base+39]=b"\x80\x00\x29"
    struct.pack_into("<I",data,base+39,0x20261021)
    data[base+43:base+54]=b"WINUXDATA  "
    data[base+54:base+62]=b"FAT16   "
    data[base+510:base+512]=b"\x55\xaa"
    for fat in range(2):
        fst=(PART_START+1+fat*FAT_SECTORS)*SECTOR
        struct.pack_into("<HHH",data,fst,0xfff8,0xffff,0xffff)
    root=(PART_START+1+FAT_SECTORS*2)*SECTOR
    entry=bytearray(32)
    entry[:11]=b"WINUX   ID "
    entry[11]=0x20
    struct.pack_into("<H",entry,26,2)
    struct.pack_into("<I",entry,28,len(MAGIC))
    data[root:root+32]=entry
    cluster2=(PART_START+DATA_START)*SECTOR
    data[cluster2:cluster2+len(MAGIC)]=MAGIC
    with OUT.open("xb") as f:f.write(data)
    summary={"file":str(OUT),"size":OUT.stat().st_size,"sha256":hashlib.sha256(data).hexdigest(),
             "partition_lba":PART_START,"filesystem":"FAT16","tag":MAGIC.decode(),
             "safety":"Created new image only; no host volume accessed"}
    (OUT.parent/"data-volume-initial.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(summary,indent=2))
if __name__=="__main__":main()
