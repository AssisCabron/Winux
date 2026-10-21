"""Build FAT16 ESP + UEFI El Torito optical image; no host disks."""
from pathlib import Path
import sys, struct, io, hashlib, json
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/python'))
import pycdlib

def make_fat(efi):
    # 30 MiB fits the 16-bit El Torito sector count, with FAT16 clusters.
    sectors, fat_sectors = 61440, 120
    root_sector = 1 + 2*fat_sectors
    data_sector = root_sector + 32
    image = bytearray(sectors * 512)
    image[:11] = b'\xeb\x3c\x90EXPOS001'
    struct.pack_into('<HBHBHHBHHHII', image, 11, 512, 2, 1, 2, 512, sectors, 0xf8, fat_sectors, 32, 64, 0, 0)
    image[36:39] = bytes([0x80, 0, 0x29])
    struct.pack_into('<I', image, 39, 0x20261020)
    image[43:54] = b'EXPERIMENT '
    image[54:62] = b'FAT16   '
    image[510:512] = b'\x55\xaa'
    # EFI dir cluster2; BOOT dir cluster3.
    def cluster(c): return (data_sector + (c-2)*2)*512
    def entry(name, attr, c, size=0):
        e = bytearray(32); e[:11]=name; e[11]=attr
        struct.pack_into('<H', e, 26, c); struct.pack_into('<I', e, 28, size)
        return e
    image[root_sector*512:root_sector*512+32]=entry(b'EFI        ',16,2)
    for c,parent,child in [(2,0,entry(b'BOOT       ',16,3)), (3,2,entry(b'BOOTX64 EFI',32,4,len(efi)))]:
        off=cluster(c)
        image[off:off+96]=entry(b'.          ',16,c)+entry(b'..         ',16,parent)+child
    count=(len(efi)+1023)//1024
    assert count+4 < (sectors-data_sector)//2
    assert (sectors-data_sector)//2+2 <= fat_sectors*256
    fat=bytearray(fat_sectors*512)
    for c,value in [(0,0xfff8),(1,0xffff),(2,0xffff),(3,0xffff)]: struct.pack_into('<H',fat,c*2,value)
    for c in range(4,4+count): struct.pack_into('<H',fat,c*2,0xffff if c==3+count else c+1)
    image[512:(1+fat_sectors)*512]=fat
    image[(1+fat_sectors)*512:root_sector*512]=fat
    image[cluster(4):cluster(4)+len(efi)]=efi
    assert len(image)==sectors*512
    return image

build=ROOT/'build'; efi=(build/'BOOTX64.EFI').read_bytes()
assert efi[:2]==b'MZ'
esp=make_fat(efi); (build/'esp.img').write_bytes(esp)
iso=pycdlib.PyCdlib(); iso.new(interchange_level=3, vol_ident='EXPERIMENTAL_OS')
iso.add_fp(io.BytesIO(esp),len(esp),iso_path='/ESP.IMG;1')
iso.add_eltorito('/ESP.IMG;1',bootcatfile='/BOOT.CAT;1',platform_id=0xef,efi=True,boot_load_size=len(esp)//512)
iso.write(str(build/'winux-0.38-x64.iso'));iso.close()
# Reopen and verify ISO payload, catalog platform, and FAT directory path.
check=pycdlib.PyCdlib(); check.open(str(build/'winux-0.38-x64.iso'))
out=io.BytesIO();check.get_file_from_iso_fp(out,iso_path='/ESP.IMG;1')
assert out.getvalue()==esp
assert check.eltorito_boot_catalog.validation_entry.platform_id==0xef
check.close()
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [build/'BOOTX64.EFI',build/'esp.img',build/'winux-0.38-x64.iso']}
(build/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
print(json.dumps(hashes,indent=2))

