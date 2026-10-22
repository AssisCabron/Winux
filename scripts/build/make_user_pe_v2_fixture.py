"""Versioned USER-PE64 fixture with a real CPL3 read of PE header MZ.
The first fixture and its SHA are preserved forever.
"""
from pathlib import Path
import hashlib,json,struct
R=Path(__file__).resolve().parents[2]
D=R/'build'/'user-pe-preview'
old=D/'user-pe-fat16.img'
legacy=json.loads((D/'user-pe-fixture.json').read_text(encoding='utf8'))
assert hashlib.sha256(old.read_bytes()).hexdigest()==legacy['disk_sha256']
good=D/'user-pe-fat16-v2.img';tampered=D/'user-pe-tampered-fat16-v2.img'
if good.exists() or tampered.exists():raise RuntimeError('Refuse overwrite of PE fixtures v2')
b=bytearray(old.read_bytes())
DATA=32+2*128+32
first,second=legacy['files']['USER64.EXE']['clusters']
offset=(DATA+second-2)*512
assert b[offset]==0xb8
# movabs rax,ImageBase; cmp word [rax],MZ; jne UD2; jmp success
prefix=b'\x48\xb8'+struct.pack('<Q',0x10efff000)+b'\x66\x81\x38\x4d\x5a\x75\x02\xeb\x02\x0f\x0b'
assert len(prefix)==21
original=bytes(b[offset:offset+64])
b[offset:offset+64]=prefix+original[:43] # Keep the static message at .text+0x100.
def fnv(buf):
    a=14695981039346656037
    for c in buf:a=((a^c)*1099511628211)&0xffffffffffffffff
    return a
# A PE file has a 512B header cluster and a 512B .text cluster.
payload=b[(DATA+first-2)*512:(DATA+first-1)*512]+b[offset:offset+512]
assert len(payload)==1024
manifest=json.loads(json.dumps(legacy))
manifest['disk']=str(good)
manifest['files']['USER64.EXE']['sha256']=hashlib.sha256(payload).hexdigest()
manifest['files']['USER64.EXE']['fnv64']=f'{fnv(payload):016x}'
good.write_bytes(b)
manifest['disk_sha256']=hashlib.sha256(b).hexdigest()
(D/'user-pe-fixture-v2.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
# New tampered image: one byte in code is altered; PE headers remain valid.
altered=bytearray(b)
assert altered[offset]==0x48
altered[offset]=0x90
tampered.write_bytes(altered)
bad={'disk':str(tampered),'sha256':hashlib.sha256(altered).hexdigest(),
     'base_sha256':manifest['disk_sha256'],'mutated_file':'APPS/USER64.EXE',
     'changed_cluster':second,'expected_result':'USER_PE_REJECTED: file integrity mismatch'}
(D/'user-pe-tampered-fixture-v2.json').write_text(json.dumps(bad,indent=2)+'\n',encoding='utf8')
header=D/'user-pe-allowlist.h'
name_to_kind={'USER64.EXE':'USER','BAD64.EXE':'BAD',
              'WRITE64.EXE':'WRITE','NX64.EXE':'NX'}
header.write_text('#ifndef WINUX_USER_PE_ALLOWLIST_H\n#define WINUX_USER_PE_ALLOWLIST_H\n'+
 ''.join(f'#define WX_PE_HASH_{kind} 0x{manifest["files"][name]["fnv64"]}ULL\n'
         for name,kind in name_to_kind.items())+'#endif\n',encoding='utf8')
print(json.dumps({'disk_sha256':manifest['disk_sha256'],
                  'tampered_sha256':bad['sha256'],
                  'user_pe_sha256':manifest['files']['USER64.EXE']['sha256'],
                  'user_fnv64':manifest['files']['USER64.EXE']['fnv64']},indent=2))
