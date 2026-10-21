"""Build an isolated P4 preview with the current NT dispatcher; keep all prior ISOs."""
from pathlib import Path
import contextlib
import hashlib
import io
import json
import shutil

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/"build"
PREVIEW=BUILD/"abi-preview"
SOURCE=BUILD/"BOOTX64-ntabi-p4.EFI"
PACK=ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py"
GOLDEN={
 "winux-0.38-x64.iso":"700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0",
 "winux-0.38-wrapper-fls-candidate.iso":"4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77",
}
def hash_file(file:Path):
    obj=hashlib.sha256()
    with file.open("rb") as inp:
        for block in iter(lambda:inp.read(1024*1024),b""):
            obj.update(block)
    return obj.hexdigest()

def check_gold():
    for name,digest in GOLDEN.items():
        assert hash_file(BUILD/name)==digest, f"Baseline ISO changed: {name}"

def run():
    check_gold()
    assert SOURCE.is_file() and SOURCE.read_bytes().startswith(b"MZ")
    PREVIEW.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(SOURCE, PREVIEW/"BOOTX64-wrapper-fls.EFI")
    code=PACK.read_text(encoding="utf-8")
    assert code.count("build=ROOT/'build'")==1
    code=code.replace("build=ROOT/'build'","build=ROOT/'build'/'abi-preview'")
    context={"__file__":str(PACK),"__name__":"__main__"}
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile(code,str(PACK),"exec"),context)
    iso=PREVIEW/"winux-0.38-wrapper-fls-candidate.iso"
    assert iso.is_file() and iso.stat().st_size>30_000_000
    check_gold()
    result={"passed":True,"variant":"P4 + NT ABI manifest","iso_sha256":hash_file(iso),
      "efi_sha256":hash_file(SOURCE),"baseline_isos_unchanged":True,
      "iso_path":str(iso),"notes":"UEFI packer reused; no new ISO algorithm"}
    (PREVIEW/"abi-preview-build.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
if __name__=="__main__":run()
