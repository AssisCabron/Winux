import sys as _layout_sys
from pathlib import Path as _layout_Path
_layout_sys.path.insert(0, str(_layout_Path(__file__).resolve().parents[1] / 'checks'))
"""Compile the original object core and compare it with retained host NT results.

This is a host test of core code, not a guest or syscall integration test.
"""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    zig = ROOT / "tools/zig-x86_64-windows-0.14.1/zig.exe"
    source = ROOT / "tests/host/nt_objects_host.c"
    exe = ROOT / "build/nt_objects_host.exe"
    subprocess.run([str(zig), "cc", "-target", "x86_64-windows-gnu", "-std=c11",
                    "-Wall", "-Wextra", "-Werror", "-O2", str(source), "-o", str(exe)],
                   check=True, timeout=180)
    observed = json.loads((ROOT / "build/nt-events-windows.json").read_text(encoding="utf-8"))
    namespace = json.loads((ROOT / "build/nt-namespace-windows.json").read_text(encoding="utf-8"))
    target = next(r["value"] for r in namespace["records"] if r.get("value"))
    run = subprocess.run([str(exe), target], capture_output=True, text=True, check=True, timeout=10)
    all_rows = [json.loads(line) for line in run.stdout.splitlines() if line]
    actual = [r for r in all_rows if not r.get("namespace")]
    namespace_rows = [{k: v for k, v in r.items() if k != "namespace"}
                      for r in all_rows if r.get("namespace")]
    if actual != observed["records"]:
        raise AssertionError("Object core diverges from retained Windows event observations")
    if namespace_rows != namespace["records"]:
        raise AssertionError("Namespace core diverges from retained Windows observations")
    adapter_source = ROOT / "tests/host/nt_object_api_host.c"
    adapter_exe = ROOT / "build/nt_object_api_host.exe"
    subprocess.run([str(zig), "cc", "-target", "x86_64-windows-gnu", "-std=c11",
                    "-Wall", "-Wextra", "-Werror", "-O2", str(adapter_source), "-o", str(adapter_exe)],
                   check=True, timeout=180)
    adapter_run = subprocess.run([str(adapter_exe)], capture_output=True, text=True, check=True, timeout=10)
    result = dict(scope="Host-compiled original core only; not integrated into ISO",
                  passed=True, matched_records=len(actual),
                  matched_namespace_records=len(namespace_rows),
                  reference_ntdll_sha256=observed["ntdll_sha256"],
                  core_sha256=hashlib.sha256((ROOT / "src/nt/objects/nt_objects.h").read_bytes()).hexdigest(),
                  test_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  namespace_fixture_sha256=hashlib.sha256((ROOT / "tests/fixtures/nt_namespace_fixture.h").read_bytes()).hexdigest(),
                  adapter_sha256=hashlib.sha256((ROOT / "src/nt/objects/nt_object_api.h").read_bytes()).hexdigest(),
                  adapter_test_sha256=hashlib.sha256(adapter_source.read_bytes()).hexdigest(),
                  adapter_result=adapter_run.stdout.strip(),
                  invariants=run.stderr.strip(),
                  limitations=["Polling only; blocking/alertable waits explicitly unsupported.",
                               "Only KnownDlls/KnownDllPath namespace; pointer validation uses a host arena fixture, not guest VM pages.",
                               "No syscall transition, concurrent access or scheduler tested."])
    (ROOT / "build/nt-objects-host-test.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
