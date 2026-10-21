"""Read-only conversion fixtures from local KernelBase; does not modify NLS state."""
import ctypes as C
import hashlib
import json
from pathlib import Path


class CPINFO(C.Structure):
    _fields_ = [('MaxCharSize', C.c_uint), ('DefaultChar', C.c_ubyte * 2),
                ('LeadByte', C.c_ubyte * 12)]


def main():
    k = C.WinDLL('kernelbase', use_last_error=True)
    k.GetCPInfo.argtypes = [C.c_uint, C.POINTER(CPINFO)]
    k.GetCPInfo.restype = C.c_int
    k.IsValidCodePage.argtypes = [C.c_uint]
    k.IsValidCodePage.restype = C.c_int
    k.WideCharToMultiByte.argtypes = [C.c_uint, C.c_uint, C.c_wchar_p, C.c_int,
                                    C.c_void_p, C.c_int, C.c_void_p, C.c_void_p]
    k.WideCharToMultiByte.restype = C.c_int
    records = []
    for cp in (1252, 850):
        info = CPINFO()
        assert k.GetCPInfo(cp, C.byref(info))
        records.append(dict(step='cpinfo', cp=cp, valid=bool(k.IsValidCodePage(cp)),
                            max_char_size=info.MaxCharSize,
                            default=list(info.DefaultChar), lead=list(info.LeadByte)))
        for label, value in [('ascii', 'cmd.exe /c echo Winux'),
                             ('portuguese', 'a\u00e7\u00e3o \u00c1\u00c9'),
                             ('euro', '\u20ac'), ('unmappable', '\u4e2d'),
                             ('embedded-nul', 'A\0B')]:
            count = len(value.encode('utf-16-le')) // 2
            size = k.WideCharToMultiByte(cp, 0, value, count, None, 0, None, None)
            assert size > 0
            out = C.create_string_buffer(size)
            written = k.WideCharToMultiByte(cp, 0, value, count, out, size, None, None)
            assert written == size
            records.append(dict(step=label, cp=cp, utf16=value.encode('utf-16-le').hex(),
                                input_units=count, size=size, output=out.raw.hex()))
            if size > 1:
                short = C.create_string_buffer(size - 1)
                C.set_last_error(0)
                result = k.WideCharToMultiByte(cp, 0, value, count, short, size - 1, None, None)
                error = C.get_last_error()
                assert result == 0 and error == 122
                records.append(dict(step=label+'-short', cp=cp, result=result, error=error))
        out = C.create_string_buffer(8)
        size = k.WideCharToMultiByte(cp, 0, 'ABC', -1, out, 8, None, None)
        assert size == 4 and out.raw[:size] == b'ABC\0'
        records.append(dict(step='terminated', cp=cp, size=size, output=out.raw[:size].hex()))
    source = Path('C:/Windows/System32/KernelBase.dll')
    result = dict(scope='Host API observations only; no guest execution; flags=0, CP1252/850',
                  source=str(source), sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  records=records)
    path = Path(__file__).resolve().parents[2] / 'build' / 'codepages-windows.json'
    path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(path=str(path), records=len(records))))


if __name__ == '__main__':
    main()
