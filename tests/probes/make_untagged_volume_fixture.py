"""Make a rejected-volume fixture from a copied image (never edit the original)."""
from pathlib import Path
import shutil,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
DIR=ROOT/"build"/"ring3-preview"
src=DIR/"winux-data.img"
dst=DIR/"untagged-rejection.img"
assert src.is_file() and not dst.exists()
shutil.copyfile(src,dst)
with dst.open("r+b") as fh:
    sector=512
    start=2048
    reserved=1;fats=2;fatsec=120;rootsec=32
    location=(start+reserved+fats*fatsec+rootsec)*sector
    fh.seek(location)
    assert fh.read(13)==b"WINUX-DATA-V1"
    fh.seek(location)
    fh.write(b"NOT-WINUX-001") # 13-byte deliberately invalid tag
digest=hashlib.sha256(dst.read_bytes()).hexdigest()
(DIR/"untagged-initial-sha256.txt").write_text(digest+"\n")
print(json.dumps({"path":str(dst),"sha256":digest,"purpose":"reject untagged disk without any writes"}))
