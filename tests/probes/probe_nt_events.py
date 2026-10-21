"""Observe anonymous NT events owned by this probe. Every wait is nonblocking.

No target process, named objects, persistent changes, or guest execution.
Output is host contract evidence, not proof of Winux support.
"""
import ctypes as C
import json
import hashlib
from pathlib import Path


def main():
    dll = C.WinDLL("ntdll")
    handle, status = C.c_void_p, C.c_int32
    create = dll.NtCreateEvent
    create.argtypes = [C.POINTER(handle), C.c_uint32, C.c_void_p, C.c_uint32, C.c_ubyte]
    create.restype = status
    set_event = dll.NtSetEvent
    set_event.argtypes = [handle, C.POINTER(C.c_int32)]
    set_event.restype = status
    wait = dll.NtWaitForSingleObject
    wait.argtypes = [handle, C.c_ubyte, C.POINTER(C.c_int64)]
    wait.restype = status
    close = dll.NtClose
    close.argtypes = [handle]
    close.restype = status
    rows = []

    def record(case, operation, result, **extra):
        rows.append(dict(case=case, operation=operation, status=f"{result & 0xffffffff:08x}", **extra))

    for kind in (0, 1):
        for initial in (0, 1):
            case = f"type{kind}-initial{initial}"
            h = handle()
            s = create(C.byref(h), 0x100003, None, kind, initial)
            record(case, "create", s)
            if s < 0 or not h.value:
                raise RuntimeError(f"Create failed: {case}: {s & 0xffffffff:08x}")
            try:
                for phase in ("initial", "after_set"):
                    if phase == "after_set":
                        for repeat in range(2):
                            previous = C.c_int32(-999)
                            s = set_event(h, C.byref(previous))
                            record(case, f"set{repeat}", s, previous=previous.value)
                    for repeat in range(2):
                        timeout = C.c_int64(0)
                        record(case, f"wait_{phase}{repeat}", wait(h, 0, C.byref(timeout)))
            finally:
                record(case, "close", close(h))
    # Null is not an owned handle; calls only inspect it. Avoid stale-handle races.
    timeout = C.c_int64(0)
    record("null", "wait", wait(None, 0, C.byref(timeout)))
    record("null", "set", set_event(None, None))
    record("null", "close", close(None))
    source = Path("C:/Windows/System32/ntdll.dll")
    result = dict(scope="Host-only anonymous events, single thread, zero-timeout waits",
                  ntdll_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), records=rows,
                  limitations=["No blocking, contention, alert delivery, named-object, access-denial or guest test.",
                               "Event polling behavior alone does not implement an NT scheduler."])
    out = Path(__file__).resolve().parents[2] / "build/nt-events-windows.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
