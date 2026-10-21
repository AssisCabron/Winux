"""Reconcile NT_MISSING telemetry with the exact Windows NTDLL stub inventory.

Read-only over guest logs and REA evidence. Writes only a JSON report under build/.
Unknown or synthetic service IDs are not silently labeled Windows APIs.
"""
from pathlib import Path
import argparse
import json
import re
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tests"/"runners"))
from _paths import analysis_cmd_dir

BEGIN=re.compile(r"^NT_MISSING_BEGIN unique=(\d+) overflow=(\d+)$")
ENTRY=re.compile(r"^NT_MISSING_SERVICE (\d+) calls=(\d+)$")

def last_complete_snapshot(text):
    completed=[]
    active=None
    for line in text.splitlines():
        line=line.strip()
        head=BEGIN.fullmatch(line)
        if head:
            active={"declared_unique":int(head.group(1)),"overflow":int(head.group(2)),"items":[]}
            continue
        if active is None:
            continue
        item=ENTRY.fullmatch(line)
        if item:
            active["items"].append({"number":int(item.group(1)),"calls":int(item.group(2))})
        if line=="NT_MISSING_END":
            if len(active["items"])==active["declared_unique"]:
                completed.append(active)
            active=None
    return completed[-1] if completed else None

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--serial",type=Path,default=ROOT/"build"/"console-ntabi-p4-telemetry-serial.log")
    args=ap.parse_args()
    serial=args.serial if args.serial.is_absolute() else ROOT/args.serial
    trace=last_complete_snapshot(serial.read_text(encoding="utf-8",errors="replace"))
    if trace is None:
        raise RuntimeError("No complete NT_MISSING telemetry block in "+str(serial))
    contract=json.loads((ROOT/"abi"/"nt_services_19041.json").read_text(encoding="utf-8"))
    analysis=analysis_cmd_dir(ROOT)
    source=json.loads((analysis/"nt-stubs.json").read_text(encoding="utf-8"))
    assert contract["ntdll_sha256"]==source["sha256"],"NTDLL source/profile mismatch"
    inventory={}
    for e in source["exports"]:
        if e.get("status")!="recognized" or not e["name"].startswith("Nt"):
            continue
        number=e.get("service_number")
        if isinstance(number,int):
            inventory.setdefault(number,set()).add(e["name"])
    supported={item["number"] for item in contract["services"]}
    items=[]
    for event in trace["items"]:
        number=event["number"]
        names=sorted(inventory.get(number,[]))
        category=("recognized_but_not_dispatched" if names and number not in supported
                 else "already_dispatched" if number in supported
                 else "unrecognized_or_test_sentinel")
        items.append({**event,"names":names,"category":category})
    result={"source":"Winux guest QEMU serial NT_MISSING diagnostic",
            "ntdll_sha256":source["sha256"],"log":str(serial),
            "observed_unique":len(items),"overflow_records":trace["overflow"],
            "mapped":items,
            "limitations":["Counts come from a single-core CPL0 fixture",
                           "Only dispatch-default service IDs are recorded, never arguments",
                           "Unrecognized IDs cannot be mapped to a Windows API without additional evidence"]}
    out=ROOT/"build"/"nt-missing-cross-reference.json"
    out.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
if __name__=="__main__":main()
