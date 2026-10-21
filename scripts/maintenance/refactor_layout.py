"""One-time, backed-up Winux source-layout refactor. No kernel semantics changed."""
from pathlib import Path
import os, re, sys, hashlib, zipfile, json

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/"src"
MODULES={
 "boot.c":"kernel/boot.c",
 "console.h":"console/console.h",
 "console_line.h":"console/console_line.h",
 "hardware.h":"arch/x86_64/hardware.h",
 "paging.h":"arch/x86_64/paging.h",
 "paging_test.h":"tests/memory/paging_test.h",
 "shell.h":"shell/shell.h",
 "memory_limits.h":"mm/memory_limits.h",
 "vm_regions.h":"mm/vm_regions.h",
 "nt_memory.h":"mm/nt_memory.h",
 "pe_loader.h":"loader/pe_loader.h",
 "pe_modules.h":"loader/pe_modules.h",
 "pe_tls.h":"loader/pe_tls.h",
 "process_context.h":"process/process_context.h",
 "process_parameters.h":"process/process_parameters.h",
 "nt_native_context.h":"process/nt_native_context.h",
 "win_runtime.h":"runtime/win_runtime.h",
 "kb_inputs_context.h":"runtime/windows/kb_inputs_context.h",
 "kb_nls_context.h":"runtime/windows/kb_nls_context.h",
 "nt_syscall.h":"nt/nt_syscall.h",
 "nt_info.h":"nt/nt_info.h",
 "nt_objects.h":"nt/objects/nt_objects.h",
 "nt_object_api.h":"nt/objects/nt_object_api.h",
 "nt_file_api.h":"nt/io/nt_file_api.h",
 "nt_file_core.h":"nt/io/nt_file_core.h",
 "nt_console_context.h":"nt/io/nt_console_context.h",
}
test_groups={
 "nt_console_test.h":"console","nt_file_test.h":"io",
 "nt_fls_test.h":"runtime","nt_heap_bootstrap_test.h":"memory",
 "nt_image_test.h":"loader","nt_info_test.h":"process",
 "nt_lock_test.h":"runtime","nt_native_context_test.h":"process",
 "nt_nls_test.h":"runtime","nt_object_test.h":"objects",
 "nt_partial_test.h":"memory","nt_process_parameters_test.h":"process",
 "nt_startup_test.h":"runtime","kb_codepage_test.h":"runtime",
 "process_context_test.h":"process",
}
for name,group in test_groups.items(): MODULES[name]=f"tests/{group}/{name}"
candidates={
 "boot_heap_policy_candidate.c":("p1","boot.c"),
 "shell_heap_policy_candidate.h":("p1","shell.h"),
 "nt_startup_heap_policy_candidate.h":("p1","startup_test.h"),
 "boot_heap_policy_alloc_candidate.c":("p2","boot.c"),
 "shell_heap_policy_alloc_candidate.h":("p2","shell.h"),
 "nt_startup_heap_policy_alloc_candidate.h":("p2","startup_test.h"),
 "boot_wrapper_realloc_candidate.c":("p3","boot.c"),
 "shell_wrapper_realloc_candidate.h":("p3","shell.h"),
 "nt_startup_wrapper_realloc_candidate.h":("p3","startup_test.h"),
 "boot_wrapper_fls_candidate.c":("p4","boot.c"),
 "shell_wrapper_fls_candidate.h":("p4","shell.h"),
 "nt_startup_wrapper_fls_candidate.h":("p4","startup_test.h"),
}
for name,(variant,basename) in candidates.items():
    MODULES[name]=f"../experiments/candidates/{variant}/{basename}"
origins={SRC/name: (SRC/new).resolve() for name,new in MODULES.items()}
pattern=re.compile(r'(^\s*#\s*include\s*")([^"]+)(")',re.MULTILINE)
def digest(blob):return hashlib.sha256(blob).hexdigest()
def norm(path):return path.resolve(strict=False)
def rewritten(path,contents):
    dest=origins.get(path,path)
    def alter(match):
        candidate=norm(path.parent/match.group(2))
        final=origins.get(candidate,candidate)
        if not candidate.is_file() and candidate not in origins:
            # Only angle-bracket system headers may be unresolved, quoted headers must exist.
            raise RuntimeError(f"Unresolved include {path}: {match.group(2)}")
        inc=Path(os.path.relpath(final,dest.parent)).as_posix()
        return match.group(1)+inc+match.group(3)
    return pattern.sub(alter,contents)
