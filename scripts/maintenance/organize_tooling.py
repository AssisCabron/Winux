"""Organize Windows probes, regression runners and build tooling preserving project root semantics."""
from pathlib import Path
import json,zipfile,ast,re,sys,subprocess

ROOT=Path(__file__).resolve().parents[2]
dest={}
for name in ("prepare_assets.py","prepare_windows.py","finalize_dlls.py","make_iso.py"):
    dest[ROOT/name]=ROOT/"scripts"/"build"/name
for file in ROOT.glob("make_iso_*candidate.py"):
    dest[file]=ROOT/"experiments"/"packaging"/file.name
for file in ROOT.glob("test_*.py"):
    dest[file]=ROOT/"tests"/"runners"/file.name
for file in ROOT.glob("probe_*.py"):
    dest[file]=ROOT/"tests"/"probes"/file.name
for file in ROOT.glob("check_*.py"):
    dest[file]=ROOT/"tests"/"checks"/file.name
dest[ROOT/"refactor_layout.py"]=ROOT/"scripts"/"maintenance"/"refactor_layout.py"
dest[ROOT/"organize_tooling.py"]=ROOT/"scripts"/"maintenance"/"organize_tooling.py"
assert len(dest)>45,len(dest)
for old in dest:
    if not old.is_file(): raise RuntimeError(f"missing file {old}")
for new in dest.values():
    if new.exists(): raise RuntimeError(f"destination exists {new}")
files={}
for old,new in dest.items():
    s=old.read_text(encoding="utf-8")
    s=s.replace("Path(__file__).resolve().parents[2]","Path(__file__).resolve().parents[2]s[2]")
    if new.parent.name=="runners":
        # Make independent checkers importable before any checker import.
        s="import sys as _layout_sys\nfrom pathlib import Path as _layout_Path\n_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[2]s[1] / 'checks'))\n"+s
    if old.name in ("refactor_layout.py","organize_tooling.py"):
        # One-time scripts are archived. Their own ROOT definitions follow the new path.
        pass
    ast.parse(s,filename=str(new))
    files[old]=(new,s)
build=ROOT/"build.ps1"
b=build.read_text(encoding="utf-8")
for name in ("finalize_dlls.py","prepare_assets.py","make_iso.py"):
    b=b.replace(f"$PSScriptRoot/{name}",f"$PSScriptRoot/scripts/build/{name}")
# Python imports prepare_windows from the same relocated build directory.
assert "scripts/build/prepare_assets.py" in b and "src/kernel/boot.c" in b
# Preserve original tooling before modifying its location.
snapshot=ROOT/"_snapshots"/"winux-pre-tooling-20261021.zip"
if snapshot.exists():raise RuntimeError("tooling backup exists, refuse to overwrite")
with zipfile.ZipFile(snapshot,"x",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for old in dest:z.write(old,old.relative_to(ROOT).as_posix())
    z.writestr("build.ps1.original",build.read_bytes())
print(f"TOOLING_BACKUP_OK {snapshot} count={len(dest)}")
for old,(new,s) in files.items():
    new.parent.mkdir(parents=True,exist_ok=True)
    new.write_text(s,encoding="utf-8")
# Syntax check generated Python before deleting original entries.
for old,(new,s) in files.items():
    compile(s,str(new),"exec")
build.write_text(b,encoding="utf-8")
for old in files:old.unlink()
report={"count":len(dest),"map":{str(old.relative_to(ROOT)).replace('\\','/'):str(new.relative_to(ROOT)).replace('\\','/') for old,(new,_) in files.items()}}
(ROOT/"_snapshots"/"tooling-layout.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(f"TOOLING_ORGANIZED_OK moved={len(dest)}")
