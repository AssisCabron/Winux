"""Private FLS indices in this process; never clear the host thread's FLS data."""
import ctypes as C
import hashlib
import json
from pathlib import Path


def main():
    n = C.WinDLL('ntdll')
    callback_type = C.WINFUNCTYPE(None, C.c_void_p)
    calls = []

    @callback_type
    def callback(value):
        calls.append(value or 0)

    specs = {
        'RtlFlsAlloc': (C.c_long, [C.c_void_p, C.POINTER(C.c_uint)]),
        'RtlFlsSetValue': (C.c_long, [C.c_uint, C.c_void_p]),
        'RtlFlsGetValue': (C.c_long, [C.c_uint, C.POINTER(C.c_void_p)]),
        'RtlFlsFree': (C.c_long, [C.c_uint]),
    }
    for name, (result, args) in specs.items():
        fn = getattr(n, name)
        fn.restype, fn.argtypes = result, args
    records, owned = [], []

    def record(step, *values):
        records.append({'step': step, 'values': list(values)})

    try:
        for i in range(32):
            index = C.c_uint(0xffffffff)
            status = n.RtlFlsAlloc(C.cast(callback, C.c_void_p) if i < 2 else None, C.byref(index)) & 0xffffffff
            if status or index.value == 0xffffffff or index.value in owned:
                raise RuntimeError(('allocate', i, status))
            owned.append(index.value)
        record('allocate', len(owned), len(set(owned)) == len(owned))
        # Explicitly establish thread FLS data, including in a fresh guest TEB.
        assert n.RtlFlsSetValue(owned[0], None) == 0
        out = C.c_void_p(0xaaaa)
        status = n.RtlFlsGetValue(owned[0], C.byref(out)) & 0xffffffff
        record('get-empty', status, out.value or 0)
        record('set-first', n.RtlFlsSetValue(owned[0], C.c_void_p(0x1111)) & 0xffffffff, len(calls))
        record('set-overwrite', n.RtlFlsSetValue(owned[0], C.c_void_p(0x2222)) & 0xffffffff, len(calls))
        status = n.RtlFlsGetValue(owned[0], C.byref(out)) & 0xffffffff
        record('get-value', status, out.value or 0)
        status = n.RtlFlsFree(owned[0]) & 0xffffffff
        if status:
            raise RuntimeError(('free', status))
        owned.pop(0)
        record('free-callback', status, len(calls), calls[-1] if calls else 0)
        status = n.RtlFlsFree(owned[0]) & 0xffffffff
        if status:
            raise RuntimeError(('free-empty', status))
        owned.pop(0)
        record('free-empty', status, len(calls))
        out = C.c_void_p(0xaaaa)
        status = n.RtlFlsGetValue(0xffffffff, C.byref(out)) & 0xffffffff
        record('invalid-get', status, out.value or 0)
        record('invalid-set', n.RtlFlsSetValue(0xffffffff, None) & 0xffffffff)
        record('invalid-free', n.RtlFlsFree(0xffffffff) & 0xffffffff)
        for index in reversed(owned):
            if n.RtlFlsFree(index):
                raise RuntimeError('cleanup failed')
        owned.clear()
        record('cleanup', len(calls))
        assert calls == [0x2222]
    finally:
        for index in reversed(owned):
            n.RtlFlsFree(index)
    result = {
        'completed': True, 'records': records,
        'ntdll_sha256': hashlib.sha256(Path('C:/Windows/System32/ntdll.dll').read_bytes()).hexdigest(),
        'limits': 'Own indices only. No RtlProcessFlsData on host, thread destruction, fibers, concurrency or exhaustion. Numeric slot identities are deliberately not compared.',
    }
    out = Path(__file__).parent / 'build/rtl-fls-windows.json'
    out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
