"""Generate version-pinned NT dispatch IDs for the *existing* Winux NT gate.

This is NOT the independent userland ABI. Windows NTDLL service numbers are
version-bound and valid only for the exact SHA-256 in abi/nt_services_19041.json.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "abi" / "nt_services_19041.json"
OUTPUT = ROOT / "abi" / "generated" / "nt_services_19041.h"
SYSCALL = ROOT / "src" / "nt" / "nt_syscall.h"

def symbol(name: str) -> str:
    assert name.startswith("Nt")
    stem = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name[2:])
    return "WX_NT_" + stem.upper()

def read_manifest():
    obj = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert obj["schema"] == "winux.nt-service-profile"
    assert obj["schema_version"] == 1
    assert obj["execution_mode"] == "CPL0_fixture_only"
    assert obj["ntdll_sha256"] == "36b1a62e4bf6bf7d68458fff6e8a33b39f6527ec6a46e55ecdd12cde83894c8d"
    assert obj["default_extra_stack_span_bytes"] == 8
    records = obj["services"]
    ids = [item["number"] for item in records]
    names = [item["name"] for item in records]
    assert len(records) == 16 and len(set(ids)) == len(ids) and len(set(names)) == len(names)
    assert ids == sorted(ids)
    assert all(item["handler"] == "nt_" + item["name"][2:] for item in records)
    assert all(item["caller_stack_span_bytes"] in (8,48,56,80) for item in records)
    assert all(re.fullmatch(r"Nt[A-Za-z0-9]+", item["name"]) for item in records)
    return obj

def generated_header(obj):
    records = obj["services"]
    lines = [
        "/* Generated from abi/nt_services_19041.json; DO NOT EDIT.",
        " * Windows NT system call IDs: profile pinned to one specific NTDLL hash.",
        " * This is NOT the future native Winux ABI and is not a ring-3 gateway.",
        " */",
        "#ifndef WINUX_NT_SERVICES_19041_H",
        "#define WINUX_NT_SERVICES_19041_H",
        "enum wx_nt_service_id {",
    ]
    lines += [f"    {symbol(r['name'])} = {r['number']}u," for r in records]
    lines += [
        "};",
        "",
        "static unsigned wx_nt_stack_span(unsigned long long service) {",
        "    switch (service) {",
    ]
    for span in (80,56,48):
        entries = [r for r in records if r["caller_stack_span_bytes"] == span]
        for record in entries:
            lines.append(f"        case {symbol(record['name'])}:")
        if entries:
            lines.append(f"            return {span}u;")
    lines += [
        "        default: return 8u;",
        "    }",
        "}",
        "#endif",
        ""
    ]
    return "\n".join(lines)

def update_source(source: str, obj):
    records = obj["services"]
    by_num = {r["number"]:r for r in records}
    by_name = {r["name"]:r for r in records}
    # First generation only: confirm the actual dispatch cases before replacing.
    old_cases = re.findall(r"\bcase\s+(\d+)\s*:\s*(?:nt_syscall_counts\[\d+\]\+\+;\s*)?return\s+(nt_\w+)\s*\(",source)
    if old_cases:
        assert len(old_cases)==len(records),("dispatcher changed",old_cases)
        for number,handler in old_cases:
            assert int(number) in by_num and by_num[int(number)]["handler"] == handler
        def convert_case(m):
            number = int(m.group(1))
            assert number in by_num
            return "case " + symbol(by_num[number]["name"]) + ":"
        source = re.sub(r"\bcase\s+(\d+)\s*:", convert_case, source)
    else:
        for r in records:
            assert re.search(r"\bcase\s+"+symbol(r["name"])+r"\s*:", source)

    old_span = re.compile(r"    u64 extra = f\[0\] == 6 \|\| f\[0\] == 8.*?;\n",re.S)
    if "wx_nt_stack_span(f[0])" not in source:
        source,n = old_span.subn("    u64 extra = wx_nt_stack_span(f[0]);\n",source)
        assert n==1,n

    for name in ("NtAllocateVirtualMemory","NtFreeVirtualMemory","NtProtectVirtualMemory","NtQueryVirtualMemory"):
        entry = by_name[name]
        old = f'nt_checked_stub(m, "{name}", {entry["number"]})'
        new = f'nt_checked_stub(m, "{name}", {symbol(name)})'
        if old in source:
            assert source.count(old)==1
            source=source.replace(old,new)
        else:
            assert new in source
    include = '#include "../../abi/generated/nt_services_19041.h"'
    if include not in source:
        token='u8 nt_syscall_stack[65536]'
        assert source.count(token)==1
        source=source.replace(token,include+"\n"+token)
    assert len(re.findall(r"\bcase\s+WX_NT_",source))==len(records)
    return source

def run(check: bool):
    obj=read_manifest()
    expected_header=generated_header(obj)
    current=SYSCALL.read_text(encoding="utf-8")
    expected_source=update_source(current,obj)
    if check:
        assert OUTPUT.is_file() and OUTPUT.read_text(encoding="utf-8")==expected_header,"Generated header drift"
        assert current==expected_source,"Dispatcher drift"
        print("NT_ABI_VERIFIED: 16 active routes and exact NTDLL profile; no new native dispatcher")
        return
    snapshot=ROOT/"_snapshots"/"nt_syscall_pre_manifest_20261021.h"
    if not snapshot.exists():
        snapshot.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(SYSCALL,snapshot)
    OUTPUT.parent.mkdir(parents=True,exist_ok=True)
    OUTPUT.write_text(expected_header,encoding="utf-8")
    if current!=expected_source:
        SYSCALL.write_text(expected_source,encoding="utf-8")
    run(True)
    print("NT_ABI_GENERATED: existing service dispatch now uses generated ID and stack-span data")

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--check",action="store_true")
    args=ap.parse_args()
    run(args.check)
