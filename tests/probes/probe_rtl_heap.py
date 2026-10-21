"""Batch reference of private Rtl heaps in this process; no foreign memory.
Not evidence that Microsoft heap code runs in the guest.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path


def main():
    n = C.WinDLL("ntdll")
    specs = {
        "RtlCreateHeap": (C.c_void_p, [C.c_uint32, C.c_void_p, C.c_size_t, C.c_size_t, C.c_void_p, C.c_void_p]),
        "RtlAllocateHeap": (C.c_void_p, [C.c_void_p, C.c_uint32, C.c_size_t]),
        "RtlReAllocateHeap": (C.c_void_p, [C.c_void_p, C.c_uint32, C.c_void_p, C.c_size_t]),
        "RtlSizeHeap": (C.c_size_t, [C.c_void_p, C.c_uint32, C.c_void_p]),
        "RtlFreeHeap": (C.c_ubyte, [C.c_void_p, C.c_uint32, C.c_void_p]),
        "RtlDestroyHeap": (C.c_void_p, [C.c_void_p]),
    }
    for name, (ret, args) in specs.items():
        fn = getattr(n, name); fn.restype = ret; fn.argtypes = args
    rows = []
    for flags in (0, 2):
        heap = n.RtlCreateHeap(flags, None, 0x100000, 0x1000, None, None)
        rows.append(dict(operation="create", flags=flags, success=bool(heap)))
        if not heap:
            raise RuntimeError("Private heap creation failed")
        try:
            for size in (0, 1, 15, 16, 4096, 65536, 3*1024*1024):
                block = n.RtlAllocateHeap(heap, 8, size)
                if not block:
                    rows.append(dict(operation="allocate", flags=flags, size=size, success=False))
                    continue
                try:
                    zeroed = all(v == 0 for v in C.string_at(block, size))
                    rows.append(dict(operation="allocate", flags=flags, size=size,
                                     aligned16=block % 16 == 0, zeroed=zeroed,
                                     reported_size=n.RtlSizeHeap(heap, 0, block)))
                finally:
                    rows.append(dict(operation="free", flags=flags, size=size,
                                     success=bool(n.RtlFreeHeap(heap, 0, block))))
            block = n.RtlAllocateHeap(heap, 0, 32)
            if not block:
                raise RuntimeError("Reallocation source failed")
            try:
                C.memset(block, 0x5a, 32)
                grown = n.RtlReAllocateHeap(heap, 8, block, 8192)
                if not grown:
                    raise RuntimeError("Reallocation failed")
                block = grown
                content = C.string_at(block, 8192)
                rows.append(dict(operation="reallocate", flags=flags,
                                 preserved=content[:32] == b"Z"*32,
                                 zero_tail=content[32:] == bytes(8192-32),
                                 reported_size=n.RtlSizeHeap(heap, 0, block)))
            finally:
                rows.append(dict(operation="free_reallocated", flags=flags,
                                 success=bool(n.RtlFreeHeap(heap, 0, block))))
            rows.append(dict(operation="free_null", flags=flags,
                             success=bool(n.RtlFreeHeap(heap, 0, None))))
        finally:
            rows.append(dict(operation="destroy", flags=flags,
                             success=n.RtlDestroyHeap(heap) is None))
    for r in rows:
        if r["operation"] == "allocate" and r.get("success") is False:
            continue
        if any(r.get(key) is False for key in ("success", "zeroed", "aligned16", "preserved", "zero_tail")):
            raise AssertionError(r)
    result = dict(scope="Two private host heaps, selected allocation outcomes and lifecycle contracts",
                  completed=True, reserve_bytes=0x100000, commit_bytes=0x1000, records=rows,
                  ntdll_sha256=hashlib.sha256(Path("C:/Windows/System32/ntdll.dll").read_bytes()).hexdigest(),
                  limitations=["No guest execution, heap corruption, invalid pointers, concurrency or allocation exhaustion tested.",
                               "Successful 3 MiB allocation does not specify exact reserve/commit policy."])
    out = Path(__file__).resolve().parents[2] / "build/rtl-heap-windows.json"
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"completed": True, "records": len(rows), "allocation_failures": [r for r in rows if r.get("success") is False], "output": str(out)}))


if __name__ == "__main__":
    main()
