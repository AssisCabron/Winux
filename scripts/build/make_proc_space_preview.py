"""Build P4 previews for independent CR3 spaces or cooperative kernel task switching."""
from pathlib import Path
import hashlib,subprocess,json,io,contextlib,argparse
cli=argparse.ArgumentParser()
cli.add_argument("--experiment",choices=("proc_space","scheduler","integration"),default="proc_space")
EXPERIMENT=cli.parse_args().experiment
OUT_NAME={"proc_space":"proc-space-preview","scheduler":"scheduler-preview","integration":"foundation-preview"}[EXPERIMENT]
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/OUT_NAME
OUT.mkdir(exist_ok=True,parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
GOLD={
"winux-0.38-x64.iso":"700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0",
"winux-0.38-wrapper-fls-candidate.iso":"4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77"
}
for f,v in GOLD.items():assert sha(ROOT/"build"/f)==v
zig=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
efi=OUT/"BOOTX64-wrapper-fls.EFI"
sources=[str(ROOT/"experiments"/EXPERIMENT/"boot.c")]
if EXPERIMENT in ("scheduler","integration"):
    # Standalone assembly owns the register and stack switch contract.
    asm=ROOT/"src"/"arch"/"x86_64"/"context_switch.S"
    obj=OUT/"context_switch.obj"
    subprocess.run([str(zig),"build-obj","-target","x86_64-uefi","-O","ReleaseSmall",
                    f"-femit-bin={obj}",str(asm)],check=True)
    sources.append(str(obj))
subprocess.run([str(zig),"build-exe","-target","x86_64-uefi","-O","ReleaseSmall",
 "-fno-stack-check","-fno-stack-protector","-mno-red-zone",f"-femit-bin={efi}",
 "-fstrip",*sources],check=True)
pack=ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py"
text=pack.read_text(encoding="utf8")
assert text.count("build=ROOT/'build'")==1
text=text.replace("build=ROOT/'build'",f"build=ROOT/'build'/'{OUT_NAME}'")
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(text,str(pack),"exec"),{"__file__":str(pack),"__name__":"__main__"})
iso=OUT/"winux-0.38-wrapper-fls-candidate.iso"
assert iso.is_file() and iso.stat().st_size>30_000_000
for f,v in GOLD.items():assert sha(ROOT/"build"/f)==v
result={"passed":True,"efi_sha256":sha(efi),"iso_sha256":sha(iso),
        "source":f"experiments/{EXPERIMENT}/boot.c","stable_isos_unchanged":True,
        "warning":"Preview only: independent CR3 spaces and optionally cooperative kernel-task scheduling; no preemptive Ring3 scheduler or sessions"}
(OUT/f"{OUT_NAME}-build.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,indent=2))
