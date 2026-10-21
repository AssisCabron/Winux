"""Compile separate post-UEFI virtio block-driver preview without changing stable ISO."""
from pathlib import Path
import hashlib,subprocess,sys,io,contextlib,json
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/"block-preview"
OUT.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
BASE={
"winux-0.38-x64.iso":"700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0",
"winux-0.38-wrapper-fls-candidate.iso":"4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77"}
for name,digest in BASE.items():assert sha(ROOT/"build"/name)==digest
disk=OUT/"driver-test-disk.img"
assert disk.is_file() and disk.stat().st_size==4096*512
zig=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
efi=OUT/"BOOTX64-wrapper-fls.EFI"
source=ROOT/"experiments"/"block"/"boot.c"
subprocess.run([str(zig),"build-exe","-target","x86_64-uefi","-O","ReleaseSmall",
 "-fno-stack-check","-fno-stack-protector","-mno-red-zone",f"-femit-bin={efi}",
 "-fstrip",str(source)],check=True)
packer=ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py"
txt=packer.read_text(encoding="utf8")
assert txt.count("build=ROOT/'build'")==1
txt=txt.replace("build=ROOT/'build'","build=ROOT/'build'/'block-preview'")
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(txt,str(packer),"exec"),{"__file__":str(packer),"__name__":"__main__"})
iso=OUT/"winux-0.38-wrapper-fls-candidate.iso"
assert iso.is_file()
for name,digest in BASE.items():assert sha(ROOT/"build"/name)==digest
result={"passed":True,"source":"experiments/block/boot.c",
        "disk_sha256":sha(disk),"efi_sha256":sha(efi),
        "iso_sha256":sha(iso),"stable_preserved":True,
        "note":"Read-only PCI VirtIO legacy block driver after ExitBootServices"}
(OUT/"block-build.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,indent=2))
