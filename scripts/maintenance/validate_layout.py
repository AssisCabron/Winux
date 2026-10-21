"""Validate source layout and quoted C includes without modifying source or ISO."""
from pathlib import Path
import ast,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2]
roots=[ROOT/"src",ROOT/"apps",ROOT/"tests",ROOT/"experiments"/"candidates"]
quoted=re.compile(r'^\s*#\s*include\s*"([^"]+)"',re.MULTILINE)
missing=[]
includes=0
sources=[]
for folder in roots:
    for file in folder.rglob("*"):
        if file.suffix not in {".c",".h"}:continue
        sources.append(file)
        for include in quoted.findall(file.read_text(encoding="utf-8")):
            includes+=1
            if not (file.parent/include).resolve().is_file():missing.append({"source":str(file.relative_to(ROOT)),"include":include})
errors=[]
py_count=0
for folder in [ROOT/"scripts",ROOT/"tests",ROOT/"experiments"]:
    for file in folder.rglob("*.py"):
        if "__pycache__" in file.parts:continue
        py_count+=1
        try:ast.parse(file.read_text(encoding="utf-8"),filename=str(file))
        except SyntaxError as exc:errors.append({"file":str(file.relative_to(ROOT)),"error":str(exc)})
images=["winux-0.38-x64.iso","winux-0.38-heap-policy-candidate.iso","winux-0.38-heap-policy-alloc-candidate.iso","winux-0.38-wrapper-realloc-candidate.iso","winux-0.38-wrapper-fls-candidate.iso"]
hashes={}
for name in images:
    path=ROOT/"build"/name
    if not path.exists(): errors.append({"missing_iso":name})
    else:hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
out={"passed":not missing and not errors,"source_files":len(sources),"checked_quoted_includes":includes,"python_files":py_count,"missing_includes":missing,"syntax_errors":errors,"isos":hashes,"tooling_snapshots":[str(f.name) for f in (ROOT/"_snapshots").glob("*.zip")]}
(ROOT/"build"/"layout-audit.json").write_text(json.dumps(out,indent=2)+"\n",encoding="utf-8")
print(json.dumps({k:v for k,v in out.items() if k not in ("isos","syntax_errors","missing_includes")},indent=2))
print("ISOS",json.dumps(hashes,indent=2))
if not out["passed"]:print("ERRORS",missing,errors);raise SystemExit(1)
