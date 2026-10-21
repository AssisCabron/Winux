"""Read-only address-to-module observations in this process, no loader mutation."""
import ctypes as C
import hashlib
import json
from pathlib import Path


def main():
    n = C.WinDLL('ntdll')
    fn = n.RtlPcToFileHeader
    fn.argtypes = [C.c_void_p, C.POINTER(C.c_void_p)]
    fn.restype = C.c_void_p
    records = []
    for name in ('ntdll', 'kernelbase', 'msvcrt'):
        module = C.WinDLL(name)
        base = module._handle
        dos = C.string_at(base, 64)
        pe = int.from_bytes(dos[60:64], 'little')
        image_size = int.from_bytes(C.string_at(base + pe + 24 + 56, 4), 'little')
        for label, offset in [('base', 0), ('dos-interior', 16), ('last-image-byte', image_size - 1)]:
            out = C.c_void_p(0xaaaa)
            result = fn(base + offset, C.byref(out))
            assert result == base and out.value == base
            records.append(dict(module=name, step=label, result_is_base=True, output_is_base=True))
    owned = C.create_string_buffer(64)
    for label, address in [('null', 0), ('private-buffer', C.addressof(owned))]:
        out = C.c_void_p(0xaaaa)
        result = fn(address, C.byref(out))
        assert not result
        records.append(dict(step=label, result=result or 0, output=out.value or 0))
    source = Path('C:/Windows/System32/ntdll.dll')
    result = dict(scope='This process only; loaded images and private buffer; no unload/concurrency',
                  ntdll_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), records=records)
    path = Path(__file__).resolve().parents[2] / 'build/pc-to-file-header-windows.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
