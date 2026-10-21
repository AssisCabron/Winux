"""One-time safe move of host C fixtures under tests/host and shared test data under tests/fixtures."""
from pathlib import Path
import zipfile,re,os,json,ast
R=Path(__file__).resolve().parents[2]
T=R/"tests"
samples=["console_endpoint_host.c","nt_file_api_host.c","nt_file_host.c","nt_objects_host.c","nt_object_api_host.c","process_parameters_host.c"]
headers=["file_io_cases.h","nt_namespace_fixture.h"]
moves={T/name:T/"host"/name for name in samples}
moves.update({T/name:T/"fixtures"/name for name in headers})
assert all(src.exists() for src in moves) and all(not dst.exists() for dst in moves.values())
pat=re.compile(r'(^\s*#\s*include\s*")([^"]+)(")',re.MULTILINE)
c_sources=list((R/"src").rglob("*.h"))+list((R/"src").rglob("*.c"))+list((R/"apps").glob("*.c"))+list(T.glob("*.h"))+list(T.glob("*.c"))
c_edits={}
for original in c_sources:
    destination=moves.get(original,original)
    source=original.read_text(encoding="utf-8")
    def fix(m):
        old=(original.parent/m.group(2)).resolve()
        if not old.exists() and old not in moves:raise RuntimeError(f"missing include: {original} -> {m.group(2)}")
        new=moves.get(old,old)
        return m.group(1)+Path(os.path.relpath(new,destination.parent)).as_posix()+m.group(3)
    c_edits[original]=pat.sub(fix,source)
py_edits={}
for py in (T/"runners").glob("*.py"):
    old=py.read_text(encoding="utf-8");new=old
    for p,d in moves.items():
        new=new.replace("tests/"+p.name,"tests/"+d.relative_to(T).as_posix())
    if new!=old:
        ast.parse(new)
        py_edits[py]=new
snapshot=R/"_snapshots"/"winux-host-fixtures-prelayout.zip"
if snapshot.exists():raise RuntimeError("Fixture snapshot already exists")
with zipfile.ZipFile(snapshot,"x",compression=zipfile.ZIP_DEFLATED) as z:
    for f in c_sources:z.write(f,f.relative_to(R).as_posix())
    for f in py_edits:z.write(f,f.relative_to(R).as_posix())
print(f"BACKUP_OK {snapshot}")
for original,new in moves.items():
    new.parent.mkdir(parents=True,exist_ok=True)
    new.write_text(c_edits[original],encoding="utf-8")
for src,data in c_edits.items():
    if src not in moves and data!=src.read_text(encoding="utf-8"):
        src.write_text(data,encoding="utf-8")
for p,text in py_edits.items():p.write_text(text,encoding="utf-8")
for f in moves:f.unlink()
print(f"FIXTURES_ORGANIZED moved={len(moves)} runners_updated={len(py_edits)}")
