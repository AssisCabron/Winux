"""Make a separate *tampered* FAT16 image for fail-closed verification."""
from pathlib import Path
import json,hashlib
R=Path(__file__).resolve().parents[2]
D=R/'build'/'user-pe-preview'
base=D/'user-pe-fat16.img'
target=D/'user-pe-tampered-fat16.img'
fixture=json.loads((D/'user-pe-fixture.json').read_text(encoding='utf8'))
assert hashlib.sha256(base.read_bytes()).hexdigest()==fixture['disk_sha256']
if target.exists():raise RuntimeError('Refuse overwrite of tampered fixture')
data=bytearray(base.read_bytes())
data_sector=32+2*128+32
second_cluster=fixture['files']['USER64.EXE']['clusters'][1]
offset=(data_sector+second_cluster-2)*512
assert data[offset]==0xb8 # mov eax,CONSOLE_WRITE at PE .text start
data[offset]=0x90 # mutate only executable payload, leave PE structure intact
target.write_bytes(data)
meta={'disk':str(target),'sha256':hashlib.sha256(data).hexdigest(),
      'base_sha256':fixture['disk_sha256'],
      'mutated_file':'APPS/USER64.EXE',
      'changed_cluster':second_cluster,
      'changed_offset':offset,'expected_result':'USER_PE_REJECTED: file integrity mismatch'}
(D/'user-pe-tampered-fixture.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf8')
print(json.dumps(meta,indent=2))
