"""Owned buffers only; never modifies host PEB/NLS or calls DLL attach."""
import ctypes as C
import hashlib, json
from pathlib import Path

class String(C.Structure):
    _fields_=[('length',C.c_uint16),('maximum',C.c_uint16),('data',C.c_void_p)]

def main():
    n=C.WinDLL('ntdll');k=C.WinDLL('kernel32')
    k.GetACP.restype=C.c_uint
    assert k.GetACP()==1252, 'Fixture requires the guest ACP 1252'
    convert=n.RtlUnicodeStringToAnsiString
    convert.argtypes=[C.POINTER(String),C.POINTER(String),C.c_ubyte];convert.restype=C.c_uint32
    free=n.RtlFreeAnsiString;free.argtypes=[C.POINTER(String)];free.restype=None
    values=['cmd.exe /d /c echo Winux','a\u00e7\u00e3o','\u20ac','\u4e2d','A\0B','']
    rows=[]
    for i,text in enumerate(values):
        raw=text.encode('utf-16-le');source=C.create_string_buffer(raw+b'\0\0')
        src=String(len(raw),len(raw)+2,C.addressof(source));dst=String()
        status=convert(C.byref(dst),C.byref(src),1)
        assert status==0 and dst.data
        data=C.string_at(dst.data,dst.length+1).hex()
        rows.append(f'NT_ANSI alloc{i} {status},{dst.length},{dst.maximum},{data}')
        free(C.byref(dst));assert not dst.length and not dst.maximum and not dst.data
    source=C.create_string_buffer('ABC'.encode('utf-16-le')+b'\0\0');src=String(6,8,C.addressof(source))
    for cap in [0,1,3,4]:
        buffer=(C.c_ubyte*8)(*([0xa5]*8));dst=String(0,cap,C.addressof(buffer))
        status=convert(C.byref(dst),C.byref(src),0)
        assert dst.data==C.addressof(buffer)
        rows.append(f'NT_ANSI cap{cap} {status},{dst.length},{dst.maximum},{bytes(buffer).hex()}')
    result={'ntdll_sha256':hashlib.sha256(Path('C:/Windows/System32/ntdll.dll').read_bytes()).hexdigest(),
            'acp':1252,'rows':rows,'scope':'owned input/output, allocation/free and short buffers; no failure injection'}
    (Path(__file__).parent/'build/ansi-strings-windows.json').write_text(json.dumps(result,indent=2))
    print('\n'.join(rows))

if __name__=='__main__':main()
