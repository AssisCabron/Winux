"""Safely regenerate stable and P4 bootable ISO layouts under build/layout-verification only."""
from pathlib import Path
import shutil,hashlib,io,sys,contextlib
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/"build"/"layout-verification"
WORK.mkdir(parents=True,exist_ok=True)
ROOT_BUILD=ROOT/"build"
configs=[
    (ROOT/"scripts"/"build"/"make_iso.py","BOOTX64.EFI","BOOTX64-stable.EFI","winux-0.38-x64.iso"),
    (ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py","BOOTX64-wrapper-fls.EFI","BOOTX64-p4.EFI","winux-0.38-wrapper-fls-candidate.iso"),
]
def digest(p):
    block=hashlib.sha256()
    with p.open('rb') as f:
        while data:=f.read(1024*1024):block.update(data)
    return block.hexdigest()
for packager,efi_name,rebuild_name,iso_name in configs:
    assert packager.exists() and (WORK/rebuild_name).exists(),packager
    shutil.copyfile(WORK/rebuild_name,WORK/efi_name)
    previous=ROOT_BUILD/efi_name
    assert digest(previous)==digest(WORK/efi_name),"EFI differs from previous validated code"
    source=packager.read_text(encoding="utf-8")
    assert source.count("build=ROOT/'build'")==1
    code=source.replace("build=ROOT/'build'","build=ROOT/'build'/'layout-verification'")
    context={"__file__":str(packager),"__name__":"__main__"}
    with contextlib.redirect_stdout(io.StringIO()) as stdout:
        exec(compile(code,str(packager),"exec"),context)
    generated=WORK/iso_name
    assert generated.exists() and generated.stat().st_size>30_000_000
    import pycdlib
    p=pycdlib.PyCdlib();p.open(str(generated))
    extracted=io.BytesIO();p.get_file_from_iso_fp(extracted,iso_path='/ESP.IMG;1')
    assert len(extracted.getvalue())==61440*512
    assert p.eltorito_boot_catalog.validation_entry.platform_id==0xef
    p.close()
    print(f"PACKAGE_VERIFIED {iso_name} size={generated.stat().st_size} sha256={digest(generated)}")
print("NO_ORIGINAL_ISOS_OVERWRITTEN")
