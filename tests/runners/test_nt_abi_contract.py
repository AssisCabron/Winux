"""Host test: generated ABI is up-to-date, matches local NTDLL and compiled consumers."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
from _paths import analysis_cmd_dir
MANIFEST=ROOT/"abi"/"nt_services_19041.json"
GEN=ROOT/"scripts"/"build"/"generate_nt_abi.py"
ZIG=ROOT/"tools"/"zig-x86_64-windows-0.14.1"/"zig.exe"
SOURCE=ROOT/"tests"/"host"/"nt_abi_contract_host.c"
OUTPUT=ROOT/"build"/"nt-abi-contract-host.exe"

def run():
    data=json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert data["status"]=="partial_routes_only"
    assert data["execution_mode"]=="CPL0_fixture_only"
    assert data["schema_version"]==1
    assert len(data["services"])==16
    actual=hashlib.sha256(Path("C:/Windows/System32/ntdll.dll").read_bytes()).hexdigest()
    assert actual==data["ntdll_sha256"], "Windows NTDLL version differs; stop using this profile"
    evidence=json.loads((analysis_cmd_dir(ROOT)/'nt-stubs.json').read_text(encoding='utf-8'))
    assert evidence['sha256']==actual, 'REA source differs from active NTDLL'
    observed={e['name']:e['service_number'] for e in evidence['exports'] if e.get('status')=='recognized' and isinstance(e.get('service_number'),int)}
    for entry in data['services']:
        assert observed.get(entry['name'])==entry['number'], ('NT DLL service drift',entry['name'])
    subprocess.run([sys.executable,str(GEN),"--check"],check=True,capture_output=True)
    subprocess.run([str(ZIG),"build-exe","-target","x86_64-windows","-O","ReleaseSmall",
       "-fstrip","-lc",f"-femit-bin={OUTPUT}",str(SOURCE)],check=True,capture_output=True)
    child=subprocess.run([str(OUTPUT)],capture_output=True,text=True,timeout=15)
    assert child.returncode==0, f"ABI host binary contract mismatch: {child.returncode}"
    result={"passed":True,"service_count":16,"ntdll_sha256":actual,
        "generated_header_compiled":True,"kernel_dispatch_profile":"CPL0 fixtures only",
        "nt_missing_ledger_scope":"unknown IDs only; no arguments",
        "checks":["manifest version/identity","16 original NTDLL stub IDs from the REA evidence","existing dispatcher 16 routes","host-compiled names and argument spans"]}
    (ROOT/"build"/"nt-abi-contract-host-test.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
if __name__=="__main__":run()
