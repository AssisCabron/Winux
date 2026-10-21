"""Create isolated RAMFS+UEFI ISO for the custom Windows DLL fixture.

Historical build/{ramfs.h,BOOTX64.EFI,*.iso} remain unchanged. No new loader.
"""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/"build"
OUT=BUILD/"plugin-preview"
ORIGINAL_ISOS={
    "winux-0.38-x64.iso":"700e6a38c0b7dbe312b6012810728af8ecd24a984cd8287e0f644a73f1c343b0",
    "winux-0.38-wrapper-fls-candidate.iso":"4d1e20074e35fb9b03bbcb3043180cac39c08cd8c5536efc784e48a7e9270c77",
}
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def verify_originals():
    for name, digest in ORIGINAL_ISOS.items():
        assert sha(BUILD/name)==digest, f"Original ISO overwritten: {name}"

def embed():
    OUT.mkdir(exist_ok=True,parents=True)
    old=BUILD/"ramfs.h"
    data=old.read_text(encoding="utf-8")
    manifest=json.loads((BUILD/"ramfs-manifest.json").read_text(encoding="utf-8"))
    assert len(manifest)<30 and len({f["name"].lower() for f in manifest})==len(manifest)
    names=[item["name"] for item in manifest]
    assert "plugintest.exe" not in names and "winuxplugin.dll" not in names
    extras=[OUT/"plugintest.exe",OUT/"winuxplugin.dll"]
    assert all(file.is_file() and file.stat().st_size>1000 for file in extras)
    count=len(manifest)
    insert=data.rfind("static const ram_file ram_files[] = {")
    assert insert>0
    close=data.find("};",insert)
    assert close>insert
    beginning=data[insert:close+2]
    assert beginning.startswith("static const ram_file ram_files[] = {")
    assert beginning.endswith("};")
    arrays=[]
    items=[]
    for i,file in enumerate(extras,count):
        blob=file.read_bytes()
        assert blob.startswith(b"MZ")
        arrays.append(f"static const u8 ram_data_{i}[] = {{" + ",".join(str(v) for v in blob) + ",0};")
        items.append("{"+json.dumps(file.name)+f",ram_data_{i},{len(blob)}"+"}")
        manifest.append({"name":file.name,"source":str(file.resolve()),"size":len(blob),"sha256":sha(file)})
    assert sum(entry["size"] for entry in manifest)<16*1024*1024
    new_table=beginning[:-2]+","+",".join(items)+"};"
    new_source=data[:insert]+"\n".join(arrays)+"\n"+new_table+data[close+2:]
    old_count=f"#define RAM_FILE_COUNT {count}"
    assert new_source.count(old_count)==1
    new_source=new_source.replace(old_count,f"#define RAM_FILE_COUNT {count+2}")
    (OUT/"ramfs.h").write_text(new_source,encoding="utf-8")
    (OUT/"ramfs-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    return count+2

def compile_efi():
    zig=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
    efi=OUT/"BOOTX64-wrapper-fls.EFI"
    args=[str(zig),"build-exe","-target","x86_64-uefi","-O","ReleaseSmall",
      "-fno-stack-check","-fno-stack-protector","-mno-red-zone",
      f"-femit-bin={efi}","-fstrip",str(ROOT/"experiments"/"plugin"/"boot.c")]
    subprocess.run(args,check=True)
    assert efi.read_bytes()[:2]==b"MZ"
    return efi

def pack_reusing_original():
    script=ROOT/"experiments"/"packaging"/"make_iso_wrapper_fls_candidate.py"
    code=script.read_text(encoding="utf-8")
    assert code.count("build=ROOT/'build'")==1
    code=code.replace("build=ROOT/'build'","build=ROOT/'build'/'plugin-preview'")
    context={"__file__":str(script),"__name__":"__main__"}
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile(code,str(script),"exec"),context)
    iso=OUT/"winux-0.38-wrapper-fls-candidate.iso"
    assert iso.is_file() and iso.stat().st_size>30_000_000
    return iso

def main():
    verify_originals()
    count=embed()
    efi=compile_efi()
    iso=pack_reusing_original()
    verify_originals()
    result={
        "passed":True,
        "variant":"Winux P4 + custom DLL fixture, CPL0 trusted test only",
        "files_in_ramfs":count,
        "dll_sha256":sha(OUT/"winuxplugin.dll"),
        "exe_sha256":sha(OUT/"plugintest.exe"),
        "efi_sha256":sha(efi),
        "iso_sha256":sha(iso),
        "original_isos_unchanged":True
    }
    (OUT/"plugin-build.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
if __name__=="__main__":main()