src_names={p.name for p in SRC.iterdir() if p.is_file()}
if set(MODULES)!=src_names:
    raise RuntimeError(f"Unhandled source files: {sorted(src_names-set(MODULES))}; missing {sorted(set(MODULES)-src_names)}")

all_c=[*SRC.glob('*.c'),*SRC.glob('*.h'),* (ROOT/"apps").glob("*.c"),*(ROOT/"tests").glob("*.c"),*(ROOT/"tests").glob("*.h")]
changes={}
for f in all_c:
    original=f.read_text(encoding="utf-8")
    changes[f]=rewritten(f,original)
# Only direct path references in host tests, no blind replacement in implementation comments.
python_edits={}
for f in ROOT.glob("test_*.py"):
    data=f.read_text(encoding="utf-8")
    for name,target in MODULES.items():
        if name in candidates:continue
        data=data.replace(f"src/{name}",f"src/{target}")
    if data!=f.read_text(encoding="utf-8"): python_edits[f]=data
build=ROOT/"build.ps1"
build_text=build.read_text(encoding="utf-8").replace('src/boot.c','src/kernel/boot.c')

if len(sys.argv)>1 and sys.argv[1]=="--plan":
    print(json.dumps({"sources":len(MODULES),"c_files_with_includes":len(all_c),"python_scripts_adjusted":[f.name for f in python_edits],"backup":"_snapshots/winux-pre-refactor-source.zip","destinations":list(MODULES.values())},indent=2));sys.exit(0)
if len(sys.argv)>1 and sys.argv[1]!="--apply":raise SystemExit("Use --plan or --apply")
snapshot=ROOT/"_snapshots"/"winux-pre-refactor-source.zip"
snapshot.parent.mkdir(exist_ok=True)
if snapshot.exists():raise RuntimeError("Snapshot already exists; refuse to overwrite or rerun")
files=[f for f in ROOT.rglob("*") if f.is_file() and not any(segment in {"build","tools","__pycache__",".idea","_snapshots"} for segment in f.relative_to(ROOT).parts) and f.suffix.lower() in {".h",".c",".py",".ps1",".md",".def",".txt",".gitignore",".wxs"}]
with zipfile.ZipFile(snapshot,"x",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for file in files:z.write(file,file.relative_to(ROOT).as_posix())
print(f"BACKUP_OK {snapshot} ({len(files)} files, {snapshot.stat().st_size} bytes)")
# Copy to new destinations first, then verify complete relocation before removing originals.
for old,dest in origins.items():
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(changes[old],encoding="utf-8")
for f in all_c:
    if f not in origins and f.read_text(encoding="utf-8")!=changes[f]:
        f.write_text(changes[f],encoding="utf-8")
for f,text in python_edits.items():f.write_text(text,encoding="utf-8")
build.write_text(build_text,encoding="utf-8")
for old,dest in origins.items():
    assert dest.exists() and old!=dest
    old.unlink()
assert not list(SRC.glob("*.*")),"Root src contains unexpected files"
# Legacy one-off generators referred to old layout; park them as evidence only.
archive=ROOT/"experiments"/"historical_generators"
archive.mkdir(parents=True,exist_ok=True)
for f in ROOT.glob("prepare_*candidate*.py"):
    f.rename(archive/f.name)
(archive/"README.md").write_text(
    "# Historical one-off candidate generators\n\n"
    "These scripts were used before the source-layout refactor and refer to the old flat src directory.\n"
    "They are archived as evidence. Build current candidates from preserved C sources under ../candidates.\n"
    "Do not run these on the new project layout.\n",encoding="utf-8")
manifest={"snapshot":str(snapshot),"old_source_count":len(MODULES),"paths":{str(k.relative_to(ROOT)):str(v.relative_to(ROOT)).replace('\\','/') for k,v in origins.items()},"python_adjusted":[f.name for f in python_edits]}
(ROOT/"_snapshots"/"refactor-manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
print(f"REFACTORED_OK: relocated {len(MODULES)} sources; adjusted {len(python_edits)} python files")
