"""Build P4 previews for independent CR3 spaces or cooperative kernel task switching."""
from pathlib import Path
import hashlib,subprocess,json,io,contextlib,argparse
cli=argparse.ArgumentParser()
cli.add_argument("--experiment",choices=("proc_space","scheduler","integration","preemption"),default="proc_space")
cli.add_argument("--vtd",action="store_true",help="Create separate Intel VT-d-required QEMU preview")
cli.add_argument("--user-pe",action="store_true",help="Build USER-PE64 isolated CPL3 loader preview")
cli.add_argument("--orbis-gui",action="store_true",help="Build experimental native Orbis OS graphical boot/login/desktop preview")
args=cli.parse_args()
EXPERIMENT=args.experiment
VTD=args.vtd
USER_PE=args.user_pe
ORBIS_GUI=args.orbis_gui
if sum((VTD,USER_PE,ORBIS_GUI))>1:cli.error("Use separate VT-d, user PE and GUI previews")
if (VTD or USER_PE or ORBIS_GUI) and EXPERIMENT!="integration":cli.error("Experimental preview requires integration")
OUT_NAME={"proc_space":"proc-space-preview","scheduler":"scheduler-preview","integration":"foundation-preview","preemption":"preemption-preview"}[EXPERIMENT]
if VTD:OUT_NAME="vtd-preview"
if USER_PE:OUT_NAME="user-pe-preview"
if ORBIS_GUI:OUT_NAME="orbis-gui-preview"
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/"build"/OUT_NAME
OUT.mkdir(exist_ok=True,parents=True)
if ORBIS_GUI:
    subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'scripts'/'build'/'generate_orbis_font.ps1')],check=True)
    subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'scripts'/'build'/'generate_orbis_rgb888.ps1')],check=True)
    subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'scripts'/'build'/'generate_orbis_icons.ps1')],check=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
GOLD={
"winux-0.38-x64.iso":"700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0",
"winux-0.38-wrapper-fls-candidate.iso":"4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77"
}
for f,v in GOLD.items():assert sha(ROOT/"build"/f)==v
zig=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
efi=OUT/"BOOTX64-wrapper-fls.EFI"
sources=[str(ROOT/"experiments"/EXPERIMENT/"boot.c")]
if EXPERIMENT in ("scheduler","integration","preemption"):
    # Standalone assembly owns the register and stack switch contract.
    asm=ROOT/"src"/"arch"/"x86_64"/"context_switch.S"
    obj=OUT/"context_switch.obj"
    # Transient COFF artifact; remove before recompilation to avoid Windows
    # antivirus/file-sharing conflicts while replacing an existing .obj.
    if obj.exists():obj.unlink()
    subprocess.run([str(zig),"build-obj","-target","x86_64-uefi","-O","ReleaseSmall",
                    f"-femit-bin={obj}",str(asm)],check=True)
    sources.append(str(obj))
if ORBIS_GUI:
    wall=OUT/"wallpaper.rgb"
    assert wall.stat().st_size==1280*800*3
    asm_src=OUT/"orbis_wallpaper.S"
    asm_src.write_text('.section .rdata,"dr"\n.global orbis_wall_rgb\norbis_wall_rgb:\n.incbin "'+wall.as_posix()+'"\n',encoding="ascii")
    wall_obj=OUT/"orbis_wallpaper.obj"
    if wall_obj.exists():wall_obj.unlink()
    subprocess.run([str(zig),"build-obj","-target","x86_64-uefi","-O","ReleaseSmall",
                    f"-femit-bin={wall_obj}",str(asm_src)],check=True)
    sources.append(str(wall_obj))
    icons=OUT/"orbis-icons-atlas.bgra"
    assert icons.stat().st_size==512*256*4
    icons_asm=OUT/"orbis_icons.S"
    icons_asm.write_text('# atlas-sha256 '+sha(icons)+'\n.section .rdata,"dr"\n.global orbis_icon_bgra\norbis_icon_bgra:\n.incbin "'+icons.as_posix()+'"\n',encoding="ascii")
    icons_obj=OUT/"orbis_icons.obj"
    if icons_obj.exists():icons_obj.unlink()
    subprocess.run([str(zig),"build-obj","-target","x86_64-uefi","-O","ReleaseSmall",
                    f"-femit-bin={icons_obj}",str(icons_asm)],check=True)
    sources.append(str(icons_obj))
subprocess.run([str(zig),"build-exe","-target","x86_64-uefi","-O","ReleaseSmall",
 "-fno-stack-check","-fno-stack-protector","-mno-red-zone",f"-femit-bin={efi}",
 "-fstrip",*(["-DWINUX_INTEL_VTD_PREVIEW"] if VTD else []),
 *(["-DWINUX_USER_PE_PREVIEW"] if USER_PE else []),
 *(["-DWINUX_ORBIS_GUI_PREVIEW"] if ORBIS_GUI else []),*sources],check=True)
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
        "warning":("Preview only: IRQ0 stays installed for the foundation guest session; single CPU, no general process/thread scheduler, SMP or user sessions"
                   if EXPERIMENT=="integration" else
                   "Preview only: bounded isolated scheduler experiments; no general process/thread scheduler, SMP or user sessions")}
(OUT/f"{OUT_NAME}-build.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,indent=2))
