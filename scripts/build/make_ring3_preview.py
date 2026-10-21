"""Build separate UEFI/QEMU Ring 3 preview; never touch stable or P4 ISOs."""
from pathlib import Path
import subprocess,sys,hashlib,io,contextlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/"ring3-preview"
OUT.mkdir(exist_ok=True,parents=True)
zig=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
efi=OUT/"BOOTX64-wrapper-fls.EFI"
source=ROOT/"experiments"/"ring3"/"boot.c"
subprocess.run([str(zig),"build-exe","-target","x86_64-uefi","-O","ReleaseSmall",
 "-fno-stack-check","-fno-stack-protector","-mno-red-zone",f"-femit-bin={efi}",
 "-fstrip",str(source)],check=True)
packer=ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py"
txt=packer.read_text(encoding="utf8")
assert txt.count("build=ROOT/'build'")==1
txt=txt.replace("build=ROOT/'build'","build=ROOT/'build'/'ring3-preview'")
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(txt,str(packer),"exec"),{"__file__":str(packer),"__name__":"__main__"})
iso=OUT/"winux-0.38-wrapper-fls-candidate.iso"
assert iso.is_file()
for p in [efi,iso]:
    print(p.name,p.stat().st_size,hashlib.sha256(p.read_bytes()).hexdigest())
print("RING3_PREVIEW_BUILT",iso)
