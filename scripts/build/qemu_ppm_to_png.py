"""Convert QEMU P6 screendump to PNG using Python stdlib (no Pillow dependency)."""
from pathlib import Path
import struct,zlib
root=Path(__file__).resolve().parents[2]
source=root/'build/orbis-gui-desktop.ppm'
target=root/'build/orbis-gui-desktop.png'
with source.open('rb') as f:
    assert f.readline().strip()==b'P6'
    size=f.readline().strip()
    while size.startswith(b'#'):size=f.readline().strip()
    w,h=map(int,size.split())
    assert f.readline().strip()==b'255'
    data=f.read()
assert len(data)>=w*h*3
scan=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
def chunk(name,payload):
    return struct.pack('>I',len(payload))+name+payload+struct.pack('>I',zlib.crc32(name+payload)&0xffffffff)
result=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan,6))+chunk(b'IEND',b'')
target.write_bytes(result)
print(str(target),w,h,len(result))
