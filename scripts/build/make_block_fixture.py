"""Create a never-overwritten, expendable read-only disk fixture (not a user disk)."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/"block-preview"
OUT.mkdir(parents=True,exist_ok=True)
disk=OUT/"driver-test-disk.img"
if disk.exists():
    raise RuntimeError("Refusing to overwrite existing test disk: "+str(disk))
sector_bytes=512
sectors=4096
header=bytearray(sector_bytes)
header[:19]=b"WINUX-DISK-READONLY"
header[510:512]=b"\x55\xaa"
marker=bytearray(sector_bytes)
marker[:17]=b"WINUX-BLOCK-IO-V1"
for i in range(17,512):marker[i]=(i*37+11)&255
with disk.open("xb") as out:
    for sector in range(sectors):
        data=header if sector==0 else marker if sector==16 else bytes(sector_bytes)
        out.write(data)
sha=hashlib.sha256(disk.read_bytes()).hexdigest()
result={"disk":str(disk),"size_bytes":disk.stat().st_size,"sector_bytes":sector_bytes,
        "sectors":sectors,"test_sector":16,"sha256":sha,
        "warning":"fixture only; no physical drives or user's existing FAT image touched"}
(OUT/"block-fixture.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,indent=2))
