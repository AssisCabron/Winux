"""Read-only staging plan for the inventoried local Windows DLL closure."""
from pathlib import Path
import hashlib,json,sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT.parent/'windows-analysis/cmd'))
from dependency_graph import ApiSet

def prepare(enabled):
    rows=[];files=[]
    if enabled:
        graph=json.loads((ROOT.parent/'windows-analysis/cmd/dependency-startup.json').read_text())
        system=Path('C:/Windows/System32')
        api=ApiSet(system/'apisetschema.dll')
        if api.metadata['sha256']!=graph['api_set']['sha256']:
            raise ValueError('API-set schema changed; regenerate and review dependency inventory')
        for m in graph['modules']:
            p=system/m['name']
            if p.name!=m['name'] or '/' in m['name'] or '\\' in m['name']:
                raise ValueError('Expected flat System32 names')
            if hashlib.sha256(p.read_bytes()).hexdigest()!=m['sha256']:
                raise ValueError(f'{m["name"]} changed; regenerate dependency inventory')
            if m['name']!='cmd.exe':files.append(p)
        for contract,values in api.entries.items():
            for v in values:
                rows.append((contract,v['parent'],v['host']))
        # Retain the NLS data for the observed local process profile. Fail closed
        # on source changes; these hashes are evidence, not universal NLS files.
        nls=json.loads((ROOT.parent/'windows-analysis/cmd/nls-inputs.json').read_text())
        for item in nls['files']:
            p=Path(item['path'])
            data=p.read_bytes()
            if len(data)!=item['bytes'] or hashlib.sha256(data).hexdigest()!=item['sha256']:
                raise ValueError(f'{p.name} changed; review NLS inputs before packaging')
            files.append(p)
    header=['/* Local schema metadata only; not Windows implementation code. */',
            'typedef struct {const char *contract,*parent,*host;} api_set_entry;',
            'static const api_set_entry api_sets[]={']
    header.extend('{'+','.join(json.dumps(v) for v in row)+'},' for row in rows)
    header.append('{0,0,0}};')
    header.append(f'#define API_SET_COUNT {len(rows)}')
    (ROOT/'build/apisets.h').write_text('\n'.join(header),encoding='ascii')
    return files
